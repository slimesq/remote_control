#pragma once

#include <QWidget>
#include <memory>

class QKeyEvent;

namespace Ui
{
class ScreenLockWindow;
}

/** @brief Provides the full-screen Windows lock overlay managed by the server. */
class ScreenLockWindow : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Creates a lock overlay.
     * @param _parent Parent widget, or nullptr.
     */
    explicit ScreenLockWindow(QWidget* _parent = nullptr);

    /** @brief Restores system UI state before destroying the overlay. */
    ~ScreenLockWindow() override;

    /** @brief Shows the lock overlay and hides supported Windows shell UI. */
    void lockScreen();

    /** @brief Hides the lock overlay and restores supported Windows shell UI. */
    void unlockScreen();

    /**
     * @brief Returns whether the overlay is currently locked.
     * @return true when the overlay is locked; otherwise false.
     */
    [[nodiscard]] bool isScreenLocked() const noexcept;

signals:
    /** @brief Requests a coordinated unlock after the local recovery shortcut is pressed. */
    void unlockRequested();

protected:
    /**
     * @brief Ignores close requests while the overlay is locked.
     * @param _event Close event supplied by Qt.
     */
    void closeEvent(QCloseEvent* _event) override;

    /**
     * @brief Restores focus while the overlay is locked.
     * @param _event Focus event supplied by Qt.
     */
    void focusOutEvent(QFocusEvent* _event) override;

    /**
     * @brief Blocks keyboard input while the overlay is locked.
     * @param _event Key event supplied by Qt.
     */
    void keyPressEvent(QKeyEvent* _event) override;

private:
    std::unique_ptr<Ui::ScreenLockWindow> m_ui;  ///< Generated lock-window user interface.
    bool m_locked{false};  ///< Whether the simulated lock is currently active.
};
