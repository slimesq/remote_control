#include "server/RemoteServer.h"

#include "server/CommandService.h"
#include "server/ControlStreamThread.h"
#include "server/FileRequestPool.h"
#include "server/RemoteSession.h"
#include "server/WatchStreamThread.h"

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

namespace
{

constexpr int MinimumFileWorkerThreads{2};
constexpr int MaximumFileWorkerThreads{4};
constexpr int MaximumWatchConnections{4};
constexpr int MaximumControlConnections{4};

}  // namespace

RemoteServer::RemoteServer(QObject* _parent)
    : QObject{_parent},
      m_server{new QTcpServer{this}},
      m_commandService{new CommandService{this}},
      m_fileRequestPool{new FileRequestPool{
          qBound(MinimumFileWorkerThreads, QThread::idealThreadCount(), MaximumFileWorkerThreads),
          this}}
{
    connect(this->m_server, &QTcpServer::newConnection, this, &RemoteServer::onNewConnection);
    connect(QCoreApplication::instance(),
            &QCoreApplication::aboutToQuit,
            this,
            &RemoteServer::shutdownWorkers);
}

bool RemoteServer::start(quint16 _port)
{
    return this->m_server->listen(QHostAddress::Any, _port);
}

quint16 RemoteServer::listeningPort() const noexcept
{
    return this->m_server->serverPort();
}

CommandService* RemoteServer::commandService() const noexcept
{
    return this->m_commandService;
}

void RemoteServer::onNewConnection()
{
    while (this->m_server->hasPendingConnections())
    {
        auto* const socket{this->m_server->nextPendingConnection()};
        new RemoteSession{socket, this, this};
    }
}

void RemoteServer::startWatchStream(QTcpSocket* _socket)
{
    // Bound long-lived watch streams independently so they cannot consume all server threads.
    if (this->m_watchThreads.size() >= MaximumWatchConnections)
    {
        _socket->abort();
        _socket->deleteLater();
        return;
    }

    auto* const thread{new WatchStreamThread{_socket, this}};
    this->m_watchThreads.append(thread);
    QObject::connect(thread, &QThread::finished, this, [this, thread] {
        this->m_watchThreads.removeOne(thread);
        thread->deleteLater();
    });
    thread->start();
}

void RemoteServer::startControlStream(QTcpSocket* _socket)
{
    // Control channels have their own limit because each channel owns a persistent thread.
    if (this->m_controlThreads.size() >= MaximumControlConnections)
    {
        _socket->abort();
        _socket->deleteLater();
        return;
    }

    auto* const thread{new ControlStreamThread{_socket, this->m_commandService, this}};
    this->m_controlThreads.append(thread);
    QObject::connect(thread, &QThread::finished, this, [this, thread] {
        this->m_controlThreads.removeOne(thread);
        thread->deleteLater();
    });
    thread->start();
}

void RemoteServer::startFileRequest(QTcpSocket* _socket, remote_control::Packet _request)
{
    if (!this->m_fileRequestPool->submit(_socket, std::move(_request)))
    {
        _socket->abort();
        _socket->deleteLater();
    }
}

void RemoteServer::shutdownWorkers()
{
    QList<WatchStreamThread*> const watchThreads{this->m_watchThreads};
    QList<ControlStreamThread*> const controlThreads{this->m_controlThreads};

    for (WatchStreamThread* const thread : watchThreads)
    {
        thread->stop();
    }
    for (ControlStreamThread* const thread : controlThreads)
    {
        thread->stop();
    }

    this->m_fileRequestPool->shutdown();

    for (WatchStreamThread* const thread : watchThreads)
    {
        thread->wait();
    }
    for (ControlStreamThread* const thread : controlThreads)
    {
        thread->wait();
    }

    this->m_watchThreads.clear();
    this->m_controlThreads.clear();
}
