#pragma once

#include "common/Protocol.h"

#include <QObject>
#include <memory>

class ScreenLockService;
class RemoteControlTransport;
class WindowsRemoteControlHostServices;

/** @brief Adapts the Qt application layer to the Windows IOCP network server. */
class RemoteControlServer : public QObject
{
public:
    /**
     * @brief Creates a stopped remote-control server.
     * @param _parent Parent object, or nullptr.
     */
    explicit RemoteControlServer(QObject* _parent = nullptr);

    /** @brief Stops IOCP workers before server-owned Qt services are destroyed. */
    ~RemoteControlServer() override;

    /**
     * @brief Starts listening on the requested TCP port.
     * @param _port TCP port to listen on.
     * @return true when listening starts successfully; otherwise false.
     */
    [[nodiscard]] bool start(quint16 _port = remote_control::DefaultServerPort);

    /**
     * @brief Returns the TCP port currently used by the server.
     * @return Active listening port, or zero when not listening.
     */
    [[nodiscard]] quint16 listeningPort() const noexcept;

    /**
     * @brief Returns the screen-lock service shared by active sessions.
     * @return Parent-owned screen-lock service.
     */
    [[nodiscard]] ScreenLockService* screenLockService() const noexcept;

private:
    /** @brief Stops the IOCP transport once during application shutdown. */
    void shutdownTransport();

    ScreenLockService* m_screenLockService{nullptr};  ///< Parent-owned GUI screen-lock service.
    std::unique_ptr<WindowsRemoteControlHostServices>
        m_hostServices;  ///< Windows implementation of transport host operations.
    std::unique_ptr<RemoteControlTransport> m_transport;  ///< Windows IOCP network transport.
};
