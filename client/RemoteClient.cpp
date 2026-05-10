#include "RemoteClient.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
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
                fail(tr("无法写入本地文件：%1").arg(m_localPath));
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
            deleteLater();
            return;
        }

        switch (m_command) {
        case remoteqt::Command::ListDirectory:
            emit m_client->directoryListed(m_context, m_entries);
            finish();
            break;
        case remoteqt::Command::DownloadFile:
            if (m_expectedDownloadBytes >= 0 && m_expectedDownloadBytes == m_receivedDownloadBytes) {
                emit m_client->downloadFinished(m_context, m_localPath, true, tr("下载完成"));
                finish();
            } else {
                fail(tr("下载被中断"));
            }
            break;
        default:
            fail(tr("连接已关闭，未收到完整响应"));
            break;
        }
        deleteLater();
    }

    void onErrorOccurred(QAbstractSocket::SocketError)
    {
        if (m_finished) {
            return;
        }
        fail(m_socket.errorString());
    }

private:
    void handlePacket(const remoteqt::Packet& packet)
    {
        if (packet.command != m_command) {
            fail(tr("收到意外命令：%1").arg(static_cast<int>(packet.command)));
            return;
        }

        switch (m_command) {
        case remoteqt::Command::TestConnection:
            emit m_client->connectionTested(true, tr("连接成功"));
            finish();
            break;
        case remoteqt::Command::ListDrives:
            emit m_client->drivesListed(remoteqt::decodeLocal8Bit(packet.payload).split(',', Qt::SkipEmptyParts));
            finish();
            break;
        case remoteqt::Command::ListDirectory:
        {
            const remoteqt::FileEntry entry = remoteqt::FileEntry::fromPayload(packet.payload);
            if (entry.isInvalid) {
                fail(tr("目录无法访问：%1").arg(m_context));
                return;
            }
            if (entry.hasNext) {
                m_entries.push_back(entry);
            }
            break;
        }
        case remoteqt::Command::RunFile:
        case remoteqt::Command::DeleteFile:
        case remoteqt::Command::LockMachine:
        case remoteqt::Command::UnlockMachine:
        case remoteqt::Command::MouseEvent:
        {
            QString message;
            const bool success = remoteqt::parseStatusPayload(packet.payload, true, &message);
            if (!success) {
                fail(message.isEmpty() ? tr("命令执行失败") : message);
                return;
            }
            emit m_client->commandCompleted(m_command, m_context, message.isEmpty() ? tr("命令执行完成") : message);
            finish();
            break;
        }
        case remoteqt::Command::DownloadFile:
            handleDownloadPacket(packet.payload);
            break;
        case remoteqt::Command::WatchScreen:
        {
            QImage image;
            if (!image.loadFromData(packet.payload, "PNG")) {
                fail(tr("远程截图解码失败"));
                return;
            }
            emit m_client->watchFrameReady(image);
            finish();
            break;
        }
        }
    }

    void handleDownloadPacket(const QByteArray& payload)
    {
        if (m_expectedDownloadBytes < 0) {
            if (payload.size() != static_cast<int>(sizeof(qint64))) {
                fail(tr("下载响应头格式错误"));
                return;
            }
            QDataStream stream(payload);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream >> m_expectedDownloadBytes;
            if (m_expectedDownloadBytes < 0) {
                fail(tr("远程文件无法读取"));
                return;
            }
            if (m_expectedDownloadBytes == 0) {
                if (m_file.isOpen()) {
                    m_file.close();
                }
                emit m_client->downloadProgress(m_context, 0, 0);
                emit m_client->downloadFinished(m_context, m_localPath, true, tr("下载完成"));
                finish();
            }
            return;
        }

        if (m_file.write(payload) != payload.size()) {
            fail(tr("写入本地文件失败"));
            return;
        }
        m_receivedDownloadBytes += payload.size();
        emit m_client->downloadProgress(m_context, m_receivedDownloadBytes, m_expectedDownloadBytes);
        if (m_receivedDownloadBytes >= m_expectedDownloadBytes) {
            m_file.close();
        }
    }

    void fail(const QString& message)
    {
        if (m_finished) {
            return;
        }
        if (m_file.isOpen()) {
            m_file.close();
        }
        emit m_client->requestFailed(m_command, m_context, message);
        if (m_command == remoteqt::Command::DownloadFile) {
            emit m_client->downloadFinished(m_context, m_localPath, false, message);
        } else if (m_command == remoteqt::Command::TestConnection) {
            emit m_client->connectionTested(false, message);
        }
        finish();
        m_socket.abort();
        deleteLater();
    }

    void finish()
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        if (m_command == remoteqt::Command::WatchScreen) {
            m_client->setProperty("watchPending", false);
        }
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
    QFile m_file;
    qint64 m_expectedDownloadBytes = -1;
    qint64 m_receivedDownloadBytes = 0;
    bool m_finished = false;
};

}

RemoteClient::RemoteClient(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<remoteqt::Command>();
    qRegisterMetaType<remoteqt::FileEntry>();
    qRegisterMetaType<QList<remoteqt::FileEntry>>();
    setProperty("watchPending", false);
}

void RemoteClient::setEndpoint(const QString& host, quint16 port)
{
    m_host = host;
    m_port = port;
}

void RemoteClient::testConnection()
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::TestConnection, {}, tr("连接测试"));
    request->start();
}

void RemoteClient::requestDrives()
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::ListDrives, {}, tr("磁盘列表"));
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
    if (property("watchPending").toBool()) {
        return;
    }
    setProperty("watchPending", true);
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::WatchScreen, {}, tr("远程监控"));
    request->start();
}

void RemoteClient::sendMouseEvent(const remoteqt::MouseEventPacket& event)
{
    QByteArray payload(reinterpret_cast<const char*>(&event), static_cast<int>(sizeof(event)));
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::MouseEvent, payload, tr("鼠标事件"));
    request->start();
}

void RemoteClient::lockRemote()
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::LockMachine, {}, tr("锁机"));
    request->start();
}

void RemoteClient::unlockRemote()
{
    auto* request = new PendingRequest(this, m_host, m_port, remoteqt::Command::UnlockMachine, {}, tr("解锁"));
    request->start();
}

#include "RemoteClient.moc"
