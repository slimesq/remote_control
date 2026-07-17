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
    /**
     * @brief Creates a command service and its lock-screen controller.
     * @param _parent Parent object, or nullptr.
     */
    explicit CommandService(QObject* _parent = nullptr);

    /** @brief Stops active timers and releases any local screen lock. */
    ~CommandService() override;

    /**
     * @brief Executes one request and returns its response packets.
     * @param _request Request packet to execute.
     * @return Response packets for the request.
     */
    [[nodiscard]] QList<remote_control::Packet> handle(remote_control::Packet const& _request);

    /** @brief Locks the local machine through the application lock window. */
    void lockLocalMachine();

    /** @brief Unlocks the local machine and stops an active timed lock test. */
    void unlockLocalMachine();

    /**
     * @brief Locks the local machine and unlocks it after the requested duration.
     * @param _seconds Lock duration in seconds.
     */
    void runTimedLockTest(int _seconds);

    /**
     * @brief Returns whether the application lock window is active.
     * @return true when the lock window is active; otherwise false.
     */
    [[nodiscard]] bool isLocked() const noexcept;

signals:
    /**
     * @brief Reports a change in the local lock state.
     * @param _locked Current lock state.
     */
    void lockStateChanged(bool _locked);

    /** @brief Reports completion of a timed lock test. */
    void timedLockTestFinished();

private:
    /**
     * @brief Builds the local drive-list response.
     * @return Packet containing available local drives.
     */
    [[nodiscard]] QList<remote_control::Packet> handleListDrives() const;

    /**
     * @brief Opens the requested local file.
     * @param _payload UTF-8 encoded file path.
     * @return Command-status packet.
     */
    [[nodiscard]] QList<remote_control::Packet> handleRunFile(QByteArray const& _payload) const;

    /**
     * @brief Builds a connection-test response.
     * @return Connection-test response packet.
     */
    [[nodiscard]] QList<remote_control::Packet> handleTestConnection() const;

    std::unique_ptr<LockWindow> m_lockWindow;
    QTimer* m_lockTestTimer{nullptr};
};
