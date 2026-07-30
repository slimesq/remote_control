#include "client/ControlConnectionWorker.h"

#include "common/Packet.h"

#include <QTcpSocket>
#include <QTimer>

#include <cstring>

namespace
{

constexpr int MaximumQueuedControlCommands{128};
constexpr int ControlResponseTimeoutMs{15000};

}  // namespace

ControlConnectionWorker::ControlConnectionWorker() : m_timeoutTimer{new QTimer{this}}
{
    this->m_timeoutTimer->setSingleShot(true);
    this->m_timeoutTimer->setInterval(ControlResponseTimeoutMs);
    connect(this->m_timeoutTimer, &QTimer::timeout, this, &ControlConnectionWorker::onTimeout);
}

ControlConnectionWorker::~ControlConnectionWorker()
{
    this->resetSocket();
}

void ControlConnectionWorker::sendMouseEvent(QString const& _host,
                                             quint16 _port,
                                             remote_control::MouseEventPacket const& _event,
                                             quint64 _generation)
{
    QByteArray const payload{reinterpret_cast<char const*>(&_event),
                             static_cast<int>(sizeof(_event))};
    this->enqueue(_host,
                  _port,
                  {remote_control::Command::MouseEvent, payload, tr("Mouse event"), _generation});
}

void ControlConnectionWorker::sendCommand(QString const& _host,
                                          quint16 _port,
                                          remote_control::Command _command,
                                          QString const& _context,
                                          quint64 _generation)
{
    if (_command != remote_control::Command::LockMachine &&
        _command != remote_control::Command::UnlockMachine)
    {
        emit this->commandFailed(
            _generation, _command, _context, tr("Unsupported control command."));
        return;
    }
    this->enqueue(_host, _port, {_command, {}, _context, _generation});
}

void ControlConnectionWorker::closeConnection()
{
    this->m_queue.clear();
    this->m_activeCommand.reset();
    this->resetSocket();
}

void ControlConnectionWorker::shutdown()
{
    this->m_state = ConnectionState::ShuttingDown;
    this->closeConnection();
}

void ControlConnectionWorker::ensureSocket()
{
    if (this->m_socket)
    {
        return;
    }

    this->m_socket = new QTcpSocket{this};
    QObject::connect(
        this->m_socket, &QTcpSocket::connected, this, &ControlConnectionWorker::onConnected);
    QObject::connect(
        this->m_socket, &QTcpSocket::readyRead, this, &ControlConnectionWorker::onReadyRead);
    QObject::connect(
        this->m_socket, &QTcpSocket::disconnected, this, &ControlConnectionWorker::onDisconnected);
    QObject::connect(this->m_socket,
                     &QTcpSocket::errorOccurred,
                     this,
                     &ControlConnectionWorker::onErrorOccurred);
}

void ControlConnectionWorker::sendHandshake()
{
    remote_control::Packet const handshake{remote_control::Command::ControlChannel};
    QByteArray const bytes{handshake.serialize()};
    if (bytes.isEmpty() || this->m_socket->write(bytes) < 0)
    {
        this->failAll(tr("Failed to open the remote control channel."));
        return;
    }
    this->m_timeoutTimer->start();
}

void ControlConnectionWorker::sendNext()
{
    if (this->m_state != ConnectionState::Ready || this->m_activeCommand.has_value() ||
        this->m_queue.isEmpty())
    {
        if (this->m_state == ConnectionState::Ready && !this->m_activeCommand.has_value() &&
            this->m_queue.isEmpty())
        {
            this->m_timeoutTimer->stop();
        }
        return;
    }

    this->m_activeCommand = this->m_queue.dequeue();
    remote_control::Packet const request{this->m_activeCommand->command,
                                         this->m_activeCommand->payload};
    QByteArray const bytes{request.serialize()};
    if (bytes.isEmpty() || this->m_socket->write(bytes) < 0)
    {
        this->failAll(tr("Failed to send the remote control command."));
        return;
    }
    this->m_timeoutTimer->start();
}

void ControlConnectionWorker::enqueue(QString const& _host, quint16 _port, PendingCommand _command)
{
    if (this->m_state == ConnectionState::ShuttingDown)
    {
        return;
    }

    QString const host{_host.trimmed()};
    if (host.isEmpty() || _port == 0)
    {
        emit this->commandFailed(_command.generation,
                                 _command.command,
                                 _command.context,
                                 tr("The remote control endpoint is invalid."));
        return;
    }

    if (this->m_host != host || this->m_port != _port)
    {
        this->closeConnection();
        this->m_host = host;
        this->m_port = _port;
    }

    if (isMoveOnly(_command) && !this->m_queue.isEmpty() && isMoveOnly(this->m_queue.back()))
    {
        this->m_queue.back() = std::move(_command);
    }
    else if (this->m_queue.size() < MaximumQueuedControlCommands)
    {
        this->m_queue.enqueue(_command);
    }
    else
    {
        emit this->commandFailed(_command.generation,
                                 _command.command,
                                 _command.context,
                                 tr("The remote control queue is full."));
        return;
    }

    this->ensureSocket();
    if (this->m_socket->state() == QAbstractSocket::ConnectedState)
    {
        this->sendNext();
    }
    else if (this->m_socket->state() == QAbstractSocket::UnconnectedState)
    {
        this->m_state = ConnectionState::Connecting;
        this->m_socket->connectToHost(this->m_host, this->m_port);
        this->m_timeoutTimer->start();
    }
}

