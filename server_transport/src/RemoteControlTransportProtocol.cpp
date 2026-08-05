#include "internal/RemoteControlTransportImpl.h"

#include "internal/RemoteControlTransportLog.h"

#include <QFileInfo>
#include <QObject>

#include <cstring>
#include <utility>

namespace
{

constexpr int MaximumIncomingBufferBytes{1024 * 1024};
constexpr qint64 SharedScreenFrameLifetimeMs{16};

using iocp_detail::makeStatusPacket;
using iocp_detail::monotonicMilliseconds;

/**
 * @brief Builds the local drive-list response used by the protocol.
 * @return Packet containing the available Windows drive roots.
 */
remote_control::Packet makeDriveListPacket(RemoteControlHostServices const& _hostServices)
{
    return {remote_control::Command::ListDrives,
            remote_control::encodeUtf8(_hostServices.localDriveRoots().join(','))};
}

}  // namespace

bool RemoteControlTransport::Impl::processReceivedPackets(
    std::shared_ptr<ConnectionContext> const& _connection)
{
    if (_connection->receiveBuffer.size() > MaximumIncomingBufferBytes)
    {
        this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
        return false;
    }

    while (!_connection->state.isTerminal())
    {
        auto const packet{remote_control::Packet::tryParse(_connection->receiveBuffer)};
        if (!packet.has_value())
        {
            break;
        }

        ConnectionPhase const currentPhase{_connection->state.phase()};
        if (currentPhase == ConnectionPhase::AwaitingRequest)
        {
            if (!this->handleInitialPacket(_connection, packet.value()))
            {
                return false;
            }
        }
        else if (currentPhase == ConnectionPhase::ScreenStream)
        {
            if (!this->handleScreenStreamPacket(_connection, packet.value()))
            {
                this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
                return false;
            }
        }
        else if (currentPhase == ConnectionPhase::ControlStream)
        {
            if (!this->handleControlPacket(_connection, packet.value()))
            {
                this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
                return false;
            }
        }
        else
        {
            this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
            return false;
        }
    }

    ConnectionPhase const currentPhase{_connection->state.phase()};
    return currentPhase == ConnectionPhase::AwaitingRequest ||
        currentPhase == ConnectionPhase::ScreenStream ||
        currentPhase == ConnectionPhase::ControlStream;
}

bool RemoteControlTransport::Impl::handleInitialPacket(
    std::shared_ptr<ConnectionContext> const& _connection, remote_control::Packet const& _packet)
{
    if (_packet.command == remote_control::Command::WatchScreen)
    {
        if (!_packet.payload.isEmpty())
        {
            this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
            return false;
        }
        if (!this->m_connectionRegistry.tryClassify(_connection, ConnectionPhase::ScreenStream))
        {
            this->closeConnection(_connection, ConnectionCloseReason::CapacityLimit);
            return false;
        }
        writeTransportLog(TransportLogLevel::Debug,
                          QStringLiteral("connection.classified"),
                          {{QStringLiteral("connection_id"), static_cast<qint64>(_connection->id)},
                           {QStringLiteral("phase"), QStringLiteral("screen_stream")}});
        bool const scheduled{this->scheduleScreenFrame(_connection)};
        if (!scheduled)
        {
            this->closeConnection(_connection, ConnectionCloseReason::TaskRejected);
        }
        return scheduled;
    }

    if (_packet.command == remote_control::Command::ControlChannel)
    {
        if (!_packet.payload.isEmpty())
        {
            this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
            return false;
        }
        if (!this->m_connectionRegistry.tryClassify(_connection, ConnectionPhase::ControlStream))
        {
            this->closeConnection(_connection, ConnectionCloseReason::CapacityLimit);
            return false;
        }
        writeTransportLog(TransportLogLevel::Debug,
                          QStringLiteral("connection.classified"),
                          {{QStringLiteral("connection_id"), static_cast<qint64>(_connection->id)},
                           {QStringLiteral("phase"), QStringLiteral("control_stream")}});
        bool const queued{this->enqueuePacket(
            _connection, makeStatusPacket(remote_control::Command::ControlChannel, true, {}))};
        if (!queued)
        {
            this->closeConnection(_connection, ConnectionCloseReason::Backpressure);
        }
        return queued;
    }

    if (_packet.command == remote_control::Command::ListDirectory ||
        _packet.command == remote_control::Command::DownloadFile ||
        _packet.command == remote_control::Command::DeleteFile)
    {
        if (!this->m_connectionRegistry.tryClassify(_connection, ConnectionPhase::FileTransfer))
        {
            this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
            return false;
        }
        writeTransportLog(TransportLogLevel::Debug,
                          QStringLiteral("connection.classified"),
                          {{QStringLiteral("connection_id"), static_cast<qint64>(_connection->id)},
                           {QStringLiteral("phase"), QStringLiteral("file_transfer")}});
        if (!this->scheduleFileRequest(_connection, _packet))
        {
            this->closeConnection(_connection, ConnectionCloseReason::TaskRejected);
        }
        return false;
    }

    if (_packet.command == remote_control::Command::RunFile)
    {
        if (!this->m_connectionRegistry.tryClassify(_connection, ConnectionPhase::OneShot))
        {
            this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
            return false;
        }
        writeTransportLog(TransportLogLevel::Debug,
                          QStringLiteral("connection.classified"),
                          {{QStringLiteral("connection_id"), static_cast<qint64>(_connection->id)},
                           {QStringLiteral("phase"), QStringLiteral("one_shot")}});
        if (!this->scheduleOpenFile(_connection, _packet.payload))
        {
            this->closeConnection(_connection, ConnectionCloseReason::TaskRejected);
        }
        return false;
    }

    if (!this->m_connectionRegistry.tryClassify(_connection, ConnectionPhase::OneShot))
    {
        this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
        return false;
    }
    writeTransportLog(TransportLogLevel::Debug,
                      QStringLiteral("connection.classified"),
                      {{QStringLiteral("connection_id"), static_cast<qint64>(_connection->id)},
                       {QStringLiteral("phase"), QStringLiteral("one_shot")}});
    remote_control::Packet response;
    bool supported{true};
    switch (_packet.command)
    {
        case remote_control::Command::TestConnection:
            response = remote_control::Packet{remote_control::Command::TestConnection};
            break;
        case remote_control::Command::ListDrives:
            response = makeDriveListPacket(this->m_hostServices);
            break;
        default:
            supported = false;
            break;
    }

    if (!supported)
    {
        this->closeConnection(_connection, ConnectionCloseReason::ProtocolViolation);
    }
    else
    {
        this->sendFinalPacket(_connection, response);
    }
    return false;
}

