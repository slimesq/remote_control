#include "server/RemoteControlServer.h"

#include "server/ScreenLockService.h"
#include "server/RemoteControlTransport.h"

#include <QCoreApplication>

RemoteControlServer::RemoteControlServer(QObject* _parent)
    : QObject{_parent},
      m_screenLockService{new ScreenLockService{this}},
      m_transport{std::make_unique<RemoteControlTransport>(this->m_screenLockService)}
{
    connect(QCoreApplication::instance(),
            &QCoreApplication::aboutToQuit,
            this,
            &RemoteControlServer::shutdownTransport);
}

RemoteControlServer::~RemoteControlServer()
{
    this->shutdownTransport();
}

bool RemoteControlServer::start(quint16 _port)
{
    return this->m_transport->start(_port);
}

quint16 RemoteControlServer::listeningPort() const noexcept
{
    return this->m_transport->listeningPort();
}

ScreenLockService* RemoteControlServer::screenLockService() const noexcept
{
    return this->m_screenLockService;
}

void RemoteControlServer::shutdownTransport()
{
    if (this->m_transport)
    {
        this->m_transport->stop();
    }
}