void ControlConnectionWorker::onConnected()
{
    this->m_state = ConnectionState::Handshaking;
    this->sendHandshake();
}

void ControlConnectionWorker::onReadyRead()
{
    this->m_buffer.append(this->m_socket->readAll());
    if (this->m_buffer.size() > remote_control::Packet::MaximumSerializedSize)
    {
        this->failAll(tr("The remote control response exceeds the packet limit."));
        return;
    }

    while (true)
    {
        auto const response{remote_control::Packet::tryParse(this->m_buffer)};
        if (!response.has_value())
        {
            return;
        }

        QString message;
        bool const success{remote_control::parseStatusPayload(response->payload, &message)};
        if (this->m_state == ConnectionState::Handshaking)
        {
            if (response->command != remote_control::Command::ControlChannel || !success)
            {
                this->failAll(message.isEmpty() ? tr("The control-channel handshake failed.")
                                                : message);
                return;
            }
            this->m_state = ConnectionState::Ready;
            this->sendNext();
            continue;
        }

        if (this->m_state != ConnectionState::Ready || !this->m_activeCommand.has_value() ||
            response->command != this->m_activeCommand->command)
        {
            this->failAll(tr("The remote control channel returned an unexpected response."));
            return;
        }

        PendingCommand const completed{std::move(this->m_activeCommand.value())};
        this->m_activeCommand.reset();
        if (success)
        {
            emit this->commandCompleted(
                completed.generation,
                completed.command,
                completed.context,
                message.isEmpty() ? tr("The command completed successfully.") : message);
        }
        else
        {
            emit this->commandFailed(completed.generation,
                                     completed.command,
                                     completed.context,
                                     message.isEmpty() ? tr("The command failed.") : message);
        }
        this->sendNext();
    }
}

void ControlConnectionWorker::onDisconnected()
{
    this->m_buffer.clear();
    if (this->m_state == ConnectionState::ShuttingDown)
    {
        return;
    }
    this->m_state = ConnectionState::Disconnected;
    if (this->m_activeCommand.has_value() || !this->m_queue.isEmpty())
    {
        this->failAll(tr("The remote control connection closed unexpectedly."));
    }
}

void ControlConnectionWorker::onErrorOccurred(QAbstractSocket::SocketError _error)
{
    static_cast<void>(_error);
    if (this->m_state != ConnectionState::ShuttingDown &&
        (this->m_activeCommand.has_value() || !this->m_queue.isEmpty()))
    {
        this->failAll(this->m_socket->errorString());
    }
}

void ControlConnectionWorker::onTimeout()
{
    bool const connectionPending{this->m_state == ConnectionState::Connecting ||
                                 this->m_state == ConnectionState::Handshaking};
    if (this->m_state != ConnectionState::ShuttingDown &&
        (connectionPending || this->m_activeCommand.has_value() || !this->m_queue.isEmpty()))
    {
        this->failAll(tr("The remote control request timed out."));
    }
}

void ControlConnectionWorker::failAll(QString const& _message)
{
    std::optional<PendingCommand> activeCommand{std::move(this->m_activeCommand)};
    this->m_activeCommand.reset();
    QQueue<PendingCommand> queuedCommands{std::move(this->m_queue)};
    this->m_queue.clear();
    this->resetSocket();

    if (activeCommand.has_value())
    {
        emit this->commandFailed(
            activeCommand->generation, activeCommand->command, activeCommand->context, _message);
    }
    while (!queuedCommands.isEmpty())
    {
        PendingCommand const command{queuedCommands.dequeue()};
        emit this->commandFailed(command.generation, command.command, command.context, _message);
    }
}

void ControlConnectionWorker::resetSocket()
{
    if (this->m_state != ConnectionState::ShuttingDown)
    {
        this->m_state = ConnectionState::Disconnected;
    }
    this->m_timeoutTimer->stop();
    this->m_buffer.clear();
    if (!this->m_socket)
    {
        return;
    }

    QObject::disconnect(this->m_socket, nullptr, this, nullptr);
    this->m_socket->abort();
    this->m_socket->deleteLater();
    this->m_socket = nullptr;
}

bool ControlConnectionWorker::isMoveOnly(PendingCommand const& _command)
{
    if (_command.command != remote_control::Command::MouseEvent ||
        _command.payload.size() != static_cast<int>(sizeof(remote_control::MouseEventPacket)))
    {
        return false;
    }

    remote_control::MouseEventPacket event{};
    std::memcpy(&event, _command.payload.constData(), sizeof(event));
    return event.action == static_cast<quint16>(remote_control::MouseAction::Move) &&
        event.button == static_cast<quint16>(remote_control::MouseButton::None);
}
