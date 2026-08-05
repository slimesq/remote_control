#include "internal/RemoteControlTransportImpl.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <algorithm>
#include <filesystem>

namespace
{

constexpr int DownloadChunkSize{64 * 1024};
constexpr int DirectoryEntriesPerBatch{64};

using iocp_detail::makeStatusPacket;

/**
 * @brief Encodes a file size as a download header.
 * @param _size File size, or a negative failure marker.
 * @return Little-endian file-size payload.
 */
QByteArray makeFileSizePayload(qint64 _size)
{
    QByteArray payload;
    QDataStream stream{&payload, QIODevice::WriteOnly};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << _size;
    return payload;
}

/**
 * @brief Removes a directory tree while observing server shutdown.
 * @param _path Root file or directory to remove.
 * @param _stopping Server shutdown flag.
 * @return true when the complete target is removed; otherwise false.
 */
bool removeRecursively(QString const& _path, std::atomic_bool const& _stopping)
{
    if (_stopping.load())
    {
        return false;
    }

    QFileInfo const rootInfo{_path};
    if (rootInfo.isJunction() || (rootInfo.isSymbolicLink() && rootInfo.isDir()))
    {
        // Remove directory reparse points themselves; never traverse into their targets.
        return QDir{rootInfo.absolutePath()}.rmdir(rootInfo.fileName());
    }
    if (rootInfo.isSymLink() || !rootInfo.isDir())
    {
        return QFile::remove(_path);
    }

    QDir const directory{_path};
    QFileInfoList const entries{directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                        QDir::Hidden | QDir::System)};
    for (QFileInfo const& entry : entries)
    {
        if (_stopping.load() || !removeRecursively(entry.absoluteFilePath(), _stopping))
        {
            return false;
        }
    }

    QDir const parentDirectory{rootInfo.absolutePath()};
    return !_stopping.load() && parentDirectory.rmdir(rootInfo.fileName());
}

}  // namespace

bool RemoteControlTransport::Impl::scheduleFileRequest(
    std::shared_ptr<ConnectionContext> const& _connection, remote_control::Packet const& _packet)
{
    std::weak_ptr<ConnectionContext> const weakConnection{_connection};
    return this->m_fileTaskPool.submit([this, weakConnection, _packet] {
        std::shared_ptr<ConnectionContext> const connection{weakConnection.lock()};
        if (!connection || connection->state.isTerminal() || this->m_stopping.load())
        {
            return;
        }
        switch (_packet.command)
        {
            case remote_control::Command::ListDirectory:
                this->streamDirectory(connection, _packet.payload);
                break;
            case remote_control::Command::DownloadFile:
                this->streamDownload(connection, _packet.payload);
                break;
            case remote_control::Command::DeleteFile:
                this->deleteTarget(connection, _packet.payload);
                break;
            default:
                this->closeConnection(connection, ConnectionCloseReason::ProtocolViolation);
                break;
        }
    });
}

