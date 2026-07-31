#include "server/RemoteSession.h"

#include "server/CommandService.h"
#include "server/RemoteServer.h"

#include <QTcpSocket>
#include <QTimer>

namespace
{

constexpr int IdleTimeoutMs{15000};
constexpr int MaxIncomingBufferBytes{1024 * 1024};

}  // namespace

RemoteSession::RemoteSession(QTcpSocket* _socket, RemoteServer* _server, QObject* _parent)
    : QObject{_parent},
      m_socket{_socket},
      m_server{_server},
      m_commandService{_server->commandService()},
      m_idleTimer{new QTimer{this}}
{
    this->m_socket->setParent(this);
    this->m_socket->setReadBufferSize(MaxIncomingBufferBytes);
    connect(this->m_socket, &QTcpSocket::readyRead, this, &RemoteSession::onReadyRead);
    connect(this->m_socket, &QTcpSocket::disconnected, this, &RemoteSession::onDisconnected);
    this->m_idleTimer->setSingleShot(true);
    this->m_idleTimer->setInterval(IdleTimeoutMs);
    connect(this->m_idleTimer, &QTimer::timeout, this, &RemoteSession::onIdleTimeout);
    this->restartIdleTimer();
}

void RemoteSession::onReadyRead()
{
    // 1. Refresh the deadline and retain bytes from partial TCP frames.
    this->restartIdleTimer();
    this->m_buffer.append(this->m_socket->readAll());
    // 2. Reject peers that exceed the bounded receive buffer.
    if (this->m_buffer.size() > MaxIncomingBufferBytes)
    {
        this->m_socket->abort();
        return;
    }
    // 3. Parse at most one complete request for this short-lived session.
    while (true)
    {
        auto const packet{remote_control::Packet::tryParse(this->m_buffer)};
        if (!packet.has_value())
        {
            break;
        }
        this->processPacket(packet.value());
        if (this->m_handled)
        {
            break;
        }
    }
}

void RemoteSession::onDisconnected()
{
    deleteLater();
}

void RemoteSession::onIdleTimeout()
{
    // Ownership may already have moved to a stream or file worker; only this session's unhandled
    // socket is eligible for the short-request timeout.
    if (this->m_handled)
    {
        return;
    }
    this->m_socket->abort();
}

void RemoteSession::processPacket(remote_control::Packet const& _packet)
{
    // Each accepted socket is classified once; later buffered packets belong to the new owner.
    if (this->m_handled)
    {
        return;
    }
    if (_packet.command == remote_control::Command::WatchScreen)
    {
        if (!_packet.payload.isEmpty())
        {
            this->m_handled = true;
            this->m_socket->abort();
            return;
        }
        this->m_server->startWatchStream(this->takeSocket());
        return;
    }

    if (_packet.command == remote_control::Command::ControlChannel)
    {
        if (!_packet.payload.isEmpty())
        {
            this->m_handled = true;
            this->m_socket->abort();
            return;
        }
        this->m_server->startControlStream(this->takeSocket());
        return;
    }

    // Potentially blocking file operations leave the session thread and share the bounded pool.
    if (_packet.command == remote_control::Command::ListDirectory ||
        _packet.command == remote_control::Command::DownloadFile ||
        _packet.command == remote_control::Command::DeleteFile)
    {
        this->m_server->startFileRequest(this->takeSocket(), _packet);
        return;
    }

    // 1. Prevent re-entry before invoking the synchronous command handler.
    this->m_handled = true;
    // 2. Queue every response packet in command-defined order.
    QList<remote_control::Packet> const responses{this->m_commandService->handle(_packet)};
    for (remote_control::Packet const& response : responses)
    {
        this->m_socket->write(response.serialize());
    }
    // 3. Close the short-lived connection after all responses are queued.
    this->m_socket->disconnectFromHost();
}

QTcpSocket* RemoteSession::takeSocket()
{
    // Transfer ownership only after callbacks and the session timeout have been disabled.
    this->m_handled = true;
    this->m_idleTimer->stop();
    QObject::disconnect(this->m_socket, nullptr, this, nullptr);
    this->m_socket->setParent(nullptr);
    QTcpSocket* const socket{this->m_socket};
    this->m_socket = nullptr;
    this->deleteLater();
    return socket;
}

void RemoteSession::restartIdleTimer()
{
    if (!this->m_handled)
    {
        this->m_idleTimer->start();
    }
}
