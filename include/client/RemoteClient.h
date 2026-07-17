#pragma once

#include "common/Packet.h"
#include "common/Protocol.h"

#include <QImage>
#include <QObject>
#include <QString>

class QThread;
class ControlConnectionWorker;
class DownloadWorker;
class PendingRequest;
class WatchConnectionWorker;

/** @brief Sends asynchronous remote-control commands and reports their results through signals. */
class RemoteClient : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Creates a remote client.
     * @param _parent Parent object, or nullptr.
     */
    explicit RemoteClient(QObject* _parent = nullptr);

    /** @brief Stops network workers and releases their thread-owned connections. */
    ~RemoteClient() override;

    /**
     * @brief Sets the host and port used by subsequent requests.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     */
    void setEndpoint(QString const& _host, quint16 _port);

    /** @brief Tests whether the configured server is reachable. */
    void testConnection();

    /** @brief Requests the remote drive list. */
    void requestDrives();

    /**
     * @brief Requests the entries in a remote directory.
     * @param _path Remote directory path.
     */
    void requestDirectory(QString const& _path);

    /**
     * @brief Requests that the server open a remote file.
     * @param _path Remote file path.
     */
    void runFile(QString const& _path);

    /**
     * @brief Requests deletion of a remote file or directory.
     * @param _path Remote file or directory path.
     */
    void deleteFile(QString const& _path);

    /**
     * @brief Downloads a remote file to a local path.
     * @param _remotePath Source path on the remote host.
     * @param _localPath Destination path on the local host.
     */
    void downloadFile(QString const& _remotePath, QString const& _localPath);

    /** @brief Requests one remote screen frame. */
    void requestWatchFrame();

    /** @brief Closes the persistent remote-screen connection. */
    void stopWatchStream();

    /** @brief Closes the persistent remote-input control connection. */
    void stopControlStream();

    /**
     * @brief Sends a mouse event to the remote host.
     * @param _event Mouse event encoded in protocol coordinates.
     */
    void sendMouseEvent(remote_control::MouseEventPacket const& _event);

    /** @brief Requests that the remote screen be locked. */
    void lockRemote();

    /** @brief Requests that the remote screen be unlocked. */
    void unlockRemote();

    /**
     * @brief Returns whether a screen-frame request is currently pending.
     * @return true when a frame request is pending; otherwise false.
     */
    [[nodiscard]] bool hasPendingWatchFrame() const noexcept;

    /**
     * @brief Updates the pending screen-frame request state.
     * @param _pending Whether a frame request is pending.
     */
    void setWatchFramePending(bool _pending);

signals:
    /**
     * @brief Reports the connection-test result.
     * @param _success Whether the connection test succeeded.
     * @param _message User-facing result message.
     */
    void connectionTested(bool _success, QString const& _message);

    /**
     * @brief Reports the available remote drives.
     * @param _drives Remote drive identifiers.
     */
    void drivesListed(QStringList const& _drives);

    /**
     * @brief Reports a completed remote directory listing.
     * @param _path Listed remote directory path.
     * @param _entries Entries returned for the directory.
     */
    void directoryListed(QString const& _path, QList<remote_control::FileEntry> const& _entries);

    /**
     * @brief Reports a successfully completed command.
     * @param _command Completed command.
     * @param _context Command-specific path or label.
     * @param _message User-facing result message.
     */
    void commandCompleted(remote_control::Command _command,
                          QString const& _context,
                          QString const& _message);

    /**
     * @brief Reports received bytes for an active download.
     * @param _remotePath Downloaded remote path.
     * @param _received Number of bytes received.
     * @param _total Expected total byte count.
     */
    void downloadProgress(QString const& _remotePath, qint64 _received, qint64 _total);

    /**
     * @brief Reports the final result of a download.
     * @param _remotePath Downloaded remote path.
     * @param _localPath Local destination path.
     * @param _success Whether the download succeeded.
     * @param _message User-facing result message.
     */
    void downloadFinished(QString const& _remotePath,
                          QString const& _localPath,
                          bool _success,
                          QString const& _message);

    /**
     * @brief Delivers a decoded remote screen frame.
     * @param _image Decoded remote screen image.
     */
    void watchFrameReady(QImage const& _image);

    /**
     * @brief Reports a failed remote request.
     * @param _command Failed command.
     * @param _context Command-specific path or label.
     * @param _message User-facing failure message.
     */
    void requestFailed(remote_control::Command _command,
                       QString const& _context,
                       QString const& _message);

private:
    friend class PendingRequest;

    /**
     * @brief Checks whether an asynchronous result belongs to the active endpoint.
     * @param _generation Endpoint generation captured when the request started.
     * @return true when the result is current; otherwise false.
     */
    [[nodiscard]] bool isEndpointGenerationCurrent(quint64 _generation) const noexcept;

    QString m_host{QStringLiteral("127.0.0.1")};
    quint16 m_port{remote_control::DefaultServerPort};
    QThread* m_watchThread{nullptr};
    WatchConnectionWorker* m_watchWorker{nullptr};
    QThread* m_controlThread{nullptr};
    ControlConnectionWorker* m_controlWorker{nullptr};
    QThread* m_downloadThread{nullptr};
    DownloadWorker* m_downloadWorker{nullptr};
    bool m_watchPending{false};
    quint64 m_endpointGeneration{0};
    quint64 m_watchGeneration{0};
    quint64 m_controlGeneration{0};
};
