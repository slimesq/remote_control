#include "server/RemoteControlTransportInternal.h"

#include "server/RemoteControlServerLog.h"

#include <WS2tcpip.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace
{

constexpr int InitialAcceptCount{8};
constexpr int MinimumCompletionWorkerCount{2};
constexpr int MaximumCompletionWorkerCount{4};
constexpr int ShellCommandWorkerCount{2};
constexpr int FileWorkerCount{4};
constexpr int ScreenCaptureWorkerCount{2};
constexpr std::size_t MaximumQueuedShellCommandTasks{16};
constexpr std::size_t MaximumQueuedTasks{64};
constexpr std::size_t MaximumQueuedScreenCaptureTasks{8};
constexpr std::size_t MaximumQueuedSendBytes{2U * 1024U * 1024U};
constexpr std::size_t MaximumSingleSendBytes{16U * 1024U * 1024U};
constexpr int ReceiveChunkSize{8 * 1024};
constexpr std::size_t MaximumConnections{256};
constexpr int MaximumScreenStreamConnections{4};
constexpr int MaximumControlStreamConnections{4};
constexpr qint64 FirstRequestIdleTimeoutMs{15 * 1000};
constexpr qint64 OneShotIdleTimeoutMs{30 * 1000};
constexpr qint64 FileTransferIdleTimeoutMs{30 * 1000};
constexpr qint64 ScreenStreamIdleTimeoutMs{30 * 1000};
constexpr qint64 ControlStreamIdleTimeoutMs{5 * 60 * 1000};
constexpr ULONG_PTR StopCompletionKey{1};
constexpr DWORD AcceptAddressPadding{16};

using iocp_detail::monotonicMilliseconds;

/**
 * @brief Checks whether another send fits the bounded per-connection backlog.
 * @param _queuedBytes Bytes already queued or in flight.
 * @param _additionalBytes Bytes requested by the producer.
 * @return true when the bytes fit both the packet and backlog limits; otherwise false.
 */
bool hasSendCapacity(std::size_t _queuedBytes, std::size_t _additionalBytes) noexcept
{
    if (_additionalBytes > MaximumSingleSendBytes)
    {
        return false;
    }
    if (_queuedBytes == 0)
    {
        return true;
    }
    return _additionalBytes <= MaximumQueuedSendBytes &&
        _queuedBytes <= MaximumQueuedSendBytes - _additionalBytes;
}

}  // namespace

RemoteControlTransport::Impl::Impl(ScreenLockService* _screenLockService)
    : m_screenLockService{_screenLockService},
      m_shellCommandTaskPool{ShellCommandWorkerCount, MaximumQueuedShellCommandTasks},
      m_fileTaskPool{FileWorkerCount, MaximumQueuedTasks},
      m_screenCaptureTaskPool{ScreenCaptureWorkerCount, MaximumQueuedScreenCaptureTasks},
      m_connectionRegistry{
          MaximumConnections, MaximumScreenStreamConnections, MaximumControlStreamConnections}
{
}

RemoteControlTransport::Impl::~Impl()
{
    this->stop();
}

