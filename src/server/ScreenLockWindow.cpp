#include "server/ScreenLockWindow.h"

#include "server/WindowsPlatformIntegration.h"
#include "ui_ScreenLockWindow.h"

#include <QCloseEvent>
#include <QFocusEvent>
#include <QKeyEvent>

ScreenLockWindow::ScreenLockWindow(QWidget* _parent)
    : QWidget{_parent}, m_ui{std::make_unique<Ui::ScreenLockWindow>()}
{
    this->m_ui->setupUi(this);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setFocusPolicy(Qt::StrongFocus);
}

ScreenLockWindow::~ScreenLockWindow()
{
    if (this->m_locked)
    {
        releaseKeyboard();
        this->m_locked = false;
        static_cast<void>(WindowsPlatformIntegration::setSystemUiLocked(false));
    }
}

void ScreenLockWindow::lockScreen()
{
    if (this->m_locked)
    {
        raise();
        activateWindow();
        return;
    }
    if (!WindowsPlatformIntegration::setSystemUiLocked(true))
    {
        return;
    }
    this->m_locked = true;
    showFullScreen();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    grabKeyboard();
}

void ScreenLockWindow::unlockScreen()
{
    if (!this->m_locked)
    {
        return;
    }
    this->m_locked = false;
    releaseKeyboard();
    static_cast<void>(WindowsPlatformIntegration::setSystemUiLocked(false));
    hide();
}

bool ScreenLockWindow::isScreenLocked() const noexcept
{
    return this->m_locked;
}

void ScreenLockWindow::closeEvent(QCloseEvent* _event)
{
    if (this->m_locked)
    {
        _event->ignore();
        return;
    }
    QWidget::closeEvent(_event);
}

void ScreenLockWindow::focusOutEvent(QFocusEvent* _event)
{
    if (this->m_locked)
    {
        raise();
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        grabKeyboard();
    }
    QWidget::focusOutEvent(_event);
}

void ScreenLockWindow::keyPressEvent(QKeyEvent* _event)
{
    // The local recovery shortcut is active only while this window owns the lock.
    if (this->m_locked && _event->key() == Qt::Key_C && (_event->modifiers() & Qt::ControlModifier))
    {
        // Route recovery through ScreenLockService so its timer and public lock state stay synchronized.
        emit this->unlockRequested();
        _event->accept();
        return;
    }

    QWidget::keyPressEvent(_event);
}
