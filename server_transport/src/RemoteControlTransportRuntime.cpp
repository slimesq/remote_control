#include "internal/RemoteControlTransportImpl.h"

#include "common/Protocol.h"

#include <chrono>
#include <utility>

namespace iocp_detail
{

qint64 monotonicMilliseconds() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

remote_control::Packet makeStatusPacket(remote_control::Command _command,
                                        bool _success,
                                        QString const& _message)
{
    return {_command, remote_control::makeStatusPayload(_success, _message)};
}

QString connectionPhaseName(ConnectionPhase _phase)
{
    switch (_phase)
    {
        case ConnectionPhase::AwaitingRequest:
            return QStringLiteral("awaiting_request");
        case ConnectionPhase::OneShot:
            return QStringLiteral("one_shot");
        case ConnectionPhase::FileTransfer:
            return QStringLiteral("file_transfer");
        case ConnectionPhase::ScreenStream:
            return QStringLiteral("screen_stream");
        case ConnectionPhase::ControlStream:
            return QStringLiteral("control_stream");
        case ConnectionPhase::Closing:
            return QStringLiteral("closing");
        case ConnectionPhase::Closed:
            return QStringLiteral("closed");
    }
    return QStringLiteral("unknown");
}

QString connectionCloseReasonName(ConnectionCloseReason _reason)
{
    switch (_reason)
    {
        case ConnectionCloseReason::ServerShutdown:
            return QStringLiteral("server_shutdown");
        case ConnectionCloseReason::PeerDisconnected:
            return QStringLiteral("peer_disconnected");
        case ConnectionCloseReason::IoFailure:
            return QStringLiteral("io_failure");
        case ConnectionCloseReason::ProtocolViolation:
            return QStringLiteral("protocol_violation");
        case ConnectionCloseReason::IdleTimeout:
            return QStringLiteral("idle_timeout");
        case ConnectionCloseReason::CapacityLimit:
            return QStringLiteral("capacity_limit");
        case ConnectionCloseReason::Backpressure:
            return QStringLiteral("backpressure");
        case ConnectionCloseReason::TaskRejected:
            return QStringLiteral("task_rejected");
        case ConnectionCloseReason::RequestComplete:
            return QStringLiteral("request_complete");
        case ConnectionCloseReason::InternalFailure:
            return QStringLiteral("internal_failure");
    }
    return QStringLiteral("unknown");
}

bool ConnectionStateMachine::tryClassify(ConnectionPhase _phase) noexcept
{
    if (_phase != ConnectionPhase::OneShot && _phase != ConnectionPhase::FileTransfer &&
        _phase != ConnectionPhase::ScreenStream && _phase != ConnectionPhase::ControlStream)
    {
        return false;
    }
    ConnectionPhase expected{ConnectionPhase::AwaitingRequest};
    return this->m_phase.compare_exchange_strong(expected, _phase);
}

bool ConnectionStateMachine::tryBeginClosing(ConnectionPhase* _previousPhase) noexcept
{
    ConnectionPhase current{this->m_phase.load()};
    while (current != ConnectionPhase::Closing && current != ConnectionPhase::Closed)
    {
        if (this->m_phase.compare_exchange_weak(current, ConnectionPhase::Closing))
        {
            if (_previousPhase)
            {
                *_previousPhase = current;
            }
            return true;
        }
    }
    return false;
}

void ConnectionStateMachine::markClosed() noexcept
{
    ConnectionPhase expected{ConnectionPhase::Closing};
    static_cast<void>(this->m_phase.compare_exchange_strong(expected, ConnectionPhase::Closed));
}

ConnectionPhase ConnectionStateMachine::phase() const noexcept
{
    return this->m_phase.load();
}

bool ConnectionStateMachine::isTerminal() const noexcept
{
    ConnectionPhase const current{this->phase()};
    return current == ConnectionPhase::Closing || current == ConnectionPhase::Closed;
}

WinsockRuntime::WinsockRuntime()
{
    WSADATA data{};
    this->m_started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

WinsockRuntime::~WinsockRuntime()
{
    if (this->m_started)
    {
        WSACleanup();
    }
}

bool WinsockRuntime::isValid() const noexcept
{
    return this->m_started;
}

TaskPool::TaskPool(int _workerCount, std::size_t _maximumQueuedTasks)
    : m_maximumQueuedTasks{_maximumQueuedTasks}
{
    this->m_threads.reserve(static_cast<std::size_t>(_workerCount));
    for (int index{0}; index < _workerCount; ++index)
    {
        this->m_threads.emplace_back([this] { this->runWorker(); });
    }
}

TaskPool::~TaskPool()
{
    this->stop();
}

bool TaskPool::submit(std::function<void()> _task)
{
    {
        std::lock_guard<std::mutex> const lock{this->m_mutex};
        if (this->m_stopping || this->m_tasks.size() >= this->m_maximumQueuedTasks)
        {
            return false;
        }
        this->m_tasks.push_back(std::move(_task));
    }
    this->m_condition.notify_one();
    return true;
}

void TaskPool::stop()
{
    {
        std::lock_guard<std::mutex> const lock{this->m_mutex};
        if (this->m_stopping)
        {
            return;
        }
        this->m_stopping = true;
        this->m_tasks.clear();
    }
    this->m_condition.notify_all();
    for (std::thread& thread : this->m_threads)
    {
        if (thread.joinable())
        {
            // No later task can start, so cancellation cannot target unrelated synchronous I/O.
            static_cast<void>(CancelSynchronousIo(thread.native_handle()));
        }
    }
    for (std::thread& thread : this->m_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    this->m_threads.clear();
}

void TaskPool::runWorker()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock{this->m_mutex};
            this->m_condition.wait(lock,
                                   [this] { return this->m_stopping || !this->m_tasks.empty(); });
            if (this->m_stopping && this->m_tasks.empty())
            {
                return;
            }
            task = std::move(this->m_tasks.front());
            this->m_tasks.pop_front();
        }
        task();
    }
}

