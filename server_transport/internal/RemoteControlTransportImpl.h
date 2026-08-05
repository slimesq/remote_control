#pragma once

#include "RemoteControlHostServices.h"
#include "RemoteControlTransport.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <MSWSock.h>
#include <Windows.h>

#ifdef DeleteFile
#undef DeleteFile
#endif

#include "common/Packet.h"

#include <QByteArray>
#include <QDirIterator>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace iocp_detail
{

/**
 * @brief Returns a monotonic timestamp for connection idle checks.
 * @return Milliseconds elapsed on the steady clock.
 */
[[nodiscard]] qint64 monotonicMilliseconds() noexcept;

/**
 * @brief Creates a common command-status packet.
 * @param _command Command associated with the status.
 * @param _success Whether the command succeeded.
 * @param _message Optional user-facing result message.
 * @return Command-status packet.
 */
[[nodiscard]] remote_control::Packet makeStatusPacket(remote_control::Command _command,
                                                      bool _success,
                                                      QString const& _message = {});

/** @brief Identifies the asynchronous operation represented by one completion. */
enum class IoOperationType
{
    Accept,   ///< Accepts a new client socket.
    Receive,  ///< Receives bytes from one connected client.
    Send,     ///< Sends bytes to one connected client.
};

/** @brief Identifies the lifecycle phase of one accepted connection. */
enum class ConnectionPhase
{
    AwaitingRequest,  ///< Waiting for the request that classifies the connection.
    OneShot,          ///< Processing a lightweight one-shot command.
    FileTransfer,     ///< Processing a file command on the file task pool.
    ScreenStream,     ///< Serving a persistent remote-screen stream.
    ControlStream,    ///< Serving a persistent input-control stream.
    Closing,          ///< Socket cancellation and state cleanup have begun.
    Closed,           ///< All connection-owned resources have been released.
};

/** @brief Identifies why a connection entered its terminal lifecycle. */
enum class ConnectionCloseReason
{
    ServerShutdown,     ///< The server is stopping.
    PeerDisconnected,   ///< The peer closed the TCP stream.
    IoFailure,          ///< A native receive or send operation failed.
    ProtocolViolation,  ///< The peer sent a packet invalid for the current phase.
    IdleTimeout,        ///< No network progress occurred before the phase deadline.
    CapacityLimit,      ///< A server connection or stream quota was exhausted.
    Backpressure,       ///< The bounded send queue could not accept more bytes.
    TaskRejected,       ///< A bounded worker queue rejected required work.
    RequestComplete,    ///< A one-shot or file response completed normally.
    InternalFailure,    ///< An internal prerequisite or invariant failed.
};

/**
 * @brief Returns a stable diagnostic name for a connection phase.
 * @param _phase Connection phase to describe.
 * @return Lowercase structured-log value.
 */
[[nodiscard]] QString connectionPhaseName(ConnectionPhase _phase);

/**
 * @brief Returns a stable diagnostic name for a close reason.
 * @param _reason Close reason to describe.
 * @return Lowercase structured-log value.
 */
[[nodiscard]] QString connectionCloseReasonName(ConnectionCloseReason _reason);

/** @brief Enforces the allowed lifecycle transitions for one connection. */
class ConnectionStateMachine final
{
public:
    /** @brief Creates a connection waiting for its first request. */
    ConnectionStateMachine() = default;

    /**
     * @brief Assigns the immutable protocol phase selected by the first request.
     * @param _phase OneShot, FileTransfer, ScreenStream, or ControlStream.
     * @return true only for a valid AwaitingRequest-to-role transition.
     */
    [[nodiscard]] bool tryClassify(ConnectionPhase _phase) noexcept;

    /**
     * @brief Starts idempotent connection shutdown.
     * @param _previousPhase Receives the active phase that began closing.
     * @return true only for the thread that wins the transition to Closing.
     */
    [[nodiscard]] bool tryBeginClosing(ConnectionPhase* _previousPhase) noexcept;

    /** @brief Completes the required Closing-to-Closed transition. */
    void markClosed() noexcept;

    /**
     * @brief Returns the current lifecycle phase.
     * @return Current connection phase.
     */
    [[nodiscard]] ConnectionPhase phase() const noexcept;

    /**
     * @brief Checks whether new I/O and protocol work must be rejected.
     * @return true while closing or closed; otherwise false.
     */
    [[nodiscard]] bool isTerminal() const noexcept;

private:
    std::atomic<ConnectionPhase> m_phase{
        ConnectionPhase::AwaitingRequest};  ///< Current lifecycle phase.
};

/** @brief Tracks one screen stream's bounded frame-request flow. */
enum class ScreenFrameFlowState
{
    Idle,                           ///< No frame is being captured or sent.
    FramePending,                   ///< One frame is being captured or sent.
    FramePendingWithQueuedRequest,  ///< One additional request is coalesced.
};

/** @brief Identifies the incremental file response stored by one connection. */
enum class FileTransferKind
{
    Directory,  ///< Sends a directory listing in bounded packet batches.
    Download,   ///< Sends a file header followed by bounded data chunks.
};

/** @brief Owns process-wide Winsock initialization. */
class WinsockRuntime final
{
public:
    /** @brief Starts Winsock 2.2 for the lifetime of this object. */
    WinsockRuntime();

    /** @brief Releases Winsock when initialization succeeded. */
    ~WinsockRuntime();

    WinsockRuntime(WinsockRuntime const&) = delete;
    WinsockRuntime(WinsockRuntime&&) = delete;
    WinsockRuntime& operator=(WinsockRuntime const&) = delete;
    WinsockRuntime& operator=(WinsockRuntime&&) = delete;

    /**
     * @brief Returns whether Winsock initialization succeeded.
     * @return true when this object owns an active Winsock session; otherwise false.
     */
    [[nodiscard]] bool isValid() const noexcept;

private:
    bool m_started{false};  ///< Whether this object owns a successful WSAStartup call.
};

/** @brief Runs bounded blocking work on a fixed set of reusable threads. */
class TaskPool final
{
public:
    /**
     * @brief Starts the requested number of reusable workers.
     * @param _workerCount Number of task threads.
     * @param _maximumQueuedTasks Maximum number of waiting tasks.
     */
    TaskPool(int _workerCount, std::size_t _maximumQueuedTasks);

    /** @brief Discards queued tasks and joins active workers. */
    ~TaskPool();

    TaskPool(TaskPool const&) = delete;
    TaskPool(TaskPool&&) = delete;
    TaskPool& operator=(TaskPool const&) = delete;
    TaskPool& operator=(TaskPool&&) = delete;

    /**
     * @brief Enqueues work without waiting for a worker.
     * @param _task Work to execute.
     * @return true when queued; otherwise false.
     */
    [[nodiscard]] bool submit(std::function<void()> _task);

    /** @brief Discards queued tasks and waits for active tasks to return. */
    void stop();

private:
    /** @brief Processes queued work until pool shutdown. */
    void runWorker();

    std::size_t m_maximumQueuedTasks{0};        ///< Maximum number of waiting tasks.
    bool m_stopping{false};                     ///< Whether shutdown rejects new tasks.
    std::mutex m_mutex;                         ///< Protects pool state and the task queue.
    std::condition_variable m_condition;        ///< Wakes workers when work or shutdown arrives.
    std::deque<std::function<void()>> m_tasks;  ///< Tasks waiting for an available worker.
    std::vector<std::thread> m_threads;         ///< Reusable task worker threads.
};

/** @brief Retains one incremental file response between asynchronous send completions. */
struct FileTransferState final
{
    /**
     * @brief Creates an empty transfer of the requested kind.
     * @param _kind Directory or download transfer kind.
     */
    explicit FileTransferState(FileTransferKind _kind);

    FileTransferKind kind{FileTransferKind::Directory};  ///< Incremental response category.
    std::ifstream file;                                  ///< Open binary source for a download.
    qint64 remainingBytes{0};                            ///< Download bytes not yet queued.
    bool headerPending{true};                            ///< Whether the size header is unsent.
    std::unique_ptr<QDirIterator> directoryIterator;     ///< Incremental directory enumerator.
    bool finished{false};                                ///< Whether the final batch is queued.
};

/** @brief Stores transport and protocol state for one accepted client connection. */
struct ConnectionContext final
{
    /**
     * @brief Creates an initial protocol context for a connected socket.
     * @param _socket Connected overlapped socket.
     * @param _id Process-local connection identifier used by diagnostics.
     */
    ConnectionContext(SOCKET _socket, quint64 _id);

    SOCKET socket{INVALID_SOCKET};  ///< Connected socket owned until close begins.
    quint64 id{0};                  ///< Process-local diagnostic connection identifier.
    ConnectionStateMachine state;   ///< Enforced connection lifecycle and protocol phase.
    std::atomic<ConnectionCloseReason> closeReason{
        ConnectionCloseReason::InternalFailure};  ///< Terminal reason used by diagnostics.
    std::atomic<qint64> lastActivityMs{monotonicMilliseconds()};  ///< Last network-progress time.
    QByteArray receiveBuffer;     ///< Bytes waiting to form complete packets.
    std::mutex socketMutex;       ///< Serializes native I/O submission with socket close.
    std::mutex screenFrameMutex;  ///< Protects persistent screen-frame flow transitions.
    ScreenFrameFlowState screenFrameState{ScreenFrameFlowState::Idle};  ///< Frame-request state.
    std::atomic<quint64> lastScreenFrameId{0};        ///< Last shared frame sent to this viewer.
    std::mutex fileTransferMutex;                     ///< Protects the incremental file response.
    std::shared_ptr<FileTransferState> fileTransfer;  ///< File response resumed after send drain.
    std::mutex sendMutex;                             ///< Protects ordered send state.
    std::deque<QByteArray> sendQueue;                 ///< Serialized packets waiting to send.
    std::size_t queuedSendBytes{0};                   ///< In-flight and queued byte count.
    bool sendPending{false};                          ///< Whether one WSASend is in flight.
    bool closeAfterSend{false};                       ///< Whether the final send closes the socket.
};

/** @brief Owns the active-connection map and enforces role-specific quotas. */
class ConnectionRegistry final
{
public:
    /**
     * @brief Creates a registry with bounded total and persistent-stream capacities.
     * @param _maximumConnections Maximum simultaneously registered connections.
     * @param _maximumScreenStreams Maximum classified screen streams.
     * @param _maximumControlStreams Maximum classified control streams.
     */
    ConnectionRegistry(std::size_t _maximumConnections,
                       int _maximumScreenStreams,
                       int _maximumControlStreams);

    /**
     * @brief Creates and registers a context for an accepted socket.
     * @param _socket Newly accepted socket.
     * @return Registered context, or nullptr when the registry is full.
     */
    [[nodiscard]] std::shared_ptr<ConnectionContext> add(SOCKET _socket);

    /**
     * @brief Classifies an initial connection while enforcing stream quotas.
     * @param _connection Registered connection to classify.
     * @param _phase Requested immutable protocol phase.
     * @return true when the state transition and quota reservation succeed.
     */
    [[nodiscard]] bool tryClassify(std::shared_ptr<ConnectionContext> const& _connection,
                                   ConnectionPhase _phase);

    /**
     * @brief Removes a closing connection and releases its role quota.
     * @param _connection Connection being closed.
     * @param _previousPhase Active phase before the Closing transition.
     */
    void remove(std::shared_ptr<ConnectionContext> const& _connection,
                ConnectionPhase _previousPhase);

    /**
     * @brief Returns a stable snapshot for timeout checks or server shutdown.
     * @return Shared references to all currently registered connections.
     */
    [[nodiscard]] std::vector<std::shared_ptr<ConnectionContext>> snapshot() const;

    /**
     * @brief Returns the current registered connection count.
     * @return Number of active registry entries.
     */
    [[nodiscard]] std::size_t size() const;

private:
    std::size_t m_maximumConnections{0};  ///< Total connection capacity.
    int m_maximumScreenStreams{0};        ///< Screen-stream connection capacity.
    int m_maximumControlStreams{0};       ///< Control-stream connection capacity.
    int m_screenStreamCount{0};           ///< Reserved screen-stream slots.
    int m_controlStreamCount{0};          ///< Reserved control-stream slots.
    quint64 m_nextConnectionId{1};        ///< Identifier assigned to the next connection.
    mutable std::mutex m_mutex;           ///< Protects entries, identifiers, and quotas.
    std::unordered_map<SOCKET, std::shared_ptr<ConnectionContext>> m_connectionsBySocket;
    ///< Registered contexts indexed by their socket handles.
};

/** @brief Extends OVERLAPPED with the state required to finish one asynchronous operation. */
struct IoOperation final : OVERLAPPED
{
    /**
     * @brief Creates an accept or receive operation and its native buffer.
     * @param _type Accept or Receive operation type.
     * @param _connection Optional connection associated with a receive.
     * @param _bufferSize Number of native bytes reserved for the operation.
     */
    IoOperation(IoOperationType _type,
                std::shared_ptr<ConnectionContext> _connection,
                int _bufferSize);

    /**
     * @brief Creates a send operation that owns its serialized bytes.
     * @param _connection Connection that owns the send sequence.
     * @param _bytes Serialized bytes to send.
     */
    IoOperation(std::shared_ptr<ConnectionContext> _connection, QByteArray _bytes);

    /** @brief Points the native send buffer at the remaining serialized bytes. */
    void refreshSendBuffer();

    IoOperationType type{IoOperationType::Receive};  ///< Completion dispatch category.
    std::shared_ptr<ConnectionContext> connection;   ///< Connection kept alive by this I/O.
    SOCKET acceptSocket{INVALID_SOCKET};             ///< Socket created for AcceptEx.
    QByteArray storage;                              ///< Accept or receive operation buffer.
    QByteArray sendBytes;                            ///< Serialized bytes owned during a send.
    int sendOffset{0};                               ///< Bytes already sent from sendBytes.
    WSABUF nativeBuffer{};                           ///< Native view passed to Winsock.
};

}  // namespace iocp_detail

