#pragma once

#include "../common/Packet.h"
#include "../common/Protocol.h"

#include <QImage>
#include <QObject>
#include <QString>

class RemoteClient : public QObject
{
    Q_OBJECT

public:
    explicit RemoteClient(QObject* parent = nullptr);

    void setEndpoint(const QString& host, quint16 port);

    void testConnection();
    void requestDrives();
    void requestDirectory(const QString& path);
    void runFile(const QString& path);
    void deleteFile(const QString& path);
    void downloadFile(const QString& remotePath, const QString& localPath);
    void requestWatchFrame();
    void sendMouseEvent(const remoteqt::MouseEventPacket& event);
    void lockRemote();
    void unlockRemote();

signals:
    void connectionTested(bool success, const QString& message);
    void drivesListed(const QStringList& drives);
    void directoryListed(const QString& path, const QList<remoteqt::FileEntry>& entries);
    void commandCompleted(remoteqt::Command command, const QString& context, const QString& message);
    void downloadProgress(const QString& remotePath, qint64 received, qint64 total);
    void downloadFinished(const QString& remotePath, const QString& localPath, bool success, const QString& message);
    void watchFrameReady(const QImage& image);
    void requestFailed(remoteqt::Command command, const QString& context, const QString& message);

private:
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 9527;
    bool m_watchPending = false;
};
