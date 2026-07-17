#pragma once

#include <QWidget>
#include <memory>

class QKeyEvent;

namespace Ui
{
class LockWindow;
}

/** @brief Provides the full-screen Windows lock overlay managed by the server. */
class LockWindow : public QWidget
{
    Q_OBJECT

public:
    /** @brief Creates a lock overlay. */
    explicit LockWindow(QWidget* _parent = nullptr);

    /** @brief Restores system UI state before destroying the overlay. */
    ~LockWindow() override;

    /** @brief Shows the lock overlay and hides supported Windows shell UI. */
    void lockMachine();

    /** @brief Hides the lock overlay and restores supported Windows shell UI. */
    void unlockMachine();

    /** @brief Returns whether the overlay is currently locked. */
    [[nodiscard]] bool isLocked() const noexcept;

protected:
    void closeEvent(QCloseEvent* _event) override;
    void focusOutEvent(QFocusEvent* _event) override;
    void keyPressEvent(QKeyEvent* _event) override;

private:
    std::unique_ptr<Ui::LockWindow> m_ui;
    bool m_locked{false};
};
