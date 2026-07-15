#pragma once

#include "Packet.h"

#include <QObject>
#include <memory>

class LockWindow;

class CommandService : public QObject
{
    Q_OBJECT

public:
    explicit CommandService(QObject* _parent = nullptr);
    ~CommandService() override;

    QList<remote_control::Packet> handle(const remote_control::Packet& _request);
    void lockLocalMachine();
    void unlockLocalMachine();
    bool isLocked() const;

private:
    QList<remote_control::Packet> handleListDrives() const;
    QList<remote_control::Packet> handleListDirectory(const QByteArray& _payload) const;
    QList<remote_control::Packet> handleRunFile(const QByteArray& _payload) const;
    QList<remote_control::Packet> handleDownloadFile(const QByteArray& _payload) const;
    QList<remote_control::Packet> handleMouseEvent(const QByteArray& _payload) const;
    QList<remote_control::Packet> handleWatchScreen() const;
    QList<remote_control::Packet> handleLockMachine() const;
    QList<remote_control::Packet> handleUnlockMachine() const;
    QList<remote_control::Packet> handleDeleteFile(const QByteArray& _payload) const;
    QList<remote_control::Packet> handleTestConnection() const;

    std::unique_ptr<LockWindow> m_lockWindow;
};