void RemoteControlTransport::Impl::streamDirectory(
    std::shared_ptr<ConnectionContext> const& _connection, QByteArray const& _payload)
{
    QString const path{remote_control::decodeUtf8(_payload)};
    QFileInfo const directoryInfo{path};
    QDir const directory{path};
    if (!this->m_hostServices.isFilePathAllowed(path) || !directoryInfo.exists() ||
        !directoryInfo.isDir() || !directory.isReadable())
    {
        remote_control::FileEntry invalidEntry;
        invalidEntry.isInvalid = true;
        invalidEntry.hasNext = false;
        this->sendFinalPacket(_connection,
                              {remote_control::Command::ListDirectory, invalidEntry.toPayload()});
        return;
    }

    auto transfer{std::make_shared<FileTransferState>(FileTransferKind::Directory)};
    transfer->directoryIterator = std::make_unique<QDirIterator>(
        path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    {
        std::lock_guard<std::mutex> const lock{_connection->fileTransferMutex};
        if (this->m_stopping.load() || _connection->state.isTerminal())
        {
            return;
        }
        _connection->fileTransfer = transfer;
    }
    this->queueDirectoryBatch(_connection, transfer);
}

void RemoteControlTransport::Impl::queueDirectoryBatch(
    std::shared_ptr<ConnectionContext> const& _connection,
    std::shared_ptr<FileTransferState> const& _transfer)
{
    if (this->m_stopping.load() || _connection->state.isTerminal())
    {
        return;
    }

    QByteArray bytes;
    int encodedEntryCount{0};
    while (_transfer->directoryIterator && _transfer->directoryIterator->hasNext() &&
           encodedEntryCount < DirectoryEntriesPerBatch)
    {
        _transfer->directoryIterator->next();
        QFileInfo const info{_transfer->directoryIterator->fileInfo()};
        remote_control::FileEntry entry;
        entry.isDirectory = info.isDir();
        entry.fileName = info.fileName();
        QByteArray const packetBytes{
            remote_control::Packet{remote_control::Command::ListDirectory, entry.toPayload()}
                .serialize()};
        if (packetBytes.isEmpty())
        {
            this->closeConnection(_connection, ConnectionCloseReason::InternalFailure);
            return;
        }
        bytes.append(packetBytes);
        ++encodedEntryCount;
    }

    if (!_transfer->directoryIterator || !_transfer->directoryIterator->hasNext())
    {
        remote_control::FileEntry terminalEntry;
        terminalEntry.hasNext = false;
        QByteArray const packetBytes{remote_control::Packet{remote_control::Command::ListDirectory,
                                                            terminalEntry.toPayload()}
                                         .serialize()};
        if (packetBytes.isEmpty())
        {
            this->closeConnection(_connection, ConnectionCloseReason::InternalFailure);
            return;
        }
        bytes.append(packetBytes);
        _transfer->finished = true;
    }

    if (!this->enqueueBytes(_connection, bytes))
    {
        this->closeConnection(_connection, ConnectionCloseReason::Backpressure);
    }
}

void RemoteControlTransport::Impl::streamDownload(
    std::shared_ptr<ConnectionContext> const& _connection, QByteArray const& _payload)
{
    QString const path{remote_control::decodeUtf8(_payload)};
    if (!this->m_hostServices.isFilePathAllowed(path))
    {
        this->sendFinalPacket(_connection,
                              {remote_control::Command::DownloadFile, makeFileSizePayload(-1)});
        return;
    }

    auto transfer{std::make_shared<FileTransferState>(FileTransferKind::Download)};
    transfer->file.open(std::filesystem::path{path.toStdWString()}, std::ios::binary);
    if (!transfer->file.is_open())
    {
        this->sendFinalPacket(_connection,
                              {remote_control::Command::DownloadFile, makeFileSizePayload(-1)});
        return;
    }

    transfer->file.seekg(0, std::ios::end);
    std::streamoff const fileSize{transfer->file.tellg()};
    transfer->file.seekg(0, std::ios::beg);
    if (fileSize < 0 || !transfer->file)
    {
        this->sendFinalPacket(_connection,
                              {remote_control::Command::DownloadFile, makeFileSizePayload(-1)});
        return;
    }
    transfer->remainingBytes = static_cast<qint64>(fileSize);
    {
        std::lock_guard<std::mutex> const lock{_connection->fileTransferMutex};
        if (this->m_stopping.load() || _connection->state.isTerminal())
        {
            return;
        }
        _connection->fileTransfer = transfer;
    }
    this->queueDownloadChunk(_connection, transfer);
}

void RemoteControlTransport::Impl::queueDownloadChunk(
    std::shared_ptr<ConnectionContext> const& _connection,
    std::shared_ptr<FileTransferState> const& _transfer)
{
    if (this->m_stopping.load() || _connection->state.isTerminal())
    {
        return;
    }

    QByteArray bytes;
    if (_transfer->headerPending)
    {
        bytes = remote_control::Packet{remote_control::Command::DownloadFile,
                                       makeFileSizePayload(_transfer->remainingBytes)}
                    .serialize();
        _transfer->headerPending = false;
    }

    if (_transfer->remainingBytes > 0)
    {
        qint64 const requestedBytes{std::min<qint64>(_transfer->remainingBytes, DownloadChunkSize)};
        QByteArray chunk{static_cast<int>(requestedBytes), Qt::Uninitialized};
        _transfer->file.read(chunk.data(), static_cast<std::streamsize>(requestedBytes));
        std::streamsize const readBytes{_transfer->file.gcount()};
        if (readBytes <= 0)
        {
            this->closeConnection(_connection, ConnectionCloseReason::IoFailure);
            return;
        }
        chunk.resize(static_cast<int>(readBytes));
        QByteArray const packetBytes{
            remote_control::Packet{remote_control::Command::DownloadFile, chunk}.serialize()};
        if (packetBytes.isEmpty())
        {
            this->closeConnection(_connection, ConnectionCloseReason::InternalFailure);
            return;
        }
        bytes.append(packetBytes);
        _transfer->remainingBytes -= chunk.size();
    }

    _transfer->finished = _transfer->remainingBytes == 0;
    if (bytes.isEmpty() || !this->enqueueBytes(_connection, bytes))
    {
        this->closeConnection(_connection, ConnectionCloseReason::Backpressure);
    }
}

void RemoteControlTransport::Impl::continueFileTransfer(
    std::shared_ptr<ConnectionContext> const& _connection)
{
    std::shared_ptr<FileTransferState> transfer;
    {
        std::lock_guard<std::mutex> const lock{_connection->fileTransferMutex};
        transfer = _connection->fileTransfer;
        if (!transfer)
        {
            return;
        }
        if (transfer->finished)
        {
            _connection->fileTransfer.reset();
        }
    }

    if (transfer->finished)
    {
        this->closeConnection(_connection, ConnectionCloseReason::RequestComplete);
        return;
    }

    std::weak_ptr<ConnectionContext> const weakConnection{_connection};
    bool const submitted{this->m_fileTaskPool.submit([this, weakConnection, transfer] {
        std::shared_ptr<ConnectionContext> const connection{weakConnection.lock()};
        if (!connection || connection->state.isTerminal() || this->m_stopping.load())
        {
            return;
        }
        if (transfer->kind == FileTransferKind::Directory)
        {
            this->queueDirectoryBatch(connection, transfer);
        }
        else
        {
            this->queueDownloadChunk(connection, transfer);
        }
    })};
    if (!submitted)
    {
        this->closeConnection(_connection, ConnectionCloseReason::TaskRejected);
    }
}

void RemoteControlTransport::Impl::deleteTarget(
    std::shared_ptr<ConnectionContext> const& _connection, QByteArray const& _payload)
{
    QString const path{remote_control::decodeUtf8(_payload)};
    QFileInfo const info{path};
    bool success{false};
    QString message;
    if (!this->m_hostServices.isFilePathAllowed(path))
    {
        message = QObject::tr("Only local drive paths are supported: %1").arg(path);
    }
    else if (!info.exists())
    {
        message = QObject::tr("Target does not exist: %1").arg(path);
    }
    else
    {
        success = removeRecursively(path, this->m_stopping);
        message = success ? QObject::tr("Delete completed.")
                          : QObject::tr("Failed to delete target: %1").arg(path);
    }

    this->sendFinalPacket(_connection,
                          makeStatusPacket(remote_control::Command::DeleteFile, success, message));
}
