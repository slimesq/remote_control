#include "client/WatchConnectionWorker.h"

#include "common/Packet.h"

#include <QTcpSocket>
#include <QTimer>

namespace
{

constexpr int WatchResponseTimeoutMs{15000};

}  // namespace

WatchConnectionWorker::WatchConnectionWorker() : m_timeoutTimer{new QTimer{this}}
{
    this->m_timeoutTimer->setSingleShot(true);
    this->m_timeoutTimer->setInterval(WatchResponseTimeoutMs);
    connect(this->m_timeoutTimer, &QTimer::timeout, this, &WatchConnectionWorker::onTimeout);
}

WatchConnectionWorker::~WatchConnectionWorker()
{
    this->resetSocket();
}

void WatchConnectionWorker::requestFrame(QString const& _host, quint16 _port, quint64 _generation)
{
    // Shutdown is terminal, and the worker permits only one frame request at a time.
    if (this->m_state == WatchState::ShuttingDown || this->m_state == WatchState::FramePending)
    {
        return;
    }
    if (_host.trimmed().isEmpty() || _port == 0)
    {
        emit this->failed(_generation, tr("The remote monitor endpoint is invalid."));
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
    this->m_state = WatchState::FramePending;
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

void WatchConnectionWorker::closeConnection()
{
    this->closeConnectionAndSetState(WatchState::Idle);
}

void WatchConnectionWorker::shutdown()
{
    this->closeConnectionAndSetState(WatchState::ShuttingDown);
}

void WatchConnectionWorker::ensureSocket()
{
    if (this->m_socket)
    {
        return;
    }

    this->m_socket = new QTcpSocket{this};
    connect(this->m_socket, &QTcpSocket::connected, this, &WatchConnectionWorker::onConnected);
    connect(this->m_socket, &QTcpSocket::readyRead, this, &WatchConnectionWorker::onReadyRead);
    connect(
        this->m_socket, &QTcpSocket::disconnected, this, &WatchConnectionWorker::onDisconnected);
    connect(
        this->m_socket, &QTcpSocket::errorOccurred, this, &WatchConnectionWorker::onErrorOccurred);
}

void WatchConnectionWorker::sendFrameRequest()
{
    // Sending is valid only for the outstanding request on a fully connected socket.
    if (this->m_state != WatchState::FramePending || !this->m_socket ||
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

void WatchConnectionWorker::onConnected()
{
    this->sendFrameRequest();
}

void WatchConnectionWorker::onReadyRead()
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
    if (this->m_state != WatchState::FramePending ||
        packet->command != remote_control::Command::WatchScreen)
    {
        this->failRequest(tr("The remote monitor returned an unexpected response."), true);
        return;
    }

    QImage image;
    if (!image.loadFromData(packet->payload, "PNG"))
    {
        this->failRequest(tr("Failed to decode the remote screenshot."), true);
        return;
    }

    this->m_state = WatchState::Idle;
    this->m_timeoutTimer->stop();
    emit this->frameReady(this->m_generation, image);
    emit this->requestFinished(this->m_generation);
}

void WatchConnectionWorker::onDisconnected()
{
    this->m_buffer.clear();
    // A disconnect fails only an outstanding frame; idle and intentional closes are silent.
    if (this->m_state == WatchState::FramePending)
    {
        this->failRequest(tr("The remote monitor connection closed unexpectedly."), false);
    }
}

void WatchConnectionWorker::onErrorOccurred(QAbstractSocket::SocketError _error)
{
    static_cast<void>(_error);
    if (this->m_state == WatchState::FramePending)
    {
        this->failRequest(this->m_socket->errorString(), true);
    }
}

void WatchConnectionWorker::onTimeout()
{
    if (this->m_state == WatchState::FramePending)
    {
        this->failRequest(tr("The remote screen request timed out."), true);
    }
}

void WatchConnectionWorker::failRequest(QString const& _message, bool _abortConnection)
{
    // Make failure completion idempotent across timeout, error, and disconnect callbacks.
    if (this->m_state != WatchState::FramePending)
    {
        return;
    }

    this->m_state = WatchState::Idle;
    this->m_timeoutTimer->stop();
    if (_abortConnection && this->m_socket)
    {
        this->m_socket->abort();
        this->m_buffer.clear();
    }
    emit this->failed(this->m_generation, _message);
    emit this->requestFinished(this->m_generation);
}

void WatchConnectionWorker::closeConnectionAndSetState(WatchState _nextState)
{
    // Capture completion responsibility before replacing FramePending with the requested state.
    bool const hadPendingRequest{this->m_state == WatchState::FramePending};
    quint64 const generation{this->m_generation};
    this->m_state = _nextState;
    this->m_timeoutTimer->stop();
    this->resetSocket();
    if (hadPendingRequest)
    {
        emit this->requestFinished(generation);
    }
}

void WatchConnectionWorker::resetSocket()
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
