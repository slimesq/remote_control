#include "RemoteClient.h"

#include <QDataStream>
#include <QSaveFile>
#include <QTcpSocket>

namespace {

class PendingRequest final : public QObject
{
    Q_OBJECT

public:
    PendingRequest(RemoteClient* _client,
        const QString& _host,
        quint16 _port,
        remote_control::Command _command,
        QByteArray _payload,
        QString _context,
        QString _localPath = {})
        : QObject(_client)
        , m_client(_client)
        , m_host(_host)
        , m_port(_port)
        , m_command(_command)
        , m_payload(std::move(_payload))
        , m_context(std::move(_context))
        , m_localPath(std::move(_localPath))
    {
        this->m_socket.setParent(this);
        connect(&this->m_socket, &QTcpSocket::connected, this, &PendingRequest::onConnected);
        connect(&this->m_socket, &QTcpSocket::readyRead, this, &PendingRequest::onReadyRead);
        connect(&this->m_socket, &QTcpSocket::disconnected, this, &PendingRequest::onDisconnected);
        connect(&this->m_socket, &QTcpSocket::errorOccurred, this, &PendingRequest::onErrorOccurred);
    }

    void start()
    {
        if (this->m_command == remote_control::Command::DownloadFile) {
            this->m_file.setFileName(this->m_localPath);
            if (!this->m_file.open(QIODevice::WriteOnly)) {
                this->fail(tr("Unable to write the local file: %1").arg(this->m_localPath));
                return;
            }
        }
        this->m_socket.connectToHost(this->m_host, this->m_port);
    }

private slots:
    void onConnected()
    {
        remote_control::Packet packet(this->m_command, this->m_payload);
        this->m_socket.write(packet.serialize());
    }

    void onReadyRead()
    {
        this->m_buffer.append(this->m_socket.readAll());
        while (true) {
            const auto packet = remote_control::Packet::tryParse(this->m_buffer);
            if (!packet.has_value()) {
                break;
            }
            this->handlePacket(packet.value());
            if (this->m_finished) {
                break;
            }
        }
        if (!this->m_finished && this->m_buffer.size() > remote_control::Packet::MaximumSerializedSize) {
            this->fail(tr("The remote response exceeds the maximum packet size."));
        }
    }

    void onDisconnected()
    {
        if (this->m_finished) {
            this->requestDeletion();
            return;
        }

        switch (this->m_command) {
        case remote_control::Command::ListDirectory:
            this->fail(tr("Directory listing ended before the terminating packet was received."));
            break;
        case remote_control::Command::DownloadFile:
            this->fail(tr("Download was interrupted."));
            break;
        default:
            this->fail(tr("Connection closed before a complete response was received."));
            break;
        }
    }

    void onErrorOccurred(QAbstractSocket::SocketError)
    {
        if (this->m_finished) {
            return;
        }
        this->fail(this->m_socket.errorString());
    }

private:
    class CallbackScope final
    {
    public:
        explicit CallbackScope(PendingRequest* _request)
            : m_request(_request)
        {
            ++this->m_request->m_callbackDepth;
        }

        ~CallbackScope()
        {
            // UI slots can spin nested event loops; delay deletion until we fully unwind.
            --this->m_request->m_callbackDepth;
            if (this->m_request->m_callbackDepth == 0 && this->m_request->m_cleanupPending) {
                this->m_request->m_cleanupPending = false;
                this->m_request->deleteLater();
            }
        }

    private:
        PendingRequest* m_request = nullptr;
    };

    void requestDeletion()
    {
        if (this->m_callbackDepth > 0) {
            this->m_cleanupPending = true;
            return;
        }
        deleteLater();
    }

    void markFinished()
    {
        if (this->m_finished) {
            return;
        }
        this->m_finished = true;
        if (this->m_command == remote_control::Command::WatchScreen) {
            this->m_client->setWatchFramePending(false);
        }
    }

