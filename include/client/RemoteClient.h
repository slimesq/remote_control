#pragma once

#include "common/Packet.h"
#include "common/Protocol.h"

#include <QImage>
#include <QObject>
#include <QString>

/** @brief Sends asynchronous remote-control commands and reports their results through signals. */
class RemoteClient : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates a remote client. */
    explicit RemoteClient(QObject* _parent = nullptr);

    /** @brief Sets the host and port used by subsequent requests. */
    void setEndpoint(QString const& _host, quint16 _port);

    /** @brief Tests whether the configured server is reachable. */
    void testConnection();

    /** @brief Requests the remote drive list. */
    void requestDrives();

    /** @brief Requests the entries in a remote directory. */
    void requestDirectory(QString const& _path);

    /** @brief Requests that the server open a remote file. */
    void runFile(QString const& _path);

    /** @brief Requests deletion of a remote file or directory. */
    void deleteFile(QString const& _path);

    /** @brief Downloads a remote file to a local path. */
    void downloadFile(QString const& _remotePath, QString const& _localPath);

    /** @brief Requests one remote screen frame. */
    void requestWatchFrame();

    /** @brief Sends a mouse event to the remote host. */
    void sendMouseEvent(remote_control::MouseEventPacket const& _event);

    /** @brief Requests that the remote screen be locked. */
    void lockRemote();

    /** @brief Requests that the remote screen be unlocked. */
    void unlockRemote();

    /** @brief Returns whether a screen-frame request is currently pending. */
    [[nodiscard]] bool hasPendingWatchFrame() const noexcept;

    /** @brief Updates the pending screen-frame request state. */
    void setWatchFramePending(bool _pending);

signals:
    void connectionTested(bool _success, QString const& _message);
    void drivesListed(QStringList const& _drives);
    void directoryListed(QString const& _path, QList<remote_control::FileEntry> const& _entries);
    void commandCompleted(remote_control::Command _command,
                          QString const& _context,
                          QString const& _message);
    void downloadProgress(QString const& _remotePath, qint64 _received, qint64 _total);
    void downloadFinished(QString const& _remotePath,
                          QString const& _localPath,
                          bool _success,
                          QString const& _message);
    void watchFrameReady(QImage const& _image);
    void requestFailed(remote_control::Command _command,
                       QString const& _context,
                       QString const& _message);

private:
    QString m_host{QStringLiteral("127.0.0.1")};
    quint16 m_port{remote_control::DefaultServerPort};
    bool m_watchPending{false};
};
