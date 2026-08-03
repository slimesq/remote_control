#pragma once

#include <QObject>
#include <memory>

class ScreenLockWindow;
class QTimer;

/** @brief Coordinates the simulated screen lock and timed lock tests on the GUI thread. */
class ScreenLockService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Creates a screen-lock service and its full-screen overlay.
     * @param _parent Parent object, or nullptr.
     */
    explicit ScreenLockService(QObject* _parent = nullptr);

    /** @brief Stops active timers and releases any local screen lock. */
    ~ScreenLockService() override;

    /** @brief Activates the application-managed screen-lock overlay. */
    void lockScreen();

    /** @brief Releases the screen-lock overlay and stops an active timed lock test. */
    void unlockScreen();

    /**
     * @brief Activates the screen lock and releases it after the requested duration.
     * @param _seconds Lock duration in seconds.
     */
    void runTimedLockTest(int _seconds);

    /**
     * @brief Returns whether the application lock window is active.
     * @return true when the lock window is active; otherwise false.
     */
    [[nodiscard]] bool isScreenLocked() const noexcept;

signals:
    /**
     * @brief Reports a change in the local lock state.
     * @param _locked Current lock state.
     */
    void lockStateChanged(bool _locked);

    /** @brief Reports completion of a timed lock test. */
    void timedLockTestFinished();

private:
    /** @brief Full-screen window used for simulated locking. */
    std::unique_ptr<ScreenLockWindow> m_screenLockWindow;
    QTimer* m_lockTestTimer{nullptr};  ///< Ends an active timed lock test.
};
