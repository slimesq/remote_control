#include "RemoteServer.h"

#include "CommandService.h"
#include "RemoteSession.h"

#include <QTcpServer>
#include <QTcpSocket>

RemoteServer::RemoteServer(QObject* _parent)
    : QObject(_parent)
    , m_server(new QTcpServer(this))
    , m_commandService(new CommandService(this))
{
    connect(this->m_server, &QTcpServer::newConnection, this, &RemoteServer::onNewConnection);
}

bool RemoteServer::start(quint16 _port)
{
    return this->m_server->listen(QHostAddress::Any, _port);
}

quint16 RemoteServer::listeningPort() const
{
    return this->m_server->serverPort();
}

CommandService* RemoteServer::commandService() const
{
    return this->m_commandService;
}

void RemoteServer::onNewConnection()
{
    while (this->m_server->hasPendingConnections()) {
        auto* socket = this->m_server->nextPendingConnection();
        new RemoteSession(socket, this->m_commandService, this);
    }
}
