#include "client/ControlStreamWorker.h"

#include "common/Packet.h"

#include <QTcpSocket>
#include <QTimer>

#include <cstring>

namespace
{

constexpr int MaximumQueuedControlCommands{128};
constexpr int ControlResponseTimeoutMs{15000};

}  // namespace

ControlStreamWorker::ControlStreamWorker() : m_timeoutTimer{new QTimer{this}}
{
    this->m_timeoutTimer->setSingleShot(true);
    this->m_timeoutTimer->setInterval(ControlResponseTimeoutMs);
    connect(this->m_timeoutTimer, &QTimer::timeout, this, &ControlStreamWorker::onTimeout);
}

ControlStreamWorker::~ControlStreamWorker()
{
    this->resetSocket();
}

void ControlStreamWorker::sendMouseEvent(QString const& _host,
                                         quint16 _port,
                                         remote_control::MouseEventPacket const& _event,
                                         quint64 _generation)
{
    QByteArray const payload{reinterpret_cast<char const*>(&_event),
                             static_cast<int>(sizeof(_event))};
    this->enqueueCommand(
        _host,
        _port,
        {remote_control::Command::MouseEvent, payload, tr("Mouse event"), _generation});
}

void ControlStreamWorker::sendCommand(QString const& _host,
                                      quint16 _port,
                                      remote_control::Command _command,
                                      QString const& _context,
                                      quint64 _generation)
{
    // The persistent control channel accepts only state-changing machine commands here;
    // mouse input uses sendMouseEvent(), and all other commands use short-lived requests.
    if (_command != remote_control::Command::LockMachine &&
        _command != remote_control::Command::UnlockMachine)
    {
        emit this->commandFailed(
            _generation, _command, _context, tr("Unsupported control command."));
        return;
    }
    this->enqueueCommand(_host, _port, {_command, {}, _context, _generation});
}

void ControlStreamWorker::closeConnection()
{
    this->m_pendingCommands.clear();
    this->m_inFlightCommand.reset();
    this->resetSocket();
}

void ControlStreamWorker::shutdown()
{
    this->m_state = ControlStreamState::ShuttingDown;
    this->closeConnection();
}

void ControlStreamWorker::ensureSocket()
{
    if (this->m_socket)
    {
        return;
    }

    this->m_socket = new QTcpSocket{this};
    QObject::connect(
        this->m_socket, &QTcpSocket::connected, this, &ControlStreamWorker::onConnected);
    QObject::connect(
        this->m_socket, &QTcpSocket::readyRead, this, &ControlStreamWorker::onReadyRead);
    QObject::connect(
        this->m_socket, &QTcpSocket::disconnected, this, &ControlStreamWorker::onDisconnected);
    QObject::connect(
        this->m_socket, &QTcpSocket::errorOccurred, this, &ControlStreamWorker::onErrorOccurred);
}

void ControlStreamWorker::sendHandshake()
{
    remote_control::Packet const handshake{remote_control::Command::ControlChannel};
    QByteArray const bytes{handshake.serialize()};
    if (bytes.isEmpty() || this->m_socket->write(bytes) < 0)
    {
        this->failAllCommands(tr("Failed to open the remote control channel."));
        return;
    }
    this->m_timeoutTimer->start();
}

void ControlStreamWorker::sendNextCommand()
{
    // Send only after the handshake completes, when no command awaits a response and the
    // queue has work.
    if (this->m_state != ControlStreamState::Ready || this->m_inFlightCommand.has_value() ||
        this->m_pendingCommands.isEmpty())
    {
        // A ready connection with no active or queued command is fully idle and needs no timeout.
        if (this->m_state == ControlStreamState::Ready && !this->m_inFlightCommand.has_value() &&
            this->m_pendingCommands.isEmpty())
        {
            this->m_timeoutTimer->stop();
        }
        return;
    }

    this->m_inFlightCommand = this->m_pendingCommands.dequeue();
    remote_control::Packet const request{this->m_inFlightCommand->command,
                                         this->m_inFlightCommand->payload};
    QByteArray const bytes{request.serialize()};
    if (bytes.isEmpty() || this->m_socket->write(bytes) < 0)
    {
        this->failAllCommands(tr("Failed to send the remote control command."));
        return;
    }
    this->m_timeoutTimer->start();
}

