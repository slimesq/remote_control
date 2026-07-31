#include "server/ControlStreamThread.h"

#include "server/CommandService.h"
#include "server/PlatformIntegration.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QPoint>
#include <QTcpSocket>
#include <QTimer>

#include <cstring>

namespace
{

constexpr int ControlIdleTimeoutMs{5 * 60 * 1000};
constexpr int MaximumControlBufferBytes{1024 * 1024};

/**
 * @brief Creates a common command-status packet.
 * @param _command Command associated with the status.
 * @param _success Whether the command succeeded.
 * @param _message User-facing result message.
 * @return Serialized command-status packet.
 */
remote_control::Packet statusPacket(remote_control::Command _command,
                                    bool _success,
                                    QString const& _message = {})
{
    return {_command, remote_control::makeStatusPayload(_success, _message)};
}

}  // namespace

ControlStreamThread::ControlStreamThread(QTcpSocket* _socket,
                                         CommandService* _commandService,
                                         QObject* _parent)
    : QThread{_parent}, m_socket{_socket}, m_commandService{_commandService}
{
    this->m_socket->moveToThread(this);
}

ControlStreamThread::~ControlStreamThread()
{
    this->stop();
    this->wait();
    delete this->m_socket;
}

void ControlStreamThread::stop()
{
    this->requestInterruption();
    if (!this->isRunning())
    {
        return;
    }

    QTcpSocket* const socket{this->m_socket};
    QMetaObject::invokeMethod(socket, [socket] { socket->abort(); }, Qt::QueuedConnection);
}

void ControlStreamThread::run()
{
    QEventLoop eventLoop;
    QTimer idleTimer;
    idleTimer.setSingleShot(true);
    idleTimer.setInterval(ControlIdleTimeoutMs);
    this->m_socket->setReadBufferSize(MaximumControlBufferBytes);

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

    // Acknowledge the ControlChannel packet that RemoteSession consumed before the handoff.
    QByteArray const handshake{
        statusPacket(remote_control::Command::ControlChannel, true, {}).serialize()};
    // Enter the persistent event loop only after a valid acknowledgement was queued and no stop
    // request arrived during channel handoff.
    if (!handshake.isEmpty() && this->m_socket->write(handshake) >= 0 &&
        !this->isInterruptionRequested())
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

bool ControlStreamThread::processAvailableRequests()
{
    this->m_buffer.append(this->m_socket->readAll());
    if (this->m_buffer.size() > MaximumControlBufferBytes)
    {
        return false;
    }

    while (!this->isInterruptionRequested())
    {
        auto const request{remote_control::Packet::tryParse(this->m_buffer)};
        if (!request.has_value())
        {
            return true;
        }

        remote_control::Packet const response{this->handleRequest(request.value())};
        QByteArray const bytes{response.serialize()};
        if (bytes.isEmpty() || this->m_socket->write(bytes) < 0)
        {
            return false;
        }
    }
    return false;
}

remote_control::Packet ControlStreamThread::handleRequest(
    remote_control::Packet const& _request) const
{
    if (_request.command == remote_control::Command::MouseEvent)
    {
        if (_request.payload.size() != static_cast<int>(sizeof(remote_control::MouseEventPacket)))
        {
            return statusPacket(_request.command, false, tr("Invalid mouse event payload."));
        }

        remote_control::MouseEventPacket event{};
        std::memcpy(&event, _request.payload.constData(), sizeof(event));
        bool const success{PlatformIntegration::sendGlobalMouseEvent(
            QPoint{event.x, event.y},
            static_cast<remote_control::MouseAction>(event.action),
            static_cast<remote_control::MouseButton>(event.button))};
        return statusPacket(
            _request.command, success, success ? QString{} : tr("Failed to send the mouse event."));
    }

    // Lock-state commands carry no payload; reject both unknown commands and malformed variants.
    if ((_request.command == remote_control::Command::LockMachine ||
         _request.command == remote_control::Command::UnlockMachine) &&
        _request.payload.isEmpty())
    {
        bool const lock{_request.command == remote_control::Command::LockMachine};
        bool const success{this->invokeLockOperation(lock)};
        QString const message{
            success ? (lock ? tr("Lock request accepted.") : tr("Unlock request accepted."))
                    : tr("Failed to queue the lock operation.")};
        return statusPacket(_request.command, success, message);
    }

    return statusPacket(_request.command, false, tr("Unsupported control-channel command."));
}

bool ControlStreamThread::invokeLockOperation(bool _lock) const
{
    CommandService* const service{this->m_commandService};
    return QMetaObject::invokeMethod(
        service,
        [service, _lock] {
            if (_lock)
            {
                service->lockLocalMachine();
            }
            else
            {
                service->unlockLocalMachine();
            }
        },
        Qt::QueuedConnection);
}
