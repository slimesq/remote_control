#include "RemoteClient.h"

#include <QDataStream>
#include <QSaveFile>
#include <QTcpSocket>

namespace {

class PendingRequest final : public QObject
{
    Q_OBJECT

public:
    PendingRequest(RemoteClient* client,
        const QString& host,
        quint16 port,
        remoteqt::Command command,
        QByteArray payload,
        QString context,
        QString localPath = {})
        : QObject(client)
        , m_client(client)
        , m_host(host)
        , m_port(port)
        , m_command(command)
        , m_payload(std::move(payload))
        , m_context(std::move(context))
        , m_localPath(std::move(localPath))
    {
        m_socket.setParent(this);
        connect(&m_socket, &QTcpSocket::connected, this, &PendingRequest::onConnected);
        connect(&m_socket, &QTcpSocket::readyRead, this, &PendingRequest::onReadyRead);
        connect(&m_socket, &QTcpSocket::disconnected, this, &PendingRequest::onDisconnected);
        connect(&m_socket, &QTcpSocket::errorOccurred, this, &PendingRequest::onErrorOccurred);
    }

    void start()
    {
        if (m_command == remoteqt::Command::DownloadFile) {
            m_file.setFileName(m_localPath);
            if (!m_file.open(QIODevice::WriteOnly)) {
                fail(tr("Unable to write the local file: %1").arg(m_localPath));
                return;
            }
        }
        m_socket.connectToHost(m_host, m_port);
    }

private slots:
    void onConnected()
    {
        remoteqt::Packet packet(m_command, m_payload);
        m_socket.write(packet.serialize());
    }

    void onReadyRead()
    {
        m_buffer.append(m_socket.readAll());
        while (true) {
            const auto packet = remoteqt::Packet::tryParse(m_buffer);
            if (!packet.has_value()) {
                break;
            }
            handlePacket(packet.value());
            if (m_finished) {
                break;
            }
        }
    }

    void onDisconnected()
    {
        if (m_finished) {
            requestDeletion();
            return;
        }

        switch (m_command) {
        case remoteqt::Command::ListDirectory:
            fail(tr("Directory listing ended before the terminating packet was received."));
            break;
        case remoteqt::Command::DownloadFile:
            fail(tr("Download was interrupted."));
            break;
        default:
            fail(tr("Connection closed before a complete response was received."));
            break;
        }
    }

    void onErrorOccurred(QAbstractSocket::SocketError)
    {
        if (m_finished) {
            return;
        }
        fail(m_socket.errorString());
    }

private:
    class CallbackScope final
    {
    public:
        explicit CallbackScope(PendingRequest* request)
            : m_request(request)
        {
            ++m_request->m_callbackDepth;
        }

        ~CallbackScope()
        {
            // UI slots can spin nested event loops; delay deletion until we fully unwind.
            --m_request->m_callbackDepth;
            if (m_request->m_callbackDepth == 0 && m_request->m_cleanupPending) {
                m_request->m_cleanupPending = false;
                m_request->deleteLater();
            }
        }

    private:
        PendingRequest* m_request = nullptr;
    };

    void requestDeletion()
    {
        if (m_callbackDepth > 0) {
            m_cleanupPending = true;
            return;
        }
        deleteLater();
    }

    void markFinished()
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        if (m_command == remoteqt::Command::WatchScreen) {
            m_client->setWatchFramePending(false);
        }
    }

    void handlePacket(const remoteqt::Packet& packet)
    {
        CallbackScope scope(this);

        if (packet.command != m_command) {
            fail(tr("Received an unexpected command: %1").arg(static_cast<int>(packet.command)));
            return;
        }

        switch (m_command) {
        case remoteqt::Command::TestConnection:
            markFinished();
            emit m_client->connectionTested(true, tr("Connection succeeded."));
            m_socket.disconnectFromHost();
            break;
        case remoteqt::Command::ListDrives:
            markFinished();
            emit m_client->drivesListed(remoteqt::decodeLocal8Bit(packet.payload).split(',', Qt::SkipEmptyParts));
            m_socket.disconnectFromHost();
            break;
        case remoteqt::Command::ListDirectory:
            handleDirectoryPacket(packet.payload);
            break;
        case remoteqt::Command::RunFile:
        case remoteqt::Command::DeleteFile:
        case remoteqt::Command::LockMachine:
        case remoteqt::Command::UnlockMachine:
        case remoteqt::Command::MouseEvent:
            handleStatusPacket(packet.payload);
            break;
        case remoteqt::Command::DownloadFile:
            handleDownloadPacket(packet.payload);
            break;
        case remoteqt::Command::WatchScreen:
        {
            QImage image;
            if (!image.loadFromData(packet.payload, "PNG")) {
                fail(tr("Failed to decode the remote screenshot."));
                return;
            }
            emit m_client->watchFrameReady(image);
            finish();
            break;
        }
        }
    }

    void handleDirectoryPacket(const QByteArray& payload)
    {
        const remoteqt::FileEntry entry = remoteqt::FileEntry::fromPayload(payload);
        if (entry.isInvalid) {
            fail(tr("Directory is unavailable: %1").arg(m_context));
            return;
        }

        // The server streams entries one packet at a time and finishes with a marker packet.
        if (entry.hasNext) {
            m_entries.push_back(entry);
            return;
        }

        markFinished();
        emit m_client->directoryListed(m_context, m_entries);
        m_socket.disconnectFromHost();
    }

