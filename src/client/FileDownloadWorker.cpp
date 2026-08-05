#include "client/FileDownloadWorker.h"

#include "common/Packet.h"

#include <QDataStream>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTimer>

#include <memory>

namespace
{

constexpr int DownloadInactivityTimeoutMs{15000};

}  // namespace

FileDownloadWorker::FileDownloadWorker() : m_timeoutTimer{new QTimer{this}}
{
    this->m_timeoutTimer->setSingleShot(true);
    this->m_timeoutTimer->setInterval(DownloadInactivityTimeoutMs);
    QObject::connect(this->m_timeoutTimer, &QTimer::timeout, this, &FileDownloadWorker::onTimeout);
}

FileDownloadWorker::~FileDownloadWorker()
{
    if (this->m_outputFile && this->m_outputFile->isOpen())
    {
        this->m_outputFile->cancelWriting();
    }
    this->resetSocket();
}

void FileDownloadWorker::startDownload(QString const& _host,
                                       quint16 _port,
                                       QString const& _remotePath,
                                       QString const& _localPath,
                                       quint64 _endpointGeneration,
                                       quint64 _downloadGeneration)
{
    // Shutdown is terminal; a live worker also serializes downloads onto one socket and save file.
    if (this->m_state == DownloadState::ShuttingDown)
    {
        return;
    }
    if (this->m_state == DownloadState::Downloading)
    {
        emit this->finished(_endpointGeneration,
                            _downloadGeneration,
                            _remotePath,
                            _localPath,
                            false,
                            tr("Another download is already active."));
        return;
    }

    this->m_host = _host.trimmed();
    this->m_port = _port;
    this->m_remotePath = _remotePath;
    this->m_localPath = _localPath;
    this->m_endpointGeneration = _endpointGeneration;
    this->m_downloadGeneration = _downloadGeneration;
    this->m_expectedFileSize = -1;
    this->m_writtenBytes = 0;
    this->m_buffer.clear();
    this->m_state = DownloadState::Downloading;

    // All endpoint and path components are required before creating any transport resources.
    if (this->m_host.isEmpty() || this->m_port == 0 || this->m_remotePath.isEmpty() ||
        this->m_localPath.isEmpty())
    {
        this->fail(tr("The download request is invalid."));
        return;
    }

    // Construct the QObject-backed file in the worker thread that exclusively uses it.
    this->m_outputFile = std::make_unique<QSaveFile>(this->m_localPath);
    if (!this->m_outputFile->open(QIODevice::WriteOnly))
    {
        this->fail(tr("Unable to write the local file: %1").arg(this->m_localPath));
        return;
    }

    this->m_socket = new QTcpSocket{this};
    QObject::connect(
        this->m_socket, &QTcpSocket::connected, this, &FileDownloadWorker::onConnected);
    QObject::connect(
        this->m_socket, &QTcpSocket::readyRead, this, &FileDownloadWorker::onReadyRead);
    QObject::connect(
        this->m_socket, &QTcpSocket::disconnected, this, &FileDownloadWorker::onDisconnected);
    QObject::connect(
        this->m_socket, &QTcpSocket::errorOccurred, this, &FileDownloadWorker::onErrorOccurred);
    this->m_timeoutTimer->start();
    this->m_socket->connectToHost(this->m_host, this->m_port);
}

void FileDownloadWorker::cancelActiveDownload(quint64 _endpointGeneration,
                                              quint64 _downloadGeneration)
{
    if (this->m_state == DownloadState::Downloading)
    {
        // Report intentional cancellation in the caller's current generation so the GUI can close
        // progress state while all previously queued data remains stale.
        this->m_endpointGeneration = _endpointGeneration;
        this->m_downloadGeneration = _downloadGeneration;
        this->fail(tr("Download cancelled because the remote endpoint changed."));
    }
}

void FileDownloadWorker::shutdown()
{
    // Preserve whether an incomplete temporary file must be cancelled before entering shutdown.
    bool const wasDownloading{this->m_state == DownloadState::Downloading};
    this->m_state = DownloadState::ShuttingDown;
    if (wasDownloading)
    {
        this->m_timeoutTimer->stop();
        if (this->m_outputFile && this->m_outputFile->isOpen())
        {
            this->m_outputFile->cancelWriting();
        }
        this->m_outputFile.reset();
    }
    this->resetSocket();
}

void FileDownloadWorker::onConnected()
{
    remote_control::Packet const request{remote_control::Command::DownloadFile,
                                         remote_control::encodeUtf8(this->m_remotePath)};
    QByteArray const bytes{request.serialize()};
    if (bytes.isEmpty() || this->m_socket->write(bytes) < 0)
    {
        this->fail(tr("Failed to send the download request."));
        return;
    }
    this->m_timeoutTimer->start();
}