void ControlStreamWorker::enqueueCommand(QString const& _host,
                                         quint16 _port,
                                         ControlCommand _command)
{
    // Shutdown is terminal: accepting work here could recreate a socket during thread teardown.
    if (this->m_state == ControlStreamState::ShuttingDown)
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

    // Commands queued for different endpoints must never share a connection or response stream.
    if (this->m_host != host || this->m_port != _port)
    {
        this->closeConnection();
        this->m_host = host;
        this->m_port = _port;
    }

    // Consecutive move-only events are interchangeable; retain only the newest cursor position
    // without crossing a button event or another ordered command.
    if (isMouseMoveOnly(_command) && !this->m_pendingCommands.isEmpty() &&
        isMouseMoveOnly(this->m_pendingCommands.back()))
    {
        this->m_pendingCommands.back() = std::move(_command);
    }
    else if (this->m_pendingCommands.size() < MaximumQueuedControlCommands)
    {
        this->m_pendingCommands.enqueue(_command);
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
    // Reuse an established channel immediately, or initiate exactly one new connection.
    if (this->m_socket->state() == QAbstractSocket::ConnectedState)
    {
        this->sendNextCommand();
    }
    else if (this->m_socket->state() == QAbstractSocket::UnconnectedState)
    {
        this->m_state = ControlStreamState::Connecting;
        this->m_socket->connectToHost(this->m_host, this->m_port);
        this->m_timeoutTimer->start();
    }
}

void ControlStreamWorker::onConnected()
{
    this->m_state = ControlStreamState::Handshaking;
    this->sendHandshake();
}

void ControlStreamWorker::onReadyRead()
{
    this->m_buffer.append(this->m_socket->readAll());
    if (this->m_buffer.size() > remote_control::Packet::MaximumSerializedSize)
    {
        this->failAllCommands(tr("The remote control response exceeds the packet limit."));
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
        if (this->m_state == ControlStreamState::Handshaking)
        {
            // Only a successful ControlChannel status packet can promote the connection to Ready.
            if (response->command != remote_control::Command::ControlChannel || !success)
            {
                this->failAllCommands(
                    message.isEmpty() ? tr("The control-channel handshake failed.") : message);
                return;
            }
            this->m_state = ControlStreamState::Ready;
            this->sendNextCommand();
            continue;
        }

        // Ready responses are strictly one-for-one and must match the command currently in flight.
        if (this->m_state != ControlStreamState::Ready || !this->m_inFlightCommand.has_value() ||
            response->command != this->m_inFlightCommand->command)
        {
            this->failAllCommands(
                tr("The remote control channel returned an unexpected response."));
            return;
        }

        ControlCommand const completed{std::move(this->m_inFlightCommand.value())};
        this->m_inFlightCommand.reset();
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
        this->sendNextCommand();
    }
}

void ControlStreamWorker::onDisconnected()
{
    this->m_buffer.clear();
    // An intentional shutdown owns cleanup and must not report queued work as a transport failure.
    if (this->m_state == ControlStreamState::ShuttingDown)
    {
        return;
    }
    this->m_state = ControlStreamState::Disconnected;
    // An idle disconnect is harmless; only unfinished work needs a failure notification.
    if (this->m_inFlightCommand.has_value() || !this->m_pendingCommands.isEmpty())
    {
        this->failAllCommands(tr("The remote control connection closed unexpectedly."));
    }
}

void ControlStreamWorker::onErrorOccurred(QAbstractSocket::SocketError _error)
{
    static_cast<void>(_error);
    // Ignore teardown and idle-socket errors because neither represents a failed user command.
    if (this->m_state != ControlStreamState::ShuttingDown &&
        (this->m_inFlightCommand.has_value() || !this->m_pendingCommands.isEmpty()))
    {
        this->failAllCommands(this->m_socket->errorString());
    }
}

void ControlStreamWorker::onTimeout()
{
    bool const connectionPending{this->m_state == ControlStreamState::Connecting ||
                                 this->m_state == ControlStreamState::Handshaking};
    // The timer covers connection setup, handshake, and command responses, but not idle periods.
    if (this->m_state != ControlStreamState::ShuttingDown &&
        (connectionPending || this->m_inFlightCommand.has_value() ||
         !this->m_pendingCommands.isEmpty()))
    {
        this->failAllCommands(tr("The remote control request timed out."));
    }
}

void ControlStreamWorker::failAllCommands(QString const& _message)
{
    std::optional<ControlCommand> inFlightCommand{std::move(this->m_inFlightCommand)};
    this->m_inFlightCommand.reset();
    QQueue<ControlCommand> queuedCommands{std::move(this->m_pendingCommands)};
    this->m_pendingCommands.clear();
    this->resetSocket();

    if (inFlightCommand.has_value())
    {
        emit this->commandFailed(inFlightCommand->generation,
                                 inFlightCommand->command,
                                 inFlightCommand->context,
                                 _message);
    }
    while (!queuedCommands.isEmpty())
    {
        ControlCommand const command{queuedCommands.dequeue()};
        emit this->commandFailed(command.generation, command.command, command.context, _message);
    }
}

void ControlStreamWorker::resetSocket()
{
    // Preserve the terminal shutdown state; every other reset returns the worker to Disconnected.
    if (this->m_state != ControlStreamState::ShuttingDown)
    {
        this->m_state = ControlStreamState::Disconnected;
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

bool ControlStreamWorker::isMouseMoveOnly(ControlCommand const& _command)
{
    // Inspect payload fields only after both the command kind and fixed packet size are validated.
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