    void handleStatusPacket(const QByteArray& payload)
    {
        QString message;
        const bool success = remoteqt::parseStatusPayload(payload, true, &message);
        if (!success) {
            fail(message.isEmpty() ? tr("The command failed.") : message);
            return;
        }

        markFinished();
        emit m_client->commandCompleted(
            m_command,
            m_context,
            message.isEmpty() ? tr("The command completed successfully.") : message);
        m_socket.disconnectFromHost();
    }

    void handleDownloadPacket(const QByteArray& payload)
    {
        if (m_expectedDownloadBytes < 0) {
            // The first packet is a little-endian size header, not file data.
            if (payload.size() != static_cast<int>(sizeof(qint64))) {
                fail(tr("The download header is invalid."));
                return;
            }

            QDataStream stream(payload);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream >> m_expectedDownloadBytes;

            if (m_expectedDownloadBytes < 0) {
                fail(tr("The remote file cannot be read."));
                return;
            }

            if (m_expectedDownloadBytes == 0) {
                if (!commitDownloadFile()) {
                    return;
                }
                markFinished();
                emit m_client->downloadProgress(m_context, 0, 0);
                emit m_client->downloadFinished(m_context, m_localPath, true, tr("Download completed."));
                m_socket.disconnectFromHost();
            }
            return;
        }

        if (m_file.write(payload) != payload.size()) {
            fail(tr("Failed to write the local file."));
            return;
        }

        m_receivedDownloadBytes += payload.size();
        emit m_client->downloadProgress(m_context, m_receivedDownloadBytes, m_expectedDownloadBytes);

        if (m_receivedDownloadBytes > m_expectedDownloadBytes) {
            fail(tr("Received more download data than expected."));
            return;
        }

        if (m_receivedDownloadBytes == m_expectedDownloadBytes) {
            if (!commitDownloadFile()) {
                return;
            }
            markFinished();
            emit m_client->downloadFinished(m_context, m_localPath, true, tr("Download completed."));
            m_socket.disconnectFromHost();
        }
    }

    bool commitDownloadFile()
    {
        if (!m_file.commit()) {
            fail(tr("Failed to save the local file: %1").arg(m_file.errorString()));
            return false;
        }
        return true;
    }

    void fail(const QString& message)
    {
        if (m_finished) {
            return;
        }

        CallbackScope scope(this);

        if (m_file.isOpen()) {
            m_file.cancelWriting();
        }

        markFinished();
        emit m_client->requestFailed(m_command, m_context, message);
        if (m_command == remoteqt::Command::DownloadFile) {
            emit m_client->downloadFinished(m_context, m_localPath, false, message);
        } else if (m_command == remoteqt::Command::TestConnection) {
            emit m_client->connectionTested(false, message);
        }

        m_socket.abort();
        requestDeletion();
    }

    void finish()
    {
        if (m_finished) {
            return;
        }
        markFinished();
        m_socket.disconnectFromHost();
    }

    RemoteClient* m_client = nullptr;
    QString m_host;
    quint16 m_port = 0;
    remoteqt::Command m_command;
    QByteArray m_payload;
    QString m_context;
    QString m_localPath;
    QTcpSocket m_socket;
    QByteArray m_buffer;
    QList<remoteqt::FileEntry> m_entries;
    QSaveFile m_file;
    qint64 m_expectedDownloadBytes = -1;
    qint64 m_receivedDownloadBytes = 0;
    bool m_finished = false;
    int m_callbackDepth = 0;
    bool m_cleanupPending = false;
};

}

RemoteClient::RemoteClient(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<remoteqt::Command>();
    qRegisterMetaType<remoteqt::FileEntry>();
    qRegisterMetaType<QList<remoteqt::FileEntry>>();
}

void RemoteClient::setEndpoint(const QString& host, quint16 port)
{
    m_host = host;
    m_port = port;
}

void RemoteClient::testConnection()
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::TestConnection, {}, tr("Connection test"));
    request->start();
}

void RemoteClient::requestDrives()
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::ListDrives, {}, tr("Drive list"));
    request->start();
}

void RemoteClient::requestDirectory(const QString& path)
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::ListDirectory, remoteqt::encodeLocal8Bit(path), path);
    request->start();
}

void RemoteClient::runFile(const QString& path)
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::RunFile, remoteqt::encodeLocal8Bit(path), path);
    request->start();
}

void RemoteClient::deleteFile(const QString& path)
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::DeleteFile, remoteqt::encodeLocal8Bit(path), path);
    request->start();
}

void RemoteClient::downloadFile(const QString& remotePath, const QString& localPath)
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::DownloadFile, remoteqt::encodeLocal8Bit(remotePath), remotePath, localPath);
    request->start();
}

void RemoteClient::requestWatchFrame()
{
    if (hasPendingWatchFrame()) {
        return;
    }
    // Allow only one outstanding screenshot request so the monitor view does not pile up sockets.
    setWatchFramePending(true);
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::WatchScreen, {}, tr("Remote monitor"));
    request->start();
}

bool RemoteClient::hasPendingWatchFrame() const
{
    return m_watchPending;
}

void RemoteClient::setWatchFramePending(bool pending)
{
    m_watchPending = pending;
}

void RemoteClient::sendMouseEvent(const remoteqt::MouseEventPacket& event)
{
    QByteArray payload(reinterpret_cast<const char*>(&event), static_cast<int>(sizeof(event)));
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::MouseEvent, payload, tr("Mouse event"));
    request->start();
}

void RemoteClient::lockRemote()
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::LockMachine, {}, tr("Lock"));
    request->start();
}

void RemoteClient::unlockRemote()
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::UnlockMachine, {}, tr("Unlock"));
    request->start();
}

#include "RemoteClient.moc"