bool RemoteControlTransport::Impl::start(quint16 _port)
{
    if (!this->m_winsockRuntime.isValid() || this->m_completionPort || this->m_stopping.load())
    {
        writeServerLog(ServerLogLevel::Critical,
                       QStringLiteral("transport.start_rejected"),
                       {{QStringLiteral("reason"), QStringLiteral("invalid_lifecycle")}});
        return false;
    }

    this->m_completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!this->m_completionPort)
    {
        return false;
    }

    this->m_listenSocket =
        WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (this->m_listenSocket == INVALID_SOCKET)
    {
        this->stop();
        return false;
    }

    BOOL const exclusiveAddressUse{TRUE};
    if (setsockopt(this->m_listenSocket,
                   SOL_SOCKET,
                   SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<char const*>(&exclusiveAddressUse),
                   sizeof(exclusiveAddressUse)) == SOCKET_ERROR)
    {
        this->stop();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(_port);
    if (bind(this->m_listenSocket, reinterpret_cast<sockaddr const*>(&address), sizeof(address)) ==
            SOCKET_ERROR ||
        listen(this->m_listenSocket, SOMAXCONN) == SOCKET_ERROR || !this->loadAcceptEx())
    {
        this->stop();
        return false;
    }

    if (!CreateIoCompletionPort(
            reinterpret_cast<HANDLE>(this->m_listenSocket), this->m_completionPort, 0, 0))
    {
        this->stop();
        return false;
    }

    sockaddr_in boundAddress{};
    int boundAddressSize{sizeof(boundAddress)};
    if (getsockname(this->m_listenSocket,
                    reinterpret_cast<sockaddr*>(&boundAddress),
                    &boundAddressSize) == SOCKET_ERROR)
    {
        this->stop();
        return false;
    }
    this->m_listeningPort.store(ntohs(boundAddress.sin_port));

    unsigned int const hardwareThreads{std::max(1U, std::thread::hardware_concurrency())};
    int const workerCount{
        std::max(MinimumCompletionWorkerCount,
                 std::min(MaximumCompletionWorkerCount, static_cast<int>(hardwareThreads)))};
    this->m_completionThreads.reserve(static_cast<std::size_t>(workerCount));
    for (int index{0}; index < workerCount; ++index)
    {
        this->m_completionThreads.emplace_back([this] { this->runCompletionWorker(); });
    }
    this->m_idleTimeoutThread = std::thread{[this] { this->runIdleTimeoutMonitor(); }};

    this->replenishAccepts();
    if (this->m_pendingAcceptOperationCount.load() != InitialAcceptCount)
    {
        this->stop();
        return false;
    }
    writeServerLog(ServerLogLevel::Info,
                   QStringLiteral("transport.started"),
                   {{QStringLiteral("port"), this->m_listeningPort.load()},
                    {QStringLiteral("completion_workers"), workerCount},
                    {QStringLiteral("initial_accepts"), InitialAcceptCount}});
    return true;
}

