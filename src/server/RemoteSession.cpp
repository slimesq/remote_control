#include "RemoteSession.h"

#include "CommandService.h"

#include <QTcpSocket>
#include <QTimer>

namespace {

constexpr int IdleTimeoutMs = 15000;
constexpr int MaxIncomingBufferBytes = 1024 * 1024;

}

RemoteSession::RemoteSession(QTcpSocket* socket, CommandService* commandService, QObject* parent)
    : QObject(parent)
    , m_socket(socket)
    , m_commandService(commandService)
    , m_idleTimer(new QTimer(this))
{
    m_socket->setParent(this);
    m_socket->setReadBufferSize(MaxIncomingBufferBytes);
    connect(m_socket, &QTcpSocket::readyRead, this, &RemoteSession::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &RemoteSession::onDisconnected);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(IdleTimeoutMs);
    connect(m_idleTimer, &QTimer::timeout, this, &RemoteSession::onIdleTimeout);
    restartIdleTimer();
}

void RemoteSession::onReadyRead()
{
    restartIdleTimer();
    m_buffer.append(m_socket->readAll());
    if (m_buffer.size() > MaxIncomingBufferBytes) {
        m_socket->abort();
        return;
    }
    while (true) {
        // Each TCP session handles at most one fully parsed request packet.
        const auto packet = remoteqt::Packet::tryParse(m_buffer);
        if (!packet.has_value()) {
            break;
        }
        processPacket(packet.value());
        if (m_handled) {
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
    if (m_handled) {
        return;
    }
    m_socket->abort();
}

void RemoteSession::processPacket(const remoteqt::Packet& packet)
{
    if (m_handled) {
        return;
    }
    m_handled = true;
    // Commands are synchronous here: collect all response packets, write them, then close the socket.
    const QList<remoteqt::Packet> responses = m_commandService->handle(packet);
    for (const remoteqt::Packet& response : responses) {
        m_socket->write(response.serialize());
    }
    m_socket->disconnectFromHost();
}

void RemoteSession::restartIdleTimer()
{
    if (!m_handled) {
        m_idleTimer->start();
    }
}
