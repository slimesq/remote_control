#include "RemoteSession.h"

#include "CommandService.h"

#include <QTcpSocket>

RemoteSession::RemoteSession(QTcpSocket* socket, CommandService* commandService, QObject* parent)
    : QObject(parent)
    , m_socket(socket)
    , m_commandService(commandService)
{
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &RemoteSession::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &RemoteSession::onDisconnected);
}

void RemoteSession::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    while (true) {
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

void RemoteSession::processPacket(const remoteqt::Packet& packet)
{
    if (m_handled) {
        return;
    }
    m_handled = true;
    const QList<remoteqt::Packet> responses = m_commandService->handle(packet);
    for (const remoteqt::Packet& response : responses) {
        m_socket->write(response.serialize());
    }
    m_socket->disconnectFromHost();
}
