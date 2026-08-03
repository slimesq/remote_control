#include "client/ScreenStreamWorker.h"

#include "common/Packet.h"

#include <QTcpSocket>
#include <QTimer>

namespace
{

constexpr int ScreenFrameResponseTimeoutMs{15000};

}  // namespace

ScreenStreamWorker::ScreenStreamWorker() : m_timeoutTimer{new QTimer{this}}
{
    this->m_timeoutTimer->setSingleShot(true);
    this->m_timeoutTimer->setInterval(ScreenFrameResponseTimeoutMs);
    connect(this->m_timeoutTimer, &QTimer::timeout, this, &ScreenStreamWorker::onTimeout);
}

ScreenStreamWorker::~ScreenStreamWorker()
{
    this->resetSocket();
}

void ScreenStreamWorker::requestFrame(QString const& _host, quint16 _port, quint64 _generation)
{
    // Shutdown is terminal, and the worker permits only one frame request at a time.
    if (this->m_state == ScreenStreamState::ShuttingDown ||
        this->m_state == ScreenStreamState::FramePending)
    {
        return;
    }
    if (_host.trimmed().isEmpty() || _port == 0)
    {
        emit this->failed(_generation, tr("The remote screen endpoint is invalid."));
        emit this->requestFinished(_generation);
        return;
    }

    QString const resolvedHost{_host.trimmed()};
    // A changed endpoint invalidates the reusable socket and any bytes buffered from its peer.
    if (this->m_host != resolvedHost || this->m_port != _port)
    {
        this->resetSocket();
        this->m_host = resolvedHost;
        this->m_port = _port;
    }

    this->m_generation = _generation;
    this->m_state = ScreenStreamState::FramePending;
    this->m_timeoutTimer->start();
    this->ensureSocket();
    if (this->m_socket->state() == QAbstractSocket::ConnectedState)
    {
        this->sendFrameRequest();
    }
    else if (this->m_socket->state() == QAbstractSocket::UnconnectedState)
    {
        this->m_socket->connectToHost(this->m_host, this->m_port);
    }
}

void ScreenStreamWorker::closeConnection()
{
    this->closeConnectionAndSetState(ScreenStreamState::Idle);
}

void ScreenStreamWorker::shutdown()
{
    this->closeConnectionAndSetState(ScreenStreamState::ShuttingDown);
}

void ScreenStreamWorker::ensureSocket()
{
    if (this->m_socket)
    {
        return;
    }

    this->m_socket = new QTcpSocket{this};
    connect(this->m_socket, &QTcpSocket::connected, this, &ScreenStreamWorker::onConnected);
    connect(this->m_socket, &QTcpSocket::readyRead, this, &ScreenStreamWorker::onReadyRead);
    connect(this->m_socket, &QTcpSocket::disconnected, this, &ScreenStreamWorker::onDisconnected);
    connect(this->m_socket, &QTcpSocket::errorOccurred, this, &ScreenStreamWorker::onErrorOccurred);
}

void ScreenStreamWorker::sendFrameRequest()
{
    // Sending is valid only for the outstanding request on a fully connected socket.
    if (this->m_state != ScreenStreamState::FramePending || !this->m_socket ||
        this->m_socket->state() != QAbstractSocket::ConnectedState)
    {
        return;
    }

    remote_control::Packet const request{remote_control::Command::WatchScreen};
    QByteArray const bytes{request.serialize()};
    if (bytes.isEmpty() || this->m_socket->write(bytes) < 0)
    {
        this->failRequest(tr("Failed to send the remote screen request."), true);
    }
}

void ScreenStreamWorker::onConnected()
{
    this->sendFrameRequest();
}

void ScreenStreamWorker::onReadyRead()
{
    this->m_buffer.append(this->m_socket->readAll());
    if (this->m_buffer.size() > remote_control::Packet::MaximumSerializedSize)
    {
        this->failRequest(tr("The remote screen response exceeds the packet limit."), true);
        return;
    }

    auto const packet{remote_control::Packet::tryParse(this->m_buffer)};
    if (!packet.has_value())
    {
        return;
    }
    // A frame is accepted only as the response to the single request currently in flight.
    if (this->m_state != ScreenStreamState::FramePending ||
        packet->command != remote_control::Command::WatchScreen)
    {
        this->failRequest(tr("The remote screen returned an unexpected response."), true);
        return;
    }

    QImage image;
    if (!image.loadFromData(packet->payload, "PNG"))
    {
        this->failRequest(tr("Failed to decode the remote screenshot."), true);
        return;
    }

    this->m_state = ScreenStreamState::Idle;
    this->m_timeoutTimer->stop();
    emit this->frameReady(this->m_generation, image);
    emit this->requestFinished(this->m_generation);
}

void ScreenStreamWorker::onDisconnected()
{
    this->m_buffer.clear();
    // A disconnect fails only an outstanding frame; idle and intentional closes are silent.
    if (this->m_state == ScreenStreamState::FramePending)
    {
        this->failRequest(tr("The remote screen connection closed unexpectedly."), false);
    }
}

void ScreenStreamWorker::onErrorOccurred(QAbstractSocket::SocketError _error)
{
    static_cast<void>(_error);
    if (this->m_state == ScreenStreamState::FramePending)
    {
        this->failRequest(this->m_socket->errorString(), true);
    }
}

void ScreenStreamWorker::onTimeout()
{
    if (this->m_state == ScreenStreamState::FramePending)
    {
        this->failRequest(tr("The remote screen request timed out."), true);
    }
}

void ScreenStreamWorker::failRequest(QString const& _message, bool _abortConnection)
{
    // Make failure completion idempotent across timeout, error, and disconnect callbacks.
    if (this->m_state != ScreenStreamState::FramePending)
    {
        return;
    }

    this->m_state = ScreenStreamState::Idle;
    this->m_timeoutTimer->stop();
    if (_abortConnection && this->m_socket)
    {
        this->m_socket->abort();
        this->m_buffer.clear();
    }
    emit this->failed(this->m_generation, _message);
    emit this->requestFinished(this->m_generation);
}

void ScreenStreamWorker::closeConnectionAndSetState(ScreenStreamState _nextState)
{
    // Capture completion responsibility before replacing FramePending with the requested state.
    bool const hadPendingFrameRequest{this->m_state == ScreenStreamState::FramePending};
    quint64 const generation{this->m_generation};
    this->m_state = _nextState;
    this->m_timeoutTimer->stop();
    this->resetSocket();
    if (hadPendingFrameRequest)
    {
        emit this->requestFinished(generation);
    }
}

void ScreenStreamWorker::resetSocket()
{
    if (!this->m_socket)
    {
        this->m_buffer.clear();
        return;
    }

    disconnect(this->m_socket, nullptr, this, nullptr);
    this->m_socket->abort();
    this->m_socket->deleteLater();
    this->m_socket = nullptr;
    this->m_buffer.clear();
}
