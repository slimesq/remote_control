#include "server/WatchStreamThread.h"

#include "common/Packet.h"
#include "server/PlatformIntegration.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QMetaObject>
#include <QTcpSocket>
#include <QTimer>

namespace
{

constexpr int WatchIdleTimeoutMs{30000};
constexpr int MaximumWatchRequestBufferBytes{1024 * 1024};

}  // namespace

WatchStreamThread::WatchStreamThread(QTcpSocket* _socket, QObject* _parent)
    : QThread{_parent}, m_socket{_socket}
{
    this->m_socket->moveToThread(this);
}

WatchStreamThread::~WatchStreamThread()
{
    this->stop();
    this->wait();
    delete this->m_socket;
}

void WatchStreamThread::stop()
{
    this->requestInterruption();
    if (!this->isRunning())
    {
        return;
    }

    QTcpSocket* const socket{this->m_socket};
    QMetaObject::invokeMethod(socket, [socket] { socket->abort(); }, Qt::QueuedConnection);
}

void WatchStreamThread::run()
{
    QEventLoop eventLoop;
    QTimer idleTimer;
    idleTimer.setSingleShot(true);
    idleTimer.setInterval(WatchIdleTimeoutMs);
    this->m_socket->setReadBufferSize(MaximumWatchRequestBufferBytes);

    QObject::connect(
        this->m_socket, &QTcpSocket::readyRead, &eventLoop, [this, &eventLoop, &idleTimer] {
            idleTimer.start();
            if (!this->processAvailableRequests())
            {
                eventLoop.quit();
            }
        });
    QObject::connect(this->m_socket, &QTcpSocket::disconnected, &eventLoop, &QEventLoop::quit);
    QObject::connect(&idleTimer, &QTimer::timeout, this->m_socket, &QTcpSocket::abort);

    // The first WatchScreen request was parsed before the socket entered this thread.
    if (this->sendFrame() && !this->isInterruptionRequested())
    {
        idleTimer.start();
        eventLoop.exec();
    }

    QObject::disconnect(this->m_socket, nullptr, &eventLoop, nullptr);
    this->m_socket->abort();
    if (QCoreApplication::instance())
    {
        this->m_socket->moveToThread(QCoreApplication::instance()->thread());
    }
}

bool WatchStreamThread::processAvailableRequests()
{
    this->m_buffer.append(this->m_socket->readAll());
    if (this->m_buffer.size() > MaximumWatchRequestBufferBytes)
    {
        return false;
    }

    while (!this->isInterruptionRequested())
    {
        auto const packet{remote_control::Packet::tryParse(this->m_buffer)};
        if (!packet.has_value())
        {
            return true;
        }
        // A persistent watch connection accepts only empty frame requests and remains usable only
        // while the corresponding frame can be produced and queued.
        if (packet->command != remote_control::Command::WatchScreen || !packet->payload.isEmpty() ||
            !this->sendFrame())
        {
            return false;
        }
    }
    return false;
}

bool WatchStreamThread::sendFrame()
{
    if (this->isInterruptionRequested())
    {
        return false;
    }

    QImage const image{PlatformIntegration::capturePrimaryScreen()};
    QByteArray payload;
    QBuffer buffer{&payload};
    // Any capture, buffer, or encoding failure is represented by an empty frame payload.
    if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    {
        payload.clear();
    }

    remote_control::Packet const response{remote_control::Command::WatchScreen, payload};
    QByteArray const bytes{response.serialize()};
    return !bytes.isEmpty() && this->m_socket->write(bytes) >= 0;
}