void RemoteControlTransport::Impl::stop()
{
    {
        // Serialize shutdown with I/O registration so the drain count cannot miss a new operation.
        std::lock_guard<std::mutex> const lock{this->m_pendingIoOperationMutex};
        if (this->m_stopping.exchange(true))
        {
            return;
        }
    }
    writeServerLog(ServerLogLevel::Info,
                   QStringLiteral("transport.stopping"),
                   {{QStringLiteral("active_connections"),
                     static_cast<qint64>(this->m_connectionRegistry.size())}});
    this->m_listeningPort.store(0);
    this->m_idleTimeoutCondition.notify_all();

    {
        // Wait for any thread submitting AcceptEx or updating an accepted socket's context.
        std::lock_guard<std::mutex> const lock{this->m_acceptMutex};
        if (this->m_listenSocket != INVALID_SOCKET)
        {
            CancelIoEx(reinterpret_cast<HANDLE>(this->m_listenSocket), nullptr);
            closesocket(this->m_listenSocket);
            this->m_listenSocket = INVALID_SOCKET;
        }
    }

    std::vector<std::shared_ptr<ConnectionContext>> const connections{
        this->m_connectionRegistry.snapshot()};
    for (std::shared_ptr<ConnectionContext> const& connection : connections)
    {
        this->closeConnection(connection, ConnectionCloseReason::ServerShutdown);
    }

    this->m_screenCaptureTaskPool.stop();
    this->m_fileTaskPool.stop();
    this->m_shellCommandTaskPool.stop();

    if (this->m_idleTimeoutThread.joinable())
    {
        this->m_idleTimeoutThread.join();
    }

    if (this->m_completionPort)
    {
        std::unique_lock<std::mutex> lock{this->m_pendingIoOperationMutex};
        this->m_pendingIoOperationCondition.wait(
            lock, [this] { return this->m_pendingIoOperationCount.load() == 0; });
        lock.unlock();

        bool wakeFailed{false};
        for (std::size_t index{0}; index < this->m_completionThreads.size(); ++index)
        {
            if (!PostQueuedCompletionStatus(this->m_completionPort, 0, StopCompletionKey, nullptr))
            {
                wakeFailed = true;
                break;
            }
        }
        if (wakeFailed)
        {
            // Closing the port wakes any worker that did not receive a stop completion.
            CloseHandle(this->m_completionPort);
        }
        for (std::thread& thread : this->m_completionThreads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        this->m_completionThreads.clear();
        if (!wakeFailed)
        {
            CloseHandle(this->m_completionPort);
        }
        this->m_completionPort = nullptr;
    }
    writeServerLog(ServerLogLevel::Info, QStringLiteral("transport.stopped"));
}

quint16 RemoteControlTransport::Impl::listeningPort() const noexcept
{
    return this->m_listeningPort.load();
}

bool RemoteControlTransport::Impl::loadAcceptEx()
{
    GUID acceptExGuid WSAID_ACCEPTEX;
    DWORD bytesReturned{0};
    return WSAIoctl(this->m_listenSocket,
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &acceptExGuid,
                    sizeof(acceptExGuid),
                    &this->m_acceptExFunction,
                    sizeof(this->m_acceptExFunction),
                    &bytesReturned,
                    nullptr,
                    nullptr) != SOCKET_ERROR &&
        this->m_acceptExFunction;
}

bool RemoteControlTransport::Impl::postAccept()
{
    if (this->m_stopping.load() || !this->m_acceptExFunction)
    {
        return false;
    }

    int const addressBufferSize{
        2 * (static_cast<int>(sizeof(sockaddr_storage)) + static_cast<int>(AcceptAddressPadding))};
    auto operation{std::make_unique<IoOperation>(
        IoOperationType::Accept, std::shared_ptr<ConnectionContext>{}, addressBufferSize)};
    operation->acceptSocket =
        WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (operation->acceptSocket == INVALID_SOCKET)
    {
        return false;
    }

    DWORD bytesReceived{0};
    if (!this->tryBeginOperation())
    {
        closesocket(operation->acceptSocket);
        return false;
    }
    this->m_pendingAcceptOperationCount.fetch_add(1);
    IoOperation* const operationPointer{operation.release()};
    BOOL const result{this->m_acceptExFunction(this->m_listenSocket,
                                               operationPointer->acceptSocket,
                                               operationPointer->storage.data(),
                                               0,
                                               sizeof(sockaddr_storage) + AcceptAddressPadding,
                                               sizeof(sockaddr_storage) + AcceptAddressPadding,
                                               &bytesReceived,
                                               operationPointer)};
    if (!result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        operation.reset(operationPointer);
        this->m_pendingAcceptOperationCount.fetch_sub(1);
        this->finishOperation();
        closesocket(operation->acceptSocket);
        return false;
    }
    return true;
}

void RemoteControlTransport::Impl::replenishAccepts()
{
    std::lock_guard<std::mutex> const lock{this->m_acceptMutex};
    while (!this->m_stopping.load() &&
           this->m_pendingAcceptOperationCount.load() < InitialAcceptCount)
    {
        if (!this->postAccept())
        {
            // The timeout monitor retries after a bounded delay instead of losing this slot.
            return;
        }
    }
}

bool RemoteControlTransport::Impl::postReceive(
    std::shared_ptr<ConnectionContext> const& _connection)
{
    auto operation{
        std::make_unique<IoOperation>(IoOperationType::Receive, _connection, ReceiveChunkSize)};
    DWORD flags{0};
    DWORD bytesReceived{0};
    bool closeRequired{false};
    {
        std::lock_guard<std::mutex> const lock{_connection->socketMutex};
        if (this->m_stopping.load() || _connection->state.isTerminal() ||
            _connection->socket == INVALID_SOCKET)
        {
            return false;
        }
        if (!this->tryBeginOperation())
        {
            return false;
        }
        IoOperation* const operationPointer{operation.release()};
        int const result{WSARecv(_connection->socket,
                                 &operationPointer->nativeBuffer,
                                 1,
                                 &bytesReceived,
                                 &flags,
                                 operationPointer,
                                 nullptr)};
        if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
        {
            operation.reset(operationPointer);
            this->finishOperation();
            closeRequired = true;
        }
    }
    if (closeRequired)
    {
        this->closeConnection(_connection, ConnectionCloseReason::IoFailure);
        return false;
    }
    return true;
}

bool RemoteControlTransport::Impl::postSend(std::unique_ptr<IoOperation> _operation)
{
    std::shared_ptr<ConnectionContext> const connection{_operation->connection};
    _operation->refreshSendBuffer();
    DWORD bytesSent{0};
    bool closeRequired{false};
    {
        std::lock_guard<std::mutex> const lock{connection->socketMutex};
        if (this->m_stopping.load() || connection->state.isTerminal() ||
            connection->socket == INVALID_SOCKET)
        {
            return false;
        }
        if (!this->tryBeginOperation())
        {
            return false;
        }
        IoOperation* const operationPointer{_operation.release()};
        int const result{WSASend(connection->socket,
                                 &operationPointer->nativeBuffer,
                                 1,
                                 &bytesSent,
                                 0,
                                 operationPointer,
                                 nullptr)};
        if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
        {
            _operation.reset(operationPointer);
            this->finishOperation();
            closeRequired = true;
        }
    }
    if (closeRequired)
    {
        this->closeConnection(connection, ConnectionCloseReason::IoFailure);
        return false;
    }
    return true;
}

void RemoteControlTransport::Impl::runCompletionWorker()
{
    while (true)
    {
        DWORD transferredBytes{0};
        ULONG_PTR completionKey{0};
        OVERLAPPED* overlapped{nullptr};
        BOOL const success{GetQueuedCompletionStatus(
            this->m_completionPort, &transferredBytes, &completionKey, &overlapped, INFINITE)};
        if (!overlapped)
        {
            if (completionKey == StopCompletionKey || this->m_stopping.load())
            {
                return;
            }
            continue;
        }

        this->finishOperation();
        auto operation{std::unique_ptr<IoOperation>{static_cast<IoOperation*>(overlapped)}};
        switch (operation->type)
        {
            case IoOperationType::Accept:
                this->handleAcceptCompletion(std::move(operation), success == TRUE);
                break;
            case IoOperationType::Receive:
                this->handleReceiveCompletion(
                    std::move(operation), success == TRUE, transferredBytes);
                break;
            case IoOperationType::Send:
                this->handleSendCompletion(std::move(operation), success == TRUE, transferredBytes);
                break;
        }
    }
}

void RemoteControlTransport::Impl::runIdleTimeoutMonitor()
{
    while (!this->m_stopping.load())
    {
        std::unique_lock<std::mutex> lock{this->m_idleTimeoutMutex};
        bool const stopping{this->m_idleTimeoutCondition.wait_for(
            lock, std::chrono::seconds{1}, [this] { return this->m_stopping.load(); })};
        if (stopping)
        {
            return;
        }
        lock.unlock();
        this->replenishAccepts();
        qint64 const now{monotonicMilliseconds()};
        std::vector<std::shared_ptr<ConnectionContext>> const connections{
            this->m_connectionRegistry.snapshot()};

        for (std::shared_ptr<ConnectionContext> const& connection : connections)
        {
            ConnectionPhase const currentPhase{connection->state.phase()};
            qint64 timeout{0};
            if (currentPhase == ConnectionPhase::AwaitingRequest)
            {
                timeout = FirstRequestIdleTimeoutMs;
            }
            else if (currentPhase == ConnectionPhase::OneShot)
            {
                timeout = OneShotIdleTimeoutMs;
            }
            else if (currentPhase == ConnectionPhase::FileTransfer)
            {
                timeout = FileTransferIdleTimeoutMs;
            }
            else if (currentPhase == ConnectionPhase::ScreenStream)
            {
                timeout = ScreenStreamIdleTimeoutMs;
            }
            else if (currentPhase == ConnectionPhase::ControlStream)
            {
                timeout = ControlStreamIdleTimeoutMs;
            }
            if (timeout > 0 && now - connection->lastActivityMs.load() >= timeout)
            {
                this->closeConnection(connection, ConnectionCloseReason::IdleTimeout);
            }
        }
    }
}

void RemoteControlTransport::Impl::handleAcceptCompletion(std::unique_ptr<IoOperation> _operation,
                                                          bool _success)
{
    SOCKET const acceptedSocket{_operation->acceptSocket};
    _operation->acceptSocket = INVALID_SOCKET;
    this->m_pendingAcceptOperationCount.fetch_sub(1);
    if (!this->m_stopping.load())
    {
        this->replenishAccepts();
    }
    if (!_success || this->m_stopping.load())
    {
        closesocket(acceptedSocket);
        return;
    }

    bool acceptContextUpdated{false};
    {
        // The listener must remain valid until Windows copies it into the accepted socket.
        std::lock_guard<std::mutex> const lock{this->m_acceptMutex};
        if (!this->m_stopping.load() && this->m_listenSocket != INVALID_SOCKET)
        {
            SOCKET const listenSocket{this->m_listenSocket};
            acceptContextUpdated = setsockopt(acceptedSocket,
                                              SOL_SOCKET,
                                              SO_UPDATE_ACCEPT_CONTEXT,
                                              reinterpret_cast<char const*>(&listenSocket),
                                              sizeof(listenSocket)) != SOCKET_ERROR;
        }
    }
    if (!acceptContextUpdated ||
        !CreateIoCompletionPort(
            reinterpret_cast<HANDLE>(acceptedSocket), this->m_completionPort, 0, 0))
    {
        closesocket(acceptedSocket);
        return;
    }

    BOOL const noDelay{TRUE};
    static_cast<void>(setsockopt(acceptedSocket,
                                 IPPROTO_TCP,
                                 TCP_NODELAY,
                                 reinterpret_cast<char const*>(&noDelay),
                                 sizeof(noDelay)));

    std::shared_ptr<ConnectionContext> const connection{
        this->m_stopping.load() ? std::shared_ptr<ConnectionContext>{}
                                : this->m_connectionRegistry.add(acceptedSocket)};
    if (!connection)
    {
        closesocket(acceptedSocket);
        writeServerLog(ServerLogLevel::Warning,
                       QStringLiteral("connection.rejected"),
                       {{QStringLiteral("reason"), QStringLiteral("capacity_limit")}});
        return;
    }
    writeServerLog(ServerLogLevel::Debug,
                   QStringLiteral("connection.accepted"),
                   {{QStringLiteral("connection_id"), static_cast<qint64>(connection->id)},
                    {QStringLiteral("active_connections"),
                     static_cast<qint64>(this->m_connectionRegistry.size())}});
    static_cast<void>(this->postReceive(connection));
}

void RemoteControlTransport::Impl::handleReceiveCompletion(std::unique_ptr<IoOperation> _operation,
                                                           bool _success,
                                                           DWORD _transferredBytes)
{
    std::shared_ptr<ConnectionContext> const connection{_operation->connection};
    if (!_success || _transferredBytes == 0 || connection->state.isTerminal())
    {
        this->closeConnection(connection,
                              _success && _transferredBytes == 0
                                  ? ConnectionCloseReason::PeerDisconnected
                                  : ConnectionCloseReason::IoFailure);
        return;
    }

    connection->lastActivityMs.store(monotonicMilliseconds());
    connection->receiveBuffer.append(_operation->storage.constData(),
                                     static_cast<int>(_transferredBytes));
    if (!this->processReceivedPackets(connection))
    {
        return;
    }
    static_cast<void>(this->postReceive(connection));
}

void RemoteControlTransport::Impl::handleSendCompletion(std::unique_ptr<IoOperation> _operation,
                                                        bool _success,
                                                        DWORD _transferredBytes)
{
    std::shared_ptr<ConnectionContext> const connection{_operation->connection};
    if (!_success || _transferredBytes == 0 || connection->state.isTerminal())
    {
        this->closeConnection(connection,
                              _success && _transferredBytes == 0
                                  ? ConnectionCloseReason::PeerDisconnected
                                  : ConnectionCloseReason::IoFailure);
        return;
    }

    _operation->sendOffset += static_cast<int>(_transferredBytes);
    connection->lastActivityMs.store(monotonicMilliseconds());
    if (_operation->sendOffset < _operation->sendBytes.size())
    {
        static_cast<void>(this->postSend(std::move(_operation)));
        return;
    }

    QByteArray nextBytes;
    bool closeAfterSend{false};
    {
        std::lock_guard<std::mutex> const lock{connection->sendMutex};
        std::size_t const completedSize{static_cast<std::size_t>(_operation->sendBytes.size())};
        connection->queuedSendBytes = completedSize <= connection->queuedSendBytes
            ? connection->queuedSendBytes - completedSize
            : 0;
        if (!connection->sendQueue.empty())
        {
            nextBytes = std::move(connection->sendQueue.front());
            connection->sendQueue.pop_front();
        }
        else
        {
            connection->sendPending = false;
            closeAfterSend = connection->closeAfterSend;
        }
    }

    if (!nextBytes.isEmpty())
    {
        static_cast<void>(
            this->postSend(std::make_unique<IoOperation>(connection, std::move(nextBytes))));
        return;
    }
    if (closeAfterSend)
    {
        this->closeConnection(connection, ConnectionCloseReason::RequestComplete);
        return;
    }

    ConnectionPhase const currentPhase{connection->state.phase()};
    if (currentPhase == ConnectionPhase::ScreenStream)
    {
        this->completeScreenFrame(connection);
    }
    else if (currentPhase == ConnectionPhase::FileTransfer)
    {
        // File workers resume only after the preceding bounded batch reaches the client.
        this->continueFileTransfer(connection);
    }
}

bool RemoteControlTransport::Impl::enqueuePacket(
    std::shared_ptr<ConnectionContext> const& _connection, remote_control::Packet const& _packet)
{
    QByteArray const bytes{_packet.serialize()};
    return !bytes.isEmpty() && this->enqueueBytes(_connection, bytes);
}

bool RemoteControlTransport::Impl::enqueueBytes(
    std::shared_ptr<ConnectionContext> const& _connection, QByteArray const& _bytes)
{
    QByteArray firstBytes;
    std::size_t const bytesSize{static_cast<std::size_t>(_bytes.size())};
    {
        std::lock_guard<std::mutex> const lock{_connection->sendMutex};
        if (this->m_stopping.load() || _connection->state.isTerminal())
        {
            return false;
        }
        if (!hasSendCapacity(_connection->queuedSendBytes, bytesSize))
        {
            writeServerLog(ServerLogLevel::Warning,
                           QStringLiteral("connection.backpressure"),
                           {{QStringLiteral("connection_id"), static_cast<qint64>(_connection->id)},
                            {QStringLiteral("queued_bytes"),
                             static_cast<qint64>(_connection->queuedSendBytes)},
                            {QStringLiteral("additional_bytes"), static_cast<qint64>(bytesSize)}});
            return false;
        }

        _connection->queuedSendBytes += bytesSize;
        if (_connection->sendPending)
        {
            _connection->sendQueue.push_back(_bytes);
            return true;
        }
        _connection->sendPending = true;
        firstBytes = _bytes;
    }

    return this->postSend(std::make_unique<IoOperation>(_connection, std::move(firstBytes)));
}

void RemoteControlTransport::Impl::sendFinalPacket(
    std::shared_ptr<ConnectionContext> const& _connection, remote_control::Packet const& _packet)
{
    if (this->enqueuePacket(_connection, _packet))
    {
        this->requestCloseAfterSend(_connection);
    }
    else
    {
        this->closeConnection(_connection, ConnectionCloseReason::Backpressure);
    }
}

void RemoteControlTransport::Impl::requestCloseAfterSend(
    std::shared_ptr<ConnectionContext> const& _connection)
{
    bool closeNow{false};
    {
        std::lock_guard<std::mutex> const lock{_connection->sendMutex};
        _connection->closeAfterSend = true;
        closeNow = !_connection->sendPending && _connection->sendQueue.empty();
    }
    if (closeNow)
    {
        this->closeConnection(_connection, ConnectionCloseReason::RequestComplete);
    }
}

void RemoteControlTransport::Impl::closeConnection(
    std::shared_ptr<ConnectionContext> const& _connection, ConnectionCloseReason _reason)
{
    if (!_connection)
    {
        return;
    }
    ConnectionPhase previousPhase{ConnectionPhase::AwaitingRequest};
    if (!_connection->state.tryBeginClosing(&previousPhase))
    {
        return;
    }
    _connection->closeReason.store(_reason);
    {
        std::lock_guard<std::mutex> const lock{_connection->sendMutex};
        _connection->sendQueue.clear();
        _connection->queuedSendBytes = 0;
    }
    {
        std::lock_guard<std::mutex> const lock{_connection->fileTransferMutex};
        _connection->fileTransfer.reset();
    }

    this->m_connectionRegistry.remove(_connection, previousPhase);
    {
        // Wait for any thread currently submitting WSARecv or WSASend before closing the handle.
        std::lock_guard<std::mutex> const lock{_connection->socketMutex};
        if (_connection->socket != INVALID_SOCKET)
        {
            CancelIoEx(reinterpret_cast<HANDLE>(_connection->socket), nullptr);
            shutdown(_connection->socket, SD_BOTH);
            closesocket(_connection->socket);
            _connection->socket = INVALID_SOCKET;
        }
    }
    _connection->state.markClosed();
    writeServerLog(
        _reason == ConnectionCloseReason::ProtocolViolation ||
                _reason == ConnectionCloseReason::Backpressure ||
                _reason == ConnectionCloseReason::CapacityLimit
            ? ServerLogLevel::Warning
            : ServerLogLevel::Debug,
        QStringLiteral("connection.closed"),
        {{QStringLiteral("connection_id"), static_cast<qint64>(_connection->id)},
         {QStringLiteral("previous_phase"), iocp_detail::connectionPhaseName(previousPhase)},
         {QStringLiteral("reason"), iocp_detail::connectionCloseReasonName(_reason)}});
}

bool RemoteControlTransport::Impl::tryBeginOperation() noexcept
{
    std::lock_guard<std::mutex> const lock{this->m_pendingIoOperationMutex};
    if (this->m_stopping.load())
    {
        return false;
    }
    this->m_pendingIoOperationCount.fetch_add(1);
    return true;
}

void RemoteControlTransport::Impl::finishOperation() noexcept
{
    if (this->m_pendingIoOperationCount.fetch_sub(1) == 1)
    {
        // Synchronize the zero transition with condition-variable waiting to avoid a lost wakeup.
        std::lock_guard<std::mutex> const lock{this->m_pendingIoOperationMutex};
        this->m_pendingIoOperationCondition.notify_all();
    }
}

RemoteControlTransport::RemoteControlTransport(ScreenLockService* _screenLockService)
    : m_impl{std::make_unique<Impl>(_screenLockService)}
{
}

RemoteControlTransport::~RemoteControlTransport() = default;

bool RemoteControlTransport::start(quint16 _port)
{
    return this->m_impl->start(_port);
}

void RemoteControlTransport::stop()
{
    this->m_impl->stop();
}

quint16 RemoteControlTransport::listeningPort() const noexcept
{
    return this->m_impl->listeningPort();
}
