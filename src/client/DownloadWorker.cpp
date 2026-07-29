#include "client/DownloadWorker.h"

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

DownloadWorker::DownloadWorker() : m_timeoutTimer{new QTimer{this}}
{
    this->m_timeoutTimer->setSingleShot(true);
    this->m_timeoutTimer->setInterval(DownloadInactivityTimeoutMs);
    QObject::connect(this->m_timeoutTimer, &QTimer::timeout, this, &DownloadWorker::onTimeout);
}

DownloadWorker::~DownloadWorker()
{
    if (this->m_file && this->m_file->isOpen())
    {
        this->m_file->cancelWriting();
    }
    this->resetSocket();
}

void DownloadWorker::startDownload(QString const& _host,
                                   quint16 _port,
                                   QString const& _remotePath,
                                   QString const& _localPath)
{
    if (this->m_state == DownloadState::ShuttingDown)
    {
        return;
    }
    if (this->m_state == DownloadState::Downloading)
    {
        emit this->finished(
            _remotePath, _localPath, false, tr("Another download is already active."));
        return;
    }

    this->m_host = _host.trimmed();
    this->m_port = _port;
    this->m_remotePath = _remotePath;
    this->m_localPath = _localPath;
    this->m_expectedBytes = -1;
    this->m_receivedBytes = 0;
    this->m_buffer.clear();
    this->m_state = DownloadState::Downloading;

    if (this->m_host.isEmpty() || this->m_port == 0 || this->m_remotePath.isEmpty() ||
        this->m_localPath.isEmpty())
    {
        this->fail(tr("The download request is invalid."));
        return;
    }

    // Construct the QObject-backed file in the worker thread that exclusively uses it.
    this->m_file = std::make_unique<QSaveFile>(this->m_localPath);
    if (!this->m_file->open(QIODevice::WriteOnly))
    {
        this->fail(tr("Unable to write the local file: %1").arg(this->m_localPath));
        return;
    }

    this->m_socket = new QTcpSocket{this};
    QObject::connect(this->m_socket, &QTcpSocket::connected, this, &DownloadWorker::onConnected);
    QObject::connect(this->m_socket, &QTcpSocket::readyRead, this, &DownloadWorker::onReadyRead);
    QObject::connect(
        this->m_socket, &QTcpSocket::disconnected, this, &DownloadWorker::onDisconnected);
    QObject::connect(
        this->m_socket, &QTcpSocket::errorOccurred, this, &DownloadWorker::onErrorOccurred);
    this->m_timeoutTimer->start();
    this->m_socket->connectToHost(this->m_host, this->m_port);
}

void DownloadWorker::shutdown()
{
    bool const wasDownloading{this->m_state == DownloadState::Downloading};
    this->m_state = DownloadState::ShuttingDown;
    if (wasDownloading)
    {
        this->m_timeoutTimer->stop();
        if (this->m_file && this->m_file->isOpen())
        {
            this->m_file->cancelWriting();
        }
        this->m_file.reset();
    }
    this->resetSocket();
}

void DownloadWorker::onConnected()
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

void DownloadWorker::onReadyRead()
{
    this->m_timeoutTimer->start();
    this->m_buffer.append(this->m_socket->readAll());
    if (this->m_buffer.size() > remote_control::Packet::MaximumSerializedSize)
    {
        this->fail(tr("The download response exceeds the packet limit."));
        return;
    }

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

void DownloadWorker::onDisconnected()
{
    if (this->m_state == DownloadState::Downloading)
    {
        this->fail(tr("Download was interrupted."));
    }
}

void DownloadWorker::onErrorOccurred(QAbstractSocket::SocketError _error)
{
    static_cast<void>(_error);
    if (this->m_state == DownloadState::Downloading)
    {
        this->fail(this->m_socket->errorString());
    }
}

void DownloadWorker::onTimeout()
{
    if (this->m_state == DownloadState::Downloading)
    {
        this->fail(tr("The download timed out."));
    }
}

void DownloadWorker::processPacket(QByteArray const& _payload)
{
    // 1. Interpret the first payload as the fixed-width remote file-size header.
    if (this->m_expectedBytes < 0)
    {
        // Reject malformed headers before reading the qint64 size value.
        if (_payload.size() != static_cast<int>(sizeof(qint64)))
        {
            this->fail(tr("The download header is invalid."));
            return;
        }

        QDataStream stream{_payload};
        stream.setByteOrder(QDataStream::LittleEndian);
        stream >> this->m_expectedBytes;
        // A negative size reports that the server could not open the requested file.
        if (this->m_expectedBytes < 0)
        {
            this->fail(tr("The remote file cannot be read."));
            return;
        }
        // Complete an empty file immediately because no data packets will follow.
        if (this->m_expectedBytes == 0)
        {
            emit this->progress(this->m_remotePath, 0, 0);
            this->completeSuccessfully();
        }
        return;
    }

    // 2. Reject data that exceeds the file size declared by the server.
    if (this->m_receivedBytes + _payload.size() > this->m_expectedBytes)
    {
        this->fail(tr("Received more download data than expected."));
        return;
    }
    // 3. Persist the complete payload and reject unavailable or partial writes.
    if (!this->m_file || this->m_file->write(_payload) != _payload.size())
    {
        this->fail(tr("Failed to write the local file."));
        return;
    }

    this->m_receivedBytes += _payload.size();
    emit this->progress(this->m_remotePath, this->m_receivedBytes, this->m_expectedBytes);
    // 4. Commit the temporary file after all declared bytes have been received.
    if (this->m_receivedBytes == this->m_expectedBytes)
    {
        this->completeSuccessfully();
    }
}

void DownloadWorker::completeSuccessfully()
{
    if (!this->m_file || !this->m_file->commit())
    {
        QString const errorMessage{this->m_file ? this->m_file->errorString()
                                                : tr("The temporary file is unavailable.")};
        this->fail(tr("Failed to save the local file: %1").arg(errorMessage));
        return;
    }

    QString const remotePath{this->m_remotePath};
    QString const localPath{this->m_localPath};
    this->m_state = DownloadState::Idle;
    this->m_timeoutTimer->stop();
    this->m_file.reset();
    this->resetSocket();
    emit this->finished(remotePath, localPath, true, tr("Download completed."));
}

void DownloadWorker::fail(QString const& _message)
{
    if (this->m_state != DownloadState::Downloading)
    {
        return;
    }

    QString const remotePath{this->m_remotePath};
    QString const localPath{this->m_localPath};
    this->m_state = DownloadState::Idle;
    this->m_timeoutTimer->stop();
    if (this->m_file && this->m_file->isOpen())
    {
        this->m_file->cancelWriting();
    }
    this->m_file.reset();
    this->resetSocket();
    emit this->finished(remotePath, localPath, false, _message);
}

void DownloadWorker::resetSocket()
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
