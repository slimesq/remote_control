#pragma once

#include "Packet.h"
#include "Protocol.h"

#include <QImage>
#include <QObject>
#include <QString>

class RemoteClient : public QObject
{
    Q_OBJECT

public:
    explicit RemoteClient(QObject* _parent = nullptr);

    void setEndpoint(const QString& _host, quint16 _port);

    void testConnection();
    void requestDrives();
    void requestDirectory(const QString& _path);
    void runFile(const QString& _path);
    void deleteFile(const QString& _path);
    void downloadFile(const QString& _remotePath, const QString& _localPath);
    void requestWatchFrame();
    void sendMouseEvent(const remote_control::MouseEventPacket& _event);
    void lockRemote();
    void unlockRemote();
    bool hasPendingWatchFrame() const;
    void setWatchFramePending(bool _pending);

signals:
    void connectionTested(bool _success, const QString& _message);
    void drivesListed(const QStringList& _drives);
    void directoryListed(const QString& _path, const QList<remote_control::FileEntry>& _entries);
    void commandCompleted(remote_control::Command _command, const QString& _context, const QString& _message);
    void downloadProgress(const QString& _remotePath, qint64 _received, qint64 _total);
    void downloadFinished(const QString& _remotePath, const QString& _localPath, bool _success, const QString& _message);
    void watchFrameReady(const QImage& _image);
    void requestFailed(remote_control::Command _command, const QString& _context, const QString& _message);

private:
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 9527;
    bool m_watchPending = false;
};
