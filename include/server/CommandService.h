#pragma once

#include "Packet.h"

#include <QObject>
#include <memory>

class LockWindow;

class CommandService : public QObject
{
    Q_OBJECT

public:
    explicit CommandService(QObject* parent = nullptr);
    ~CommandService() override;

    QList<remoteqt::Packet> handle(const remoteqt::Packet& request);
    void lockLocalMachine();
    void unlockLocalMachine();
    bool isLocked() const;

private:
    QList<remoteqt::Packet> handleListDrives() const;
    QList<remoteqt::Packet> handleListDirectory(const QByteArray& payload) const;
    QList<remoteqt::Packet> handleRunFile(const QByteArray& payload) const;
    QList<remoteqt::Packet> handleDownloadFile(const QByteArray& payload) const;
    QList<remoteqt::Packet> handleMouseEvent(const QByteArray& payload) const;
    QList<remoteqt::Packet> handleWatchScreen() const;
    QList<remoteqt::Packet> handleLockMachine() const;
    QList<remoteqt::Packet> handleUnlockMachine() const;
    QList<remoteqt::Packet> handleDeleteFile(const QByteArray& payload) const;
    QList<remoteqt::Packet> handleTestConnection() const;

    std::unique_ptr<LockWindow> m_lockWindow;
};
