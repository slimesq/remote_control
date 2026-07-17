#include "server/RemoteSession.h"

#include "server/CommandService.h"

#include <QTcpSocket>
#include <QTimer>

namespace
{

constexpr int IdleTimeoutMs{15000};
constexpr int MaxIncomingBufferBytes{1024 * 1024};

}  // namespace

RemoteSession::RemoteSession(QTcpSocket* _socket, CommandService* _commandService, QObject* _parent)
    : QObject{_parent},
      m_socket{_socket},
      m_commandService{_commandService},
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
    this->restartIdleTimer();
    this->m_buffer.append(this->m_socket->readAll());
    if (this->m_buffer.size() > MaxIncomingBufferBytes)
    {
        this->m_socket->abort();
        return;
    }
    while (true)
    {
        // Each TCP session handles at most one fully parsed request packet.
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
    if (this->m_handled)
    {
        return;
    }
    this->m_socket->abort();
}

void RemoteSession::processPacket(remote_control::Packet const& _packet)
{
    if (this->m_handled)
    {
        return;
    }
    this->m_handled = true;
    // Commands are synchronous here: collect all response packets, write them,
    // then close the socket.
    QList<remote_control::Packet> const responses{this->m_commandService->handle(_packet)};
    for (remote_control::Packet const& response : responses)
    {
        this->m_socket->write(response.serialize());
    }
    this->m_socket->disconnectFromHost();
}

void RemoteSession::restartIdleTimer()
{
    if (!this->m_handled)
    {
        this->m_idleTimer->start();
    }
}
