#pragma once

#include "common/Protocol.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * @brief Supplies host operations required by the protocol dispatcher.
 *
 * Implementations may be called from IOCP or bounded worker threads and must therefore marshal
 * GUI-only operations to the GUI thread internally.
 */
class RemoteControlHostServices
{
public:
    /** @brief Allows derived host-service implementations to be destroyed polymorphically. */
    virtual ~RemoteControlHostServices() = default;

    /**
     * @brief Returns file-system roots that remote clients may browse.
     * @return Display-ready local drive roots.
     */
    [[nodiscard]] virtual QStringList localDriveRoots() const = 0;

    /**
     * @brief Checks whether a remote file command may access a path.
     * @param _path Path supplied by the remote client.
     * @return true when the path is permitted; otherwise false.
     */
    [[nodiscard]] virtual bool isFilePathAllowed(QString const& _path) const = 0;

    /**
     * @brief Opens a local file with the host operating system.
     * @param _path Existing local file path.
     * @return true when the open request succeeds; otherwise false.
     */
    [[nodiscard]] virtual bool openFile(QString const& _path) = 0;

    /**
     * @brief Injects one decoded remote mouse event into the host desktop.
     * @param _event Mouse event received from the control stream.
     * @return true when the host accepts the event; otherwise false.
     */
    [[nodiscard]] virtual bool sendMouseEvent(remote_control::MouseEventPacket const& _event) = 0;

    /**
     * @brief Captures and PNG-encodes the current host screen.
     * @return PNG bytes, or an empty array when capture fails.
     */
    [[nodiscard]] virtual QByteArray captureScreenPng() = 0;

    /**
     * @brief Requests a lock-screen state change without blocking the IOCP worker.
     * @param _locked true to lock the screen; false to unlock it.
     * @return true when the request is accepted; otherwise false.
     */
    [[nodiscard]] virtual bool requestScreenLock(bool _locked) = 0;
};
