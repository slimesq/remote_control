#include "server/FileRequestWorker.h"

#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>

namespace
{

constexpr int DownloadChunkSize{64 * 1024};
constexpr int NetworkPollIntervalMs{250};
constexpr int NetworkWriteTimeoutMs{15000};

/**
 * @brief Encodes a file size as a download header.
 * @param _size File size, or a negative failure marker.
 * @return Little-endian download header payload.
 */
QByteArray sizeToPayload(qint64 _size)
{
    QByteArray payload;
    QDataStream stream{&payload, QIODevice::WriteOnly};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << _size;
    return payload;
}

/**
 * @brief Creates a common command-status packet.
 * @param _command Command associated with the status.
 * @param _success Whether the command succeeded.
 * @param _message User-facing result message.
 * @return Serialized command-status packet.
 */
remote_control::Packet statusPacket(remote_control::Command _command,
                                    bool _success,
                                    QString const& _message)
{
    return {_command, remote_control::makeStatusPayload(_success, _message)};
}

}  // namespace

FileRequestWorker::FileRequestWorker(QObject* _parent) : QObject{_parent}
{
}

void FileRequestWorker::process(QTcpSocket* _socket, remote_control::Packet const& _request)
{
    this->m_socket = _socket;

    // 1. Execute only the file operations explicitly transferred by RemoteSession.
    if (!this->m_stopping.load())
    {
        switch (_request.command)
        {
            case remote_control::Command::ListDirectory:
                static_cast<void>(this->streamDirectory(_request.payload));
                break;
            case remote_control::Command::DownloadFile:
                static_cast<void>(this->streamDownload(_request.payload));
                break;
            case remote_control::Command::DeleteFile:
                static_cast<void>(this->deleteTarget(_request.payload));
                break;
            default:
                break;
        }
    }

    // 2. Release this request's socket before the reusable worker becomes idle.
    this->releaseSocket();
    emit this->requestFinished();
}

void FileRequestWorker::requestStop() noexcept
{
    this->m_stopping.store(true);
}

bool FileRequestWorker::streamDirectory(QByteArray const& _payload)
{
    QString const path{remote_control::decodeUtf8(_payload)};
    QFileInfo const directoryInfo{path};
    QDir const directory{path};
    if (!directoryInfo.exists() || !directoryInfo.isDir() || !directory.isReadable())
    {
        remote_control::FileEntry invalidEntry;
        invalidEntry.isInvalid = true;
        invalidEntry.hasNext = false;
        return this->writePacket(
            {remote_control::Command::ListDirectory, invalidEntry.toPayload()});
    }

    // Preserve stable directory-first ordering while doing the work outside the GUI thread.
    QFileInfoList const entries{directory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::DirsFirst | QDir::Name)};
    for (QFileInfo const& info : entries)
    {
        if (this->m_stopping.load())
        {
            return false;
        }
        remote_control::FileEntry entry;
        entry.isDirectory = info.isDir();
        entry.fileName = info.fileName();
        if (!this->writePacket({remote_control::Command::ListDirectory, entry.toPayload()}))
        {
            return false;
        }
    }

    remote_control::FileEntry terminalEntry;
    terminalEntry.hasNext = false;
    return this->writePacket({remote_control::Command::ListDirectory, terminalEntry.toPayload()});
}

bool FileRequestWorker::streamDownload(QByteArray const& _payload)
{
    QFile file{remote_control::decodeUtf8(_payload)};
    if (!file.open(QIODevice::ReadOnly))
    {
        return this->writePacket({remote_control::Command::DownloadFile, sizeToPayload(-1)});
    }

    if (!this->writePacket({remote_control::Command::DownloadFile, sizeToPayload(file.size())}))
    {
        return false;
    }

    // Read, serialize, and flush one bounded chunk at a time instead of buffering the file.
    while (!file.atEnd() && !this->m_stopping.load())
    {
        QByteArray const chunk{file.read(DownloadChunkSize)};
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
        {
            return false;
        }
        if (!chunk.isEmpty() && !this->writePacket({remote_control::Command::DownloadFile, chunk}))
        {
            return false;
        }
    }
    return !this->m_stopping.load();
}

bool FileRequestWorker::deleteTarget(QByteArray const& _payload)
{
    QString const path{remote_control::decodeUtf8(_payload)};
    QFileInfo const info{path};
    if (!info.exists())
    {
        return this->writePacket(statusPacket(
            remote_control::Command::DeleteFile, false, tr("Target does not exist: %1").arg(path)));
    }

    bool const success{info.isDir() && !info.isSymLink() ? this->removeRecursively(path)
                                                         : QFile::remove(path)};
    QString const message{success ? tr("Delete completed.")
                                  : tr("Failed to delete target: %1").arg(path)};
    return this->writePacket(statusPacket(remote_control::Command::DeleteFile, success, message));
}

bool FileRequestWorker::removeRecursively(QString const& _path)
{
    if (this->m_stopping.load())
    {
        return false;
    }

    QFileInfo const rootInfo{_path};
    if (rootInfo.isSymLink() || !rootInfo.isDir())
    {
        return QFile::remove(_path);
    }

    QDir const directory{_path};
    QFileInfoList const entries{directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                        QDir::Hidden | QDir::System)};
    for (QFileInfo const& entry : entries)
    {
        if (this->m_stopping.load())
        {
            return false;
        }

        bool const removed{entry.isDir() && !entry.isSymLink()
                               ? this->removeRecursively(entry.absoluteFilePath())
                               : QFile::remove(entry.absoluteFilePath())};
        if (!removed)
        {
            return false;
        }
    }

    if (this->m_stopping.load())
    {
        return false;
    }
    QDir const parentDirectory{rootInfo.absolutePath()};
    return parentDirectory.rmdir(rootInfo.fileName());
}

bool FileRequestWorker::writePacket(remote_control::Packet const& _packet)
{
    if (this->m_stopping.load())
    {
        return false;
    }

    QByteArray const bytes{_packet.serialize()};
    if (bytes.isEmpty() || this->m_socket->write(bytes) != bytes.size())
    {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (this->m_socket->bytesToWrite() > 0 && timer.elapsed() < NetworkWriteTimeoutMs)
    {
        if (this->m_stopping.load())
        {
            return false;
        }
        static_cast<void>(this->m_socket->waitForBytesWritten(NetworkPollIntervalMs));
    }
    return this->m_socket->bytesToWrite() == 0;
}

void FileRequestWorker::releaseSocket()
{
    if (this->m_stopping.load())
    {
        this->m_socket->abort();
    }
    else
    {
        this->m_socket->disconnectFromHost();
        QElapsedTimer timer;
        timer.start();
        while (this->m_socket->state() != QAbstractSocket::UnconnectedState &&
               timer.elapsed() < NetworkWriteTimeoutMs && !this->m_stopping.load())
        {
            static_cast<void>(this->m_socket->waitForDisconnected(NetworkPollIntervalMs));
        }
        if (this->m_socket->state() != QAbstractSocket::UnconnectedState)
        {
            this->m_socket->abort();
        }
    }

    this->m_socket->deleteLater();
    this->m_socket = nullptr;
}