    void handlePacket(const remote_control::Packet& _packet)
    {
        CallbackScope scope(this);

        if (_packet.command != this->m_command) {
            this->fail(tr("Received an unexpected command: %1").arg(static_cast<int>(_packet.command)));
            return;
        }

        switch (this->m_command) {
        case remote_control::Command::TestConnection:
            this->markFinished();
            emit this->m_client->connectionTested(true, tr("Connection succeeded."));
            this->m_socket.disconnectFromHost();
            break;
        case remote_control::Command::ListDrives:
            this->markFinished();
            emit this->m_client->drivesListed(remote_control::decodeUtf8(_packet.payload).split(',', Qt::SkipEmptyParts));
            this->m_socket.disconnectFromHost();
            break;
        case remote_control::Command::ListDirectory:
            this->handleDirectoryPacket(_packet.payload);
            break;
        case remote_control::Command::RunFile:
        case remote_control::Command::DeleteFile:
        case remote_control::Command::LockMachine:
        case remote_control::Command::UnlockMachine:
        case remote_control::Command::MouseEvent:
            this->handleStatusPacket(_packet.payload);
            break;
        case remote_control::Command::DownloadFile:
            this->handleDownloadPacket(_packet.payload);
            break;
        case remote_control::Command::WatchScreen:
        {
            QImage image;
            if (!image.loadFromData(_packet.payload, "PNG")) {
                this->fail(tr("Failed to decode the remote screenshot."));
                return;
            }
            emit this->m_client->watchFrameReady(image);
            this->finish();
            break;
        }
        }
    }

    void handleDirectoryPacket(const QByteArray& _payload)
    {
        const remote_control::FileEntry entry = remote_control::FileEntry::fromPayload(_payload);
        if (entry.isInvalid) {
            this->fail(tr("Directory is unavailable: %1").arg(this->m_context));
            return;
        }

        // The server streams entries one packet at a time and finishes with a marker packet.
        if (entry.hasNext) {
            this->m_entries.push_back(entry);
            return;
        }

        this->markFinished();
        emit this->m_client->directoryListed(this->m_context, this->m_entries);
        this->m_socket.disconnectFromHost();
    }

    void handleStatusPacket(const QByteArray& _payload)
    {
        QString message;
        const bool success = remote_control::parseStatusPayload(_payload, true, &message);
        if (!success) {
            this->fail(message.isEmpty() ? tr("The command failed.") : message);
            return;
        }

        this->markFinished();
        emit this->m_client->commandCompleted(
            this->m_command,
            this->m_context,
            message.isEmpty() ? tr("The command completed successfully.") : message);
        this->m_socket.disconnectFromHost();
    }

    void handleDownloadPacket(const QByteArray& _payload)
    {
        if (this->m_expectedDownloadBytes < 0) {
            // The first packet is a little-endian size header, not file data.
            if (_payload.size() != static_cast<int>(sizeof(qint64))) {
                this->fail(tr("The download header is invalid."));
                return;
            }

            QDataStream stream(_payload);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream >> this->m_expectedDownloadBytes;

            if (this->m_expectedDownloadBytes < 0) {
                this->fail(tr("The remote file cannot be read."));
                return;
            }

            if (this->m_expectedDownloadBytes == 0) {
                if (!this->commitDownloadFile()) {
                    return;
                }
                this->markFinished();
                emit this->m_client->downloadProgress(this->m_context, 0, 0);
                emit this->m_client->downloadFinished(this->m_context, this->m_localPath, true, tr("Download completed."));
                this->m_socket.disconnectFromHost();
            }
            return;
        }

        if (this->m_file.write(_payload) != _payload.size()) {
            this->fail(tr("Failed to write the local file."));
            return;
        }

        this->m_receivedDownloadBytes += _payload.size();
        emit this->m_client->downloadProgress(this->m_context, this->m_receivedDownloadBytes, this->m_expectedDownloadBytes);

        if (this->m_receivedDownloadBytes > this->m_expectedDownloadBytes) {
            this->fail(tr("Received more download data than expected."));
            return;
        }

        if (this->m_receivedDownloadBytes == this->m_expectedDownloadBytes) {
            if (!this->commitDownloadFile()) {
                return;
            }
            this->markFinished();
            emit this->m_client->downloadFinished(this->m_context, this->m_localPath, true, tr("Download completed."));
            this->m_socket.disconnectFromHost();
        }
    }

