#pragma once

#include "RemoteControlHostServices.h"

class ScreenLockService;

/** @brief Adapts Windows desktop services to the IOCP transport host contract. */
class WindowsRemoteControlHostServices final : public RemoteControlHostServices
{
public:
    /**
     * @brief Creates an adapter for the GUI-thread screen-lock service.
     * @param _screenLockService Screen-lock service owned by RemoteControlServer.
     */
    explicit WindowsRemoteControlHostServices(ScreenLockService& _screenLockService);

    /**
     * @brief Returns supported Windows drive roots.
     * @return Display-ready local drive roots.
     */
    [[nodiscard]] QStringList localDriveRoots() const override;

    /**
     * @brief Checks whether a path belongs to a supported Windows drive.
     * @param _path Path supplied by a remote client.
     * @return true for supported local paths; otherwise false.
     */
    [[nodiscard]] bool isFilePathAllowed(QString const& _path) const override;

    /**
     * @brief Opens a file through its Windows shell association.
     * @param _path Existing local file path.
     * @return true when Windows accepts the request; otherwise false.
     */
    [[nodiscard]] bool openFile(QString const& _path) override;

    /**
     * @brief Converts and injects one protocol mouse event.
     * @param _event Mouse event received from the control stream.
     * @return true when Windows accepts the event; otherwise false.
     */
    [[nodiscard]] bool sendMouseEvent(remote_control::MouseEventPacket const& _event) override;

    /**
     * @brief Captures the primary Windows screen.
     * @return PNG bytes, or an empty array when capture fails.
     */
    [[nodiscard]] QByteArray captureScreenPng() override;

    /**
     * @brief Queues a screen-lock state change on the GUI thread.
     * @param _locked true to lock the screen; false to unlock it.
     * @return true when Qt accepts the queued invocation; otherwise false.
     */
    [[nodiscard]] bool requestScreenLock(bool _locked) override;

private:
    ScreenLockService& m_screenLockService;  ///< GUI-thread lock service.
};
