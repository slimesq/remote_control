#pragma once

#include "common/Protocol.h"

#include <cstddef>
#include <memory>

class RemoteControlHostServices;

/** @brief Configures IOCP worker counts, capacity limits, and idle deadlines. */
struct RemoteControlTransportOptions final
{
    static constexpr int DefaultInitialAcceptCount{8};  ///< Default posted accept count.
    static constexpr std::size_t DefaultMaximumQueuedShellCommandTasks{
        16};  ///< Default shell-command queue capacity.
    static constexpr std::size_t DefaultMaximumQueuedFileTasks{
        64};  ///< Default file-operation queue capacity.
    static constexpr std::size_t DefaultMaximumQueuedScreenCaptureTasks{
        8};  ///< Default screen-capture queue capacity.
    static constexpr std::size_t DefaultMaximumConnections{
        256};  ///< Default simultaneous connection capacity.
    static constexpr qint64 MillisecondsPerSecond{1000};  ///< Milliseconds in one second.
    static constexpr qint64 SecondsPerMinute{60};         ///< Seconds in one minute.
    static constexpr qint64 DefaultFirstRequestIdleTimeoutMs{
        15 * MillisecondsPerSecond};  ///< Default first-request deadline.
    static constexpr qint64 DefaultConnectionIdleTimeoutMs{
        30 * MillisecondsPerSecond};  ///< Default ordinary connection deadline.
    static constexpr qint64 DefaultControlStreamIdleTimeoutMs{
        5 * SecondsPerMinute * MillisecondsPerSecond};  ///< Default control-stream deadline.

    int initialAcceptCount{DefaultInitialAcceptCount};  ///< Number of posted AcceptEx operations.
    int minimumCompletionWorkerCount{2};                ///< Minimum IOCP completion worker count.
    int maximumCompletionWorkerCount{4};                ///< Maximum IOCP completion worker count.
    int shellCommandWorkerCount{2};                     ///< Blocking shell-command worker count.
    int fileWorkerCount{4};                             ///< Blocking file-operation worker count.
    int screenCaptureWorkerCount{2};                    ///< Screen capture worker count.
    std::size_t maximumQueuedShellCommandTasks{
        DefaultMaximumQueuedShellCommandTasks};  ///< Shell-command queue capacity.
    std::size_t maximumQueuedFileTasks{
        DefaultMaximumQueuedFileTasks};  ///< File-operation queue capacity.
    std::size_t maximumQueuedScreenCaptureTasks{
        DefaultMaximumQueuedScreenCaptureTasks};  ///< Screen-capture queue capacity.
    std::size_t maximumConnections{
        DefaultMaximumConnections};          ///< Simultaneous connection capacity.
    int maximumScreenStreamConnections{4};   ///< Screen-stream connection capacity.
    int maximumControlStreamConnections{4};  ///< Control-stream connection capacity.
    qint64 firstRequestIdleTimeoutMs{
        DefaultFirstRequestIdleTimeoutMs};  ///< Deadline for receiving the first packet.
    qint64 oneShotIdleTimeoutMs{
        DefaultConnectionIdleTimeoutMs};  ///< One-shot connection inactivity deadline.
    qint64 fileTransferIdleTimeoutMs{
        DefaultConnectionIdleTimeoutMs};  ///< File-transfer inactivity deadline.
    qint64 screenStreamIdleTimeoutMs{
        DefaultConnectionIdleTimeoutMs};  ///< Screen-stream inactivity deadline.
    qint64 controlStreamIdleTimeoutMs{
        DefaultControlStreamIdleTimeoutMs};  ///< Control-stream inactivity deadline.
};

/** @brief Provides the Windows IOCP transport used by the remote-control server. */
class RemoteControlTransport final
{
public:
    /**
     * @brief Creates a stopped IOCP transport.
     * @param _hostServices Thread-safe host operations that must outlive this transport.
     */
    explicit RemoteControlTransport(RemoteControlHostServices& _hostServices);

    /**
     * @brief Creates a stopped IOCP transport with explicit runtime limits.
     * @param _hostServices Thread-safe host operations that must outlive this transport.
     * @param _options Worker, capacity, and timeout configuration.
     */
    RemoteControlTransport(RemoteControlHostServices& _hostServices,
                           RemoteControlTransportOptions const& _options);

    /** @brief Stops all asynchronous operations and releases Windows networking resources. */
    ~RemoteControlTransport();

    RemoteControlTransport(RemoteControlTransport const&) = delete;
    RemoteControlTransport(RemoteControlTransport&&) = delete;
    RemoteControlTransport& operator=(RemoteControlTransport const&) = delete;
    RemoteControlTransport& operator=(RemoteControlTransport&&) = delete;

    /**
     * @brief Starts listening and posts asynchronous accepts.
     * @param _port TCP port to listen on.
     * @return true when all IOCP resources start successfully; otherwise false.
     */
    [[nodiscard]] bool start(quint16 _port = remote_control::DefaultServerPort);

    /** @brief Cancels pending I/O, drains completions, and joins all worker threads. */
    void stop();

    /**
     * @brief Returns the active listening port.
     * @return Listening port, or zero while stopped.
     */
    [[nodiscard]] quint16 listeningPort() const noexcept;

private:
    class Impl;

    std::unique_ptr<Impl> m_impl;  ///< Windows-specific IOCP implementation.
};