    bool commitDownloadFile()
    {
        if (!this->m_file.commit()) {
            this->fail(tr("Failed to save the local file: %1").arg(this->m_file.errorString()));
            return false;
        }
        return true;
    }

    void fail(const QString& _message)
    {
        if (this->m_finished) {
            return;
        }

        CallbackScope scope(this);

        if (this->m_file.isOpen()) {
            this->m_file.cancelWriting();
        }

        this->markFinished();
        emit this->m_client->requestFailed(this->m_command, this->m_context, _message);
        if (this->m_command == remote_control::Command::DownloadFile) {
            emit this->m_client->downloadFinished(this->m_context, this->m_localPath, false, _message);
        } else if (this->m_command == remote_control::Command::TestConnection) {
            emit this->m_client->connectionTested(false, _message);
        }

        this->m_socket.abort();
        this->requestDeletion();
    }

    void finish()
    {
        if (this->m_finished) {
            return;
        }
        this->markFinished();
        this->m_socket.disconnectFromHost();
    }

    RemoteClient* m_client = nullptr;
    QString m_host;
    quint16 m_port = 0;
    remote_control::Command m_command;
    QByteArray m_payload;
    QString m_context;
    QString m_localPath;
    QTcpSocket m_socket;
    QByteArray m_buffer;
    QList<remote_control::FileEntry> m_entries;
    QSaveFile m_file;
    qint64 m_expectedDownloadBytes = -1;
    qint64 m_receivedDownloadBytes = 0;
    bool m_finished = false;
    int m_callbackDepth = 0;
    bool m_cleanupPending = false;
};

}

RemoteClient::RemoteClient(QObject* _parent)
    : QObject(_parent)
{
    qRegisterMetaType<remote_control::Command>();
    qRegisterMetaType<remote_control::FileEntry>();
    qRegisterMetaType<QList<remote_control::FileEntry>>();
}

void RemoteClient::setEndpoint(const QString& _host, quint16 _port)
{
    this->m_host = _host;
    this->m_port = _port;
}

void RemoteClient::testConnection()
{
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::TestConnection, {}, tr("Connection test"));
    request->start();
}

void RemoteClient::requestDrives()
{
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::ListDrives, {}, tr("Drive list"));
    request->start();
}

void RemoteClient::requestDirectory(const QString& _path)
{
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::ListDirectory, remote_control::encodeUtf8(_path), _path);
    request->start();
}

void RemoteClient::runFile(const QString& _path)
{
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::RunFile, remote_control::encodeUtf8(_path), _path);
    request->start();
}

void RemoteClient::deleteFile(const QString& _path)
{
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::DeleteFile, remote_control::encodeUtf8(_path), _path);
    request->start();
}

void RemoteClient::downloadFile(const QString& _remotePath, const QString& _localPath)
{
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::DownloadFile, remote_control::encodeUtf8(_remotePath), _remotePath, _localPath);
    request->start();
}

void RemoteClient::requestWatchFrame()
{
    if (this->hasPendingWatchFrame()) {
        return;
    }
    // Allow only one outstanding screenshot request so the monitor view does not pile up sockets.
    this->setWatchFramePending(true);
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::WatchScreen, {}, tr("Remote monitor"));
    request->start();
}

bool RemoteClient::hasPendingWatchFrame() const
{
    return this->m_watchPending;
}

void RemoteClient::setWatchFramePending(bool _pending)
{
    this->m_watchPending = _pending;
}

void RemoteClient::sendMouseEvent(const remote_control::MouseEventPacket& _event)
{
    QByteArray payload(reinterpret_cast<const char*>(&_event), static_cast<int>(sizeof(_event)));
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::MouseEvent, payload, tr("Mouse event"));
    request->start();
}

void RemoteClient::lockRemote()
{
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::LockMachine, {}, tr("Lock"));
    request->start();
}

void RemoteClient::unlockRemote()
{
    auto* request = new PendingRequest(this, this->m_host, this->m_port, remote_control::Command::UnlockMachine, {}, tr("Unlock"));
    request->start();
}

#include "RemoteClient.moc"