/** @brief Implements the Windows IOCP transport behind the public PIMPL boundary. */
class RemoteControlTransport::Impl final
{
public:
    /**
     * @brief Creates a stopped IOCP implementation.
     * @param _hostServices Thread-safe host operations that outlive this implementation.
     */
    Impl(RemoteControlHostServices& _hostServices, RemoteControlTransportOptions const& _options);

    /** @brief Stops the implementation before destroying synchronization state. */
    ~Impl();

    Impl(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;

    /**
     * @brief Creates the completion port, listener, workers, and initial accepts.
     * @param _port TCP port to listen on.
     * @return true when startup succeeds; otherwise false.
     */
    [[nodiscard]] bool start(quint16 _port);

    /** @brief Cancels sockets, drains pending completions, and joins every server thread. */
    void stop();

    /**
     * @brief Returns the active listener port.
     * @return Listening port, or zero while stopped.
     */
    [[nodiscard]] quint16 listeningPort() const noexcept;

private:
    using ConnectionContext = iocp_detail::ConnectionContext;
    using ConnectionCloseReason = iocp_detail::ConnectionCloseReason;
    using ConnectionPhase = iocp_detail::ConnectionPhase;
    using ConnectionRegistry = iocp_detail::ConnectionRegistry;
    using FileTransferState = iocp_detail::FileTransferState;
    using FileTransferKind = iocp_detail::FileTransferKind;
    using IoOperation = iocp_detail::IoOperation;
    using IoOperationType = iocp_detail::IoOperationType;
    using TaskPool = iocp_detail::TaskPool;
    using ScreenFrameFlowState = iocp_detail::ScreenFrameFlowState;
    using WinsockRuntime = iocp_detail::WinsockRuntime;

    /**
     * @brief Loads the AcceptEx extension function for the listener provider.
     * @return true when AcceptEx is available; otherwise false.
     */
    [[nodiscard]] bool loadAcceptEx();

    /**
     * @brief Creates a socket and submits one asynchronous accept operation.
     * @return true when the accept was submitted; otherwise false.
     */
    [[nodiscard]] bool postAccept();

    /** @brief Restores the configured number of pending accept operations. */
    void replenishAccepts();

    /**
     * @brief Submits an asynchronous receive for one active connection.
     * @param _connection Connection that receives the next bytes.
     * @return true when the receive was submitted; otherwise false.
     */
    [[nodiscard]] bool postReceive(std::shared_ptr<ConnectionContext> const& _connection);

    /**
     * @brief Submits an asynchronous send operation.
     * @param _operation Operation that owns the serialized bytes.
     * @return true when the send was submitted; otherwise false.
     */
    [[nodiscard]] bool postSend(std::unique_ptr<IoOperation> _operation);

    /** @brief Dispatches completion notifications until shutdown. */
    void runCompletionWorker();

    /** @brief Closes connections that exceed their idle timeout. */
    void runIdleTimeoutMonitor();

    /**
     * @brief Finalizes an AcceptEx completion and starts receiving from the client.
     * @param _operation Completed accept operation.
     * @param _success Whether AcceptEx completed successfully.
     */
    void handleAcceptCompletion(std::unique_ptr<IoOperation> _operation, bool _success);

    /**
     * @brief Appends received bytes, dispatches packets, and submits the next receive.
     * @param _operation Completed receive operation.
     * @param _success Whether WSARecv completed successfully.
     * @param _transferredBytes Number of bytes received.
     */
    void handleReceiveCompletion(std::unique_ptr<IoOperation> _operation,
                                 bool _success,
                                 DWORD _transferredBytes);

    /**
     * @brief Advances the ordered send queue after a send completion.
     * @param _operation Completed send operation.
     * @param _success Whether WSASend completed successfully.
     * @param _transferredBytes Number of bytes sent.
     */
    void handleSendCompletion(std::unique_ptr<IoOperation> _operation,
                              bool _success,
                              DWORD _transferredBytes);

    /**
     * @brief Parses and dispatches every complete packet currently buffered.
     * @param _connection Connection whose receive buffer is processed.
     * @return true while the connection may continue receiving; otherwise false.
     */
    [[nodiscard]] bool processReceivedPackets(
        std::shared_ptr<ConnectionContext> const& _connection);

    /**
     * @brief Classifies a connection from its first command and routes the request.
     * @param _connection Newly accepted connection.
     * @param _packet First complete protocol packet.
     * @return true for a persistent connection; otherwise false.
     */
    [[nodiscard]] bool handleInitialPacket(std::shared_ptr<ConnectionContext> const& _connection,
                                           remote_control::Packet const& _packet);

    /**
     * @brief Handles one frame request on a persistent screen stream.
     * @param _connection Screen-stream connection receiving the request.
     * @param _packet Frame request packet.
     * @return true when the connection remains valid; otherwise false.
     */
    [[nodiscard]] bool handleScreenStreamPacket(
        std::shared_ptr<ConnectionContext> const& _connection,
        remote_control::Packet const& _packet);

    /**
     * @brief Handles one input command on a persistent control connection.
     * @param _connection Control connection receiving the command.
     * @param _packet Control command packet.
     * @return true when the connection remains valid; otherwise false.
     */
    [[nodiscard]] bool handleControlPacket(std::shared_ptr<ConnectionContext> const& _connection,
                                           remote_control::Packet const& _packet);

    /**
     * @brief Enqueues a supported file request on the file task pool.
     * @param _connection Connection that owns the request.
     * @param _packet File command and payload.
     * @return true when the task was accepted; otherwise false.
     */
    [[nodiscard]] bool scheduleFileRequest(std::shared_ptr<ConnectionContext> const& _connection,
                                           remote_control::Packet const& _packet);

    /**
     * @brief Enqueues a shell-open request on the shell-command task pool.
     * @param _connection Connection that owns the request.
     * @param _payload Encoded local path.
     * @return true when the task was accepted; otherwise false.
     */
    [[nodiscard]] bool scheduleOpenFile(std::shared_ptr<ConnectionContext> const& _connection,
                                        QByteArray const& _payload);

    /**
     * @brief Captures or reuses a screen frame and serializes its response packet.
     * @param _connection Screen-stream connection requesting the frame.
     * @return Serialized frame response, or an empty array on failure.
     */
    [[nodiscard]] QByteArray makeScreenFramePacketBytes(
        std::shared_ptr<ConnectionContext> const& _connection);

    /**
     * @brief Submits one frame capture to the screen-capture task pool.
     * @param _connection Screen-stream connection requesting the frame.
     * @return true when the task was accepted; otherwise false.
     */
    [[nodiscard]] bool submitScreenFrame(std::shared_ptr<ConnectionContext> const& _connection);

    /**
     * @brief Starts or coalesces one request according to the frame flow state.
     * @param _connection Screen-stream connection requesting the frame.
     * @return true when the request is accepted; otherwise false.
     */
    [[nodiscard]] bool scheduleScreenFrame(std::shared_ptr<ConnectionContext> const& _connection);

    /**
     * @brief Completes one frame task and starts a coalesced request when present.
     * @param _connection Screen-stream connection whose task completed.
     */
    void completeScreenFrame(std::shared_ptr<ConnectionContext> const& _connection);

    /**
     * @brief Initializes an incremental directory response.
     * @param _connection Connection receiving the directory entries.
     * @param _payload Encoded directory path.
     */
    void streamDirectory(std::shared_ptr<ConnectionContext> const& _connection,
                         QByteArray const& _payload);

    /**
     * @brief Encodes and queues the next bounded directory batch.
     * @param _connection Connection receiving the directory entries.
     * @param _transfer Active directory transfer state.
     */
    void queueDirectoryBatch(std::shared_ptr<ConnectionContext> const& _connection,
                             std::shared_ptr<FileTransferState> const& _transfer);

    /**
     * @brief Opens a file and initializes its incremental download response.
     * @param _connection Connection receiving the file.
     * @param _payload Encoded file path.
     */
    void streamDownload(std::shared_ptr<ConnectionContext> const& _connection,
                        QByteArray const& _payload);

    /**
     * @brief Reads and queues the next bounded download chunk.
     * @param _connection Connection receiving the file.
     * @param _transfer Active download transfer state.
     */
    void queueDownloadChunk(std::shared_ptr<ConnectionContext> const& _connection,
                            std::shared_ptr<FileTransferState> const& _transfer);

    /**
     * @brief Resumes a file response after its send queue drains.
     * @param _connection Connection that owns the file response.
     */
    void continueFileTransfer(std::shared_ptr<ConnectionContext> const& _connection);

    /**
     * @brief Deletes a local file or directory and sends the result.
     * @param _connection Connection that owns the request.
     * @param _payload Encoded target path.
     */
    void deleteTarget(std::shared_ptr<ConnectionContext> const& _connection,
                      QByteArray const& _payload);

    /**
     * @brief Serializes and queues one protocol packet.
     * @param _connection Destination connection.
     * @param _packet Packet to serialize.
     * @return true when queued; otherwise false.
     */
    [[nodiscard]] bool enqueuePacket(std::shared_ptr<ConnectionContext> const& _connection,
                                     remote_control::Packet const& _packet);

    /**
     * @brief Appends serialized bytes to the bounded ordered send queue.
     * @param _connection Destination connection.
     * @param _bytes Serialized bytes to queue.
     * @return true when queued; otherwise false.
     */
    [[nodiscard]] bool enqueueBytes(std::shared_ptr<ConnectionContext> const& _connection,
                                    QByteArray const& _bytes);

    /**
     * @brief Queues a one-shot response and closes after its bytes are sent.
     * @param _connection Destination connection.
     * @param _packet Final response packet.
     */
    void sendFinalPacket(std::shared_ptr<ConnectionContext> const& _connection,
                         remote_control::Packet const& _packet);

    /**
     * @brief Marks a connection to close after its ordered send queue drains.
     * @param _connection Connection to close gracefully.
     */
    void requestCloseAfterSend(std::shared_ptr<ConnectionContext> const& _connection);

    /**
     * @brief Idempotently cancels and closes one connection.
     * @param _connection Connection to close.
     */
    void closeConnection(std::shared_ptr<ConnectionContext> const& _connection,
                         ConnectionCloseReason _reason = ConnectionCloseReason::InternalFailure);

    /**
     * @brief Registers one newly submitted asynchronous I/O operation.
     * @return true while new operations are accepted; otherwise false.
     */
    [[nodiscard]] bool tryBeginOperation() noexcept;

    /** @brief Releases one pending-operation count and wakes shutdown if drained. */
    void finishOperation() noexcept;

    RemoteControlHostServices& m_hostServices;       ///< Host operations used by commands.
    RemoteControlTransportOptions m_options;         ///< Validated transport configuration.
    WinsockRuntime m_winsockRuntime;                 ///< Process Winsock lifetime owner.
    TaskPool m_shellCommandTaskPool;                 ///< Bounded shell-command task pool.
    TaskPool m_fileTaskPool;                         ///< Bounded blocking file-operation pool.
    TaskPool m_screenCaptureTaskPool;                ///< Screen capture and PNG encoding pool.
    ConnectionRegistry m_connectionRegistry;         ///< Active contexts and role quotas.
    std::atomic_bool m_stopping{false};              ///< Whether server shutdown has begun.
    SOCKET m_listenSocket{INVALID_SOCKET};           ///< Overlapped listening socket.
    HANDLE m_completionPort{nullptr};                ///< Windows I/O completion port.
    LPFN_ACCEPTEX m_acceptExFunction{nullptr};       ///< Dynamically loaded AcceptEx function.
    std::atomic<quint16> m_listeningPort{0};         ///< Active bound TCP port.
    std::vector<std::thread> m_completionThreads;    ///< GetQueuedCompletionStatus workers.
    std::thread m_idleTimeoutThread;                 ///< Connection idle-timeout monitor.
    std::mutex m_acceptMutex;                        ///< Serializes AcceptEx slot replenishment.
    std::mutex m_idleTimeoutMutex;                   ///< Protects interruptible timeout waiting.
    std::condition_variable m_idleTimeoutCondition;  ///< Wakes the timeout monitor during shutdown.
    std::atomic_int m_pendingAcceptOperationCount{0};  ///< AcceptEx operations awaiting completion.
    std::mutex m_screenFrameCacheMutex;            ///< Serializes shared capture and PNG encoding.
    QByteArray m_screenFramePacketCache;           ///< Most recent serialized frame response.
    qint64 m_screenFrameCacheTimestampMs{0};       ///< Monotonic time of the cached frame.
    quint64 m_screenFrameCacheId{0};               ///< Identity of the cached frame payload.
    std::atomic_int m_pendingIoOperationCount{0};  ///< I/O operations awaiting completion.
    std::mutex m_pendingIoOperationMutex;          ///< Coordinates the shutdown drain wait.
    /** @brief Wakes shutdown when the pending I/O operation count reaches zero. */
    std::condition_variable m_pendingIoOperationCondition;
};
