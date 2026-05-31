#include "RemoteServer.h"

#include "CommandService.h"
#include "RemoteSession.h"

#include <QTcpServer>
#include <QTcpSocket>

RemoteServer::RemoteServer(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_commandService(new CommandService(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &RemoteServer::onNewConnection);
}

bool RemoteServer::start(quint16 port)
{
    return m_server->listen(QHostAddress::Any, port);
}

quint16 RemoteServer::listeningPort() const
{
    return m_server->serverPort();
}

CommandService* RemoteServer::commandService() const
{
    return m_commandService;
}

void RemoteServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        auto* socket = m_server->nextPendingConnection();
        new RemoteSession(socket, m_commandService, this);
    }
}