FileTransferState::FileTransferState(FileTransferKind _kind) : kind{_kind}
{
}

ConnectionContext::ConnectionContext(SOCKET _socket, quint64 _id) : socket{_socket}, id{_id}
{
}

ConnectionRegistry::ConnectionRegistry(std::size_t _maximumConnections,
                                       int _maximumScreenStreams,
                                       int _maximumControlStreams)
    : m_maximumConnections{_maximumConnections},
      m_maximumScreenStreams{_maximumScreenStreams},
      m_maximumControlStreams{_maximumControlStreams}
{
}

std::shared_ptr<ConnectionContext> ConnectionRegistry::add(SOCKET _socket)
{
    std::lock_guard<std::mutex> const lock{this->m_mutex};
    if (this->m_connectionsBySocket.size() >= this->m_maximumConnections)
    {
        return {};
    }
    auto connection{std::make_shared<ConnectionContext>(_socket, this->m_nextConnectionId++)};
    if (!this->m_connectionsBySocket.emplace(_socket, connection).second)
    {
        return {};
    }
    return connection;
}

bool ConnectionRegistry::tryClassify(std::shared_ptr<ConnectionContext> const& _connection,
                                     ConnectionPhase _phase)
{
    if (!_connection)
    {
        return false;
    }
    std::lock_guard<std::mutex> const lock{this->m_mutex};
    if (this->m_connectionsBySocket.find(_connection->socket) == this->m_connectionsBySocket.end())
    {
        return false;
    }
    if (_phase == ConnectionPhase::ScreenStream &&
        this->m_screenStreamCount >= this->m_maximumScreenStreams)
    {
        return false;
    }
    if (_phase == ConnectionPhase::ControlStream &&
        this->m_controlStreamCount >= this->m_maximumControlStreams)
    {
        return false;
    }
    if (!_connection->state.tryClassify(_phase))
    {
        return false;
    }
    if (_phase == ConnectionPhase::ScreenStream)
    {
        ++this->m_screenStreamCount;
    }
    else if (_phase == ConnectionPhase::ControlStream)
    {
        ++this->m_controlStreamCount;
    }
    return true;
}

void ConnectionRegistry::remove(std::shared_ptr<ConnectionContext> const& _connection,
                                ConnectionPhase _previousPhase)
{
    if (!_connection)
    {
        return;
    }
    std::lock_guard<std::mutex> const lock{this->m_mutex};
    auto const iterator{this->m_connectionsBySocket.find(_connection->socket)};
    if (iterator == this->m_connectionsBySocket.end() || iterator->second != _connection)
    {
        return;
    }
    this->m_connectionsBySocket.erase(iterator);
    if (_previousPhase == ConnectionPhase::ScreenStream && this->m_screenStreamCount > 0)
    {
        --this->m_screenStreamCount;
    }
    else if (_previousPhase == ConnectionPhase::ControlStream && this->m_controlStreamCount > 0)
    {
        --this->m_controlStreamCount;
    }
}

std::vector<std::shared_ptr<ConnectionContext>> ConnectionRegistry::snapshot() const
{
    std::lock_guard<std::mutex> const lock{this->m_mutex};
    std::vector<std::shared_ptr<ConnectionContext>> connections;
    connections.reserve(this->m_connectionsBySocket.size());
    for (auto const& item : this->m_connectionsBySocket)
    {
        connections.push_back(item.second);
    }
    return connections;
}

std::size_t ConnectionRegistry::size() const
{
    std::lock_guard<std::mutex> const lock{this->m_mutex};
    return this->m_connectionsBySocket.size();
}

IoOperation::IoOperation(IoOperationType _type,
                         std::shared_ptr<ConnectionContext> _connection,
                         int _bufferSize)
    : OVERLAPPED{},
      type{_type},
      connection{std::move(_connection)},
      storage{_bufferSize, Qt::Uninitialized}
{
    this->nativeBuffer.buf = this->storage.data();
    this->nativeBuffer.len = static_cast<ULONG>(this->storage.size());
}

IoOperation::IoOperation(std::shared_ptr<ConnectionContext> _connection, QByteArray _bytes)
    : OVERLAPPED{},
      type{IoOperationType::Send},
      connection{std::move(_connection)},
      sendBytes{std::move(_bytes)}
{
    this->refreshSendBuffer();
}

void IoOperation::refreshSendBuffer()
{
    this->nativeBuffer.buf = this->sendBytes.data() + this->sendOffset;
    this->nativeBuffer.len = static_cast<ULONG>(this->sendBytes.size() - this->sendOffset);
}

}  // namespace iocp_detail