void FileDownloadWorker::onReadyRead()
{
    this->m_timeoutTimer->start();
    this->m_buffer.append(this->m_socket->readAll());
    if (this->m_buffer.size() > remote_control::Packet::MaximumSerializedSize)
    {
        this->fail(tr("The download response exceeds the packet limit."));
        return;
    }

    // processPacket() may complete or fail the transfer, so re-check state before parsing more.
    while (this->m_state == DownloadState::Downloading)
    {
        auto const packet{remote_control::Packet::tryParse(this->m_buffer)};
        if (!packet.has_value())
        {
            return;
        }
        if (packet->command != remote_control::Command::DownloadFile)
        {
            this->fail(tr("The download connection returned an unexpected response."));
            return;
        }
        this->processPacket(packet->payload);
    }
}

void FileDownloadWorker::onDisconnected()
{
    // A disconnect after completion or during shutdown must not produce a second result.
    if (this->m_state == DownloadState::Downloading)
    {
        this->fail(tr("Download was interrupted."));
    }
}

void FileDownloadWorker::onErrorOccurred(QAbstractSocket::SocketError _error)
{
    static_cast<void>(_error);
    if (this->m_state == DownloadState::Downloading)
    {
        this->fail(this->m_socket->errorString());
    }
}

void FileDownloadWorker::onTimeout()
{
    if (this->m_state == DownloadState::Downloading)
    {
        this->fail(tr("The download timed out."));
    }
}

void FileDownloadWorker::processPacket(QByteArray const& _payload)
{
    // 1. Interpret the first payload as the fixed-width remote file-size header.
    if (this->m_expectedFileSize < 0)
    {
        // Reject malformed headers before reading the qint64 size value.
        if (_payload.size() != static_cast<int>(sizeof(qint64)))
        {
            this->fail(tr("The download header is invalid."));
            return;
        }

        QDataStream stream{_payload};
        stream.setByteOrder(QDataStream::LittleEndian);
        stream >> this->m_expectedFileSize;
        // A negative size reports that the server could not open the requested file.
        if (this->m_expectedFileSize < 0)
        {
            this->fail(tr("The remote file cannot be read."));
            return;
        }
        // Complete an empty file immediately because no data packets will follow.
        if (this->m_expectedFileSize == 0)
        {
            emit this->progress(
                this->m_endpointGeneration, this->m_downloadGeneration, this->m_remotePath, 0, 0);
            this->completeSuccessfully();
        }
        return;
    }

    // 2. Reject data that exceeds the file size declared by the server.
    if (this->m_writtenBytes + _payload.size() > this->m_expectedFileSize)
    {
        this->fail(tr("Received more download data than expected."));
        return;
    }
    // 3. Persist the complete payload and reject unavailable or partial writes.
    if (!this->m_outputFile || this->m_outputFile->write(_payload) != _payload.size())
    {
        this->fail(tr("Failed to write the local file."));
        return;
    }

    this->m_writtenBytes += _payload.size();
    emit this->progress(this->m_endpointGeneration,
                        this->m_downloadGeneration,
                        this->m_remotePath,
                        this->m_writtenBytes,
                        this->m_expectedFileSize);
    // 4. Commit the temporary file after all declared bytes have been received.
    if (this->m_writtenBytes == this->m_expectedFileSize)
    {
        this->completeSuccessfully();
    }
}

void FileDownloadWorker::completeSuccessfully()
{
    if (!this->m_outputFile || !this->m_outputFile->commit())
    {
        QString const errorMessage{this->m_outputFile ? this->m_outputFile->errorString()
                                                      : tr("The temporary file is unavailable.")};
        this->fail(tr("Failed to save the local file: %1").arg(errorMessage));
        return;
    }

    QString const remotePath{this->m_remotePath};
    QString const localPath{this->m_localPath};
    quint64 const endpointGeneration{this->m_endpointGeneration};
    quint64 const downloadGeneration{this->m_downloadGeneration};
    this->m_state = DownloadState::Idle;
    this->m_timeoutTimer->stop();
    this->m_outputFile.reset();
    this->resetSocket();
    emit this->finished(endpointGeneration,
                        downloadGeneration,
                        remotePath,
                        localPath,
                        true,
                        tr("Download completed."));
}

void FileDownloadWorker::fail(QString const& _message)
{
    // Timeout, socket, protocol, and file errors can converge here; report completion only once.
    if (this->m_state != DownloadState::Downloading)
    {
        return;
    }

    QString const remotePath{this->m_remotePath};
    QString const localPath{this->m_localPath};
    quint64 const endpointGeneration{this->m_endpointGeneration};
    quint64 const downloadGeneration{this->m_downloadGeneration};
    this->m_state = DownloadState::Idle;
    this->m_timeoutTimer->stop();
    if (this->m_outputFile && this->m_outputFile->isOpen())
    {
        this->m_outputFile->cancelWriting();
    }
    this->m_outputFile.reset();
    this->resetSocket();
    emit this->finished(
        endpointGeneration, downloadGeneration, remotePath, localPath, false, _message);
}

void FileDownloadWorker::resetSocket()
{
    this->m_buffer.clear();
    if (!this->m_socket)
    {
        return;
    }

    QObject::disconnect(this->m_socket, nullptr, this, nullptr);
    this->m_socket->abort();
    this->m_socket->deleteLater();
    this->m_socket = nullptr;
}
