#pragma once

#include "common/Protocol.h"

#include <QImage>
#include <QObject>
#include <QString>

class QThread;
class ControlStreamWorker;
class FileDownloadWorker;
class OneShotRequest;
class ScreenStreamWorker;

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
    void requestDriveList();

    /**
     * @brief Requests the entries in a remote directory.
     * @param _path Remote directory path.
     */
    void requestDirectoryListing(QString const& _path);

    /**
     * @brief Requests that the server open a remote file.
     * @param _path Remote file path.
     */
    void openRemoteFile(QString const& _path);

    /**
     * @brief Requests deletion of a remote file or directory.
     * @param _path Remote file or directory path.
     */
    void deleteRemotePath(QString const& _path);

    /**
     * @brief Downloads a remote file to a local path.
     * @param _remotePath Source path on the remote host.
     * @param _localPath Destination path on the local host.
     */
    void downloadRemoteFile(QString const& _remotePath, QString const& _localPath);

    /** @brief Requests one remote screen frame. */
    void requestScreenFrame();

    /** @brief Closes the persistent remote-screen connection. */
    void stopScreenStream();

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
    [[nodiscard]] bool hasPendingScreenFrame() const noexcept;

    /**
     * @brief Updates the pending screen-frame request state.
     * @param _pending Whether a frame request is pending.
     */
    void setScreenFramePending(bool _pending);

signals:
    /**
     * @brief Reports the final result of Command::TestConnection.
     * @param _success Whether the connection test succeeded.
     * @param _message User-facing result message.
     */
    void connectionTested(bool _success, QString const& _message);

    /**
     * @brief Reports the final result of Command::ListDrives.
     * @param _drives Remote drive identifiers, or an empty list on failure.
     * @param _success Whether the request succeeded.
     * @param _message User-facing result message.
     */
    void driveListFinished(QStringList const& _drives, bool _success, QString const& _message);

    /**
     * @brief Reports the final result of Command::ListDirectory.
     * @param _path Listed remote directory path.
     * @param _entries Entries returned for the directory, or an empty list on failure.
     * @param _success Whether the request succeeded.
     * @param _message User-facing result message.
     */
    void directoryListFinished(QString const& _path,
                               QList<remote_control::FileEntry> const& _entries,
                               bool _success,
                               QString const& _message);

    /**
     * @brief Reports the final result of Command::RunFile or Command::DeleteFile.
     * @param _command Completed file command.
     * @param _path Remote file path associated with the command.
     * @param _success Whether the command succeeded.
     * @param _message User-facing result message.
     */
    void remotePathCommandFinished(remote_control::Command _command,
                                   QString const& _path,
                                   bool _success,
                                   QString const& _message);

    /**
     * @brief Reports the final result of Command::MouseEvent, Command::LockMachine, or
     * Command::UnlockMachine.
     * @param _command Completed control command.
     * @param _context Command-specific label.
     * @param _success Whether the command succeeded.
     * @param _message User-facing result message.
     */
    void controlCommandFinished(remote_control::Command _command,
                                QString const& _context,
                                bool _success,
                                QString const& _message);

    /**
     * @brief Reports received bytes for an active Command::DownloadFile request.
     * @param _remotePath Downloaded remote path.
     * @param _received Number of bytes received.
     * @param _total Expected total byte count.
     */
    void downloadProgress(QString const& _remotePath, qint64 _received, qint64 _total);

    /**
     * @brief Reports the final result of Command::DownloadFile.
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
     * @brief Delivers a decoded frame returned by Command::WatchScreen.
     * @param _image Decoded remote screen image.
     */
    void screenFrameReady(QImage const& _image);

    /** @brief Reports that the current screen-frame request has finished. */
    void screenFrameRequestFinished();

    /**
     * @brief Reports a connection or frame-request failure for Command::WatchScreen.
     * @param _message User-facing failure message.
     */
    void screenStreamFailed(QString const& _message);

private:
    friend class OneShotRequest;

    /**
     * @brief Checks whether an asynchronous result belongs to the active endpoint.
     * @param _generation Endpoint generation captured when the request started.
     * @return true when the result is current; otherwise false.
     */
    [[nodiscard]] bool isEndpointGenerationCurrent(quint64 _generation) const noexcept;

    QString m_host{QStringLiteral("127.0.0.1")};        ///< Configured server host name or address.
    quint16 m_port{remote_control::DefaultServerPort};  ///< Configured server TCP port.
    QThread* m_screenStreamThread{nullptr};             ///< Thread for remote-screen network I/O.
    ScreenStreamWorker* m_screenStreamWorker{nullptr};  ///< Persistent screen-stream worker.
    QThread* m_controlStreamThread{nullptr};            ///< Thread for remote-control network I/O.
    ControlStreamWorker* m_controlStreamWorker{nullptr};  ///< Persistent input-control worker.
    QThread* m_fileDownloadThread{nullptr};               ///< Thread for file-download network I/O.
    FileDownloadWorker* m_fileDownloadWorker{nullptr};    ///< Worker that performs one download.
    bool m_screenFramePending{false};      ///< Whether one frame request is awaiting completion.
    quint64 m_endpointGeneration{0};       ///< Discards results from obsolete endpoints.
    quint64 m_screenStreamGeneration{0};   ///< Discards results from stopped screen streams.
    quint64 m_controlStreamGeneration{0};  ///< Discards results from stopped control sessions.
};