bool RemoteControlTransport::Impl::handleScreenStreamPacket(
    std::shared_ptr<ConnectionContext> const& _connection, remote_control::Packet const& _packet)
{
    return _packet.command == remote_control::Command::WatchScreen && _packet.payload.isEmpty() &&
        this->scheduleScreenFrame(_connection);
}

bool RemoteControlTransport::Impl::handleControlPacket(
    std::shared_ptr<ConnectionContext> const& _connection, remote_control::Packet const& _packet)
{
    remote_control::Packet response;
    if (_packet.command == remote_control::Command::MouseEvent)
    {
        if (_packet.payload.size() != static_cast<int>(sizeof(remote_control::MouseEventPacket)))
        {
            response = makeStatusPacket(
                _packet.command, false, QObject::tr("Invalid mouse event payload."));
        }
        else
        {
            remote_control::MouseEventPacket event{};
            std::memcpy(&event, _packet.payload.constData(), sizeof(event));
            bool const success{this->m_hostServices.sendMouseEvent(event)};
            response = makeStatusPacket(
                _packet.command,
                success,
                success ? QString{} : QObject::tr("Failed to send the mouse event."));
        }
    }
    else if ((_packet.command == remote_control::Command::LockMachine ||
              _packet.command == remote_control::Command::UnlockMachine) &&
             _packet.payload.isEmpty())
    {
        bool const lock{_packet.command == remote_control::Command::LockMachine};
        bool const success{this->m_hostServices.requestScreenLock(lock)};
        response = makeStatusPacket(_packet.command,
                                    success,
                                    success ? (lock ? QObject::tr("Lock request accepted.")
                                                    : QObject::tr("Unlock request accepted."))
                                            : QObject::tr("Failed to queue the lock operation."));
    }
    else
    {
        response = makeStatusPacket(
            _packet.command, false, QObject::tr("Unsupported control-channel command."));
    }
    return this->enqueuePacket(_connection, response);
}

bool RemoteControlTransport::Impl::scheduleOpenFile(
    std::shared_ptr<ConnectionContext> const& _connection, QByteArray const& _payload)
{
    std::weak_ptr<ConnectionContext> const weakConnection{_connection};
    return this->m_shellCommandTaskPool.submit([this, weakConnection, _payload] {
        std::shared_ptr<ConnectionContext> const connection{weakConnection.lock()};
        if (!connection || connection->state.isTerminal() || this->m_stopping.load())
        {
            return;
        }

        QString const path{remote_control::decodeUtf8(_payload)};
        bool const success{this->m_hostServices.isFilePathAllowed(path) &&
                           QFileInfo::exists(path) && this->m_hostServices.openFile(path)};
        remote_control::Packet const response{
            makeStatusPacket(remote_control::Command::RunFile,
                             success,
                             success ? QObject::tr("Open file completed.")
                                     : QObject::tr("Failed to open file: %1").arg(path))};
        this->sendFinalPacket(connection, response);
    });
}

