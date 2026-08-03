#pragma once

#include "common/Protocol.h"

#include <memory>

class ScreenLockService;

/** @brief Provides the Windows IOCP transport used by the remote-control server. */
class RemoteControlTransport final
{
public:
    /**
     * @brief Creates a stopped IOCP transport.
     * @param _screenLockService GUI-thread service used for simulated lock operations.
     */
    explicit RemoteControlTransport(ScreenLockService* _screenLockService);

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
