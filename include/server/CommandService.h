#pragma once

#include "common/Packet.h"

#include <QObject>
#include <memory>

class LockWindow;
class QTimer;

/** @brief Executes decoded remote-control commands on the local Windows host. */
class CommandService : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates a command service and its lock-screen controller. */
    explicit CommandService(QObject* _parent = nullptr);

    /** @brief Stops active timers and releases any local screen lock. */
    ~CommandService() override;

    /** @brief Executes one request and returns its response packets. */
    [[nodiscard]] QList<remote_control::Packet> handle(remote_control::Packet const& _request);

    /** @brief Locks the local machine through the application lock window. */
    void lockLocalMachine();

    /** @brief Unlocks the local machine and stops an active timed lock test. */
    void unlockLocalMachine();

    /** @brief Locks the local machine and unlocks it after the requested duration. */
    void runTimedLockTest(int _seconds);

    /** @brief Returns whether the application lock window is active. */
    [[nodiscard]] bool isLocked() const noexcept;

signals:
    void lockStateChanged(bool _locked);
    void timedLockTestFinished();

private:
    [[nodiscard]] QList<remote_control::Packet> handleListDrives() const;
    [[nodiscard]] QList<remote_control::Packet> handleListDirectory(
        QByteArray const& _payload) const;
    [[nodiscard]] QList<remote_control::Packet> handleRunFile(QByteArray const& _payload) const;
    [[nodiscard]] QList<remote_control::Packet> handleDownloadFile(
        QByteArray const& _payload) const;
    [[nodiscard]] QList<remote_control::Packet> handleMouseEvent(QByteArray const& _payload) const;
    [[nodiscard]] QList<remote_control::Packet> handleWatchScreen() const;
    [[nodiscard]] QList<remote_control::Packet> handleLockMachine();
    [[nodiscard]] QList<remote_control::Packet> handleUnlockMachine();
    [[nodiscard]] QList<remote_control::Packet> handleDeleteFile(QByteArray const& _payload) const;
    [[nodiscard]] QList<remote_control::Packet> handleTestConnection() const;

    std::unique_ptr<LockWindow> m_lockWindow;
    QTimer* m_lockTestTimer{nullptr};
};