QByteArray RemoteControlTransport::Impl::makeScreenFramePacketBytes(
    std::shared_ptr<ConnectionContext> const& _connection)
{
    std::lock_guard<std::mutex> const lock{this->m_screenFrameCacheMutex};
    qint64 const now{monotonicMilliseconds()};
    if (!this->m_screenFramePacketCache.isEmpty() &&
        now - this->m_screenFrameCacheTimestampMs <= SharedScreenFrameLifetimeMs &&
        _connection->lastScreenFrameId.load() != this->m_screenFrameCacheId)
    {
        _connection->lastScreenFrameId.store(this->m_screenFrameCacheId);
        return this->m_screenFramePacketCache;
    }

    QByteArray payload{this->m_hostServices.captureScreenPng()};
    if (payload.isEmpty())
    {
        return {};
    }
    QByteArray const packetBytes{
        remote_control::Packet{remote_control::Command::WatchScreen, std::move(payload)}
            .serialize()};
    if (packetBytes.isEmpty())
    {
        return {};
    }

    // Cache complete wire bytes so concurrent viewers share capture, encoding, and serialization.
    this->m_screenFramePacketCache = packetBytes;
    this->m_screenFrameCacheTimestampMs = monotonicMilliseconds();
    ++this->m_screenFrameCacheId;
    _connection->lastScreenFrameId.store(this->m_screenFrameCacheId);
    return packetBytes;
}

bool RemoteControlTransport::Impl::submitScreenFrame(
    std::shared_ptr<ConnectionContext> const& _connection)
{
    std::weak_ptr<ConnectionContext> const weakConnection{_connection};
    return this->m_screenCaptureTaskPool.submit([this, weakConnection] {
        std::shared_ptr<ConnectionContext> const connection{weakConnection.lock()};
        if (!connection || connection->state.isTerminal() || this->m_stopping.load())
        {
            return;
        }

        QByteArray const packetBytes{this->makeScreenFramePacketBytes(connection)};
        bool const queued{!packetBytes.isEmpty() && this->enqueueBytes(connection, packetBytes)};
        if (!queued)
        {
            this->closeConnection(connection, ConnectionCloseReason::Backpressure);
        }
    });
}

bool RemoteControlTransport::Impl::scheduleScreenFrame(
    std::shared_ptr<ConnectionContext> const& _connection)
{
    {
        std::lock_guard<std::mutex> const lock{_connection->screenFrameMutex};
        if (_connection->state.isTerminal())
        {
            return false;
        }
        if (_connection->screenFrameState == ScreenFrameFlowState::FramePending)
        {
            _connection->screenFrameState = ScreenFrameFlowState::FramePendingWithQueuedRequest;
            return true;
        }
        if (_connection->screenFrameState == ScreenFrameFlowState::FramePendingWithQueuedRequest)
        {
            return true;
        }
        _connection->screenFrameState = ScreenFrameFlowState::FramePending;
    }

    bool const submitted{this->submitScreenFrame(_connection)};
    if (!submitted)
    {
        std::lock_guard<std::mutex> const lock{_connection->screenFrameMutex};
        _connection->screenFrameState = ScreenFrameFlowState::Idle;
    }
    return submitted;
}

void RemoteControlTransport::Impl::completeScreenFrame(
    std::shared_ptr<ConnectionContext> const& _connection)
{
    bool submitNextFrame{false};
    {
        std::lock_guard<std::mutex> const lock{_connection->screenFrameMutex};
        if (_connection->screenFrameState == ScreenFrameFlowState::FramePendingWithQueuedRequest &&
            !_connection->state.isTerminal())
        {
            _connection->screenFrameState = ScreenFrameFlowState::FramePending;
            submitNextFrame = true;
        }
        else
        {
            _connection->screenFrameState = ScreenFrameFlowState::Idle;
        }
    }

    if (submitNextFrame && !this->submitScreenFrame(_connection))
    {
        {
            std::lock_guard<std::mutex> const lock{_connection->screenFrameMutex};
            _connection->screenFrameState = ScreenFrameFlowState::Idle;
        }
        this->closeConnection(_connection, ConnectionCloseReason::TaskRejected);
    }
}
