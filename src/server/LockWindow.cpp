#include "server/LockWindow.h"

#include "server/PlatformIntegration.h"
#include "ui_LockWindow.h"

#include <QCloseEvent>
#include <QFocusEvent>
#include <QKeyEvent>

LockWindow::LockWindow(QWidget* _parent)
    : QWidget{_parent}, m_ui{std::make_unique<Ui::LockWindow>()}
{
    this->m_ui->setupUi(this);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setFocusPolicy(Qt::StrongFocus);
}

LockWindow::~LockWindow()
{
    if (this->m_locked)
    {
        releaseKeyboard();
        this->m_locked = false;
        PlatformIntegration::setSystemUiLocked(false);
    }
}

void LockWindow::lockMachine()
{
    if (this->m_locked)
    {
        raise();
        activateWindow();
        return;
    }
    this->m_locked = true;
    PlatformIntegration::setSystemUiLocked(true);
    showFullScreen();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    grabKeyboard();
}

void LockWindow::unlockMachine()
{
    if (!this->m_locked)
    {
        return;
    }
    this->m_locked = false;
    releaseKeyboard();
    PlatformIntegration::setSystemUiLocked(false);
    hide();
}

bool LockWindow::isLocked() const noexcept
{
    return this->m_locked;
}

void LockWindow::closeEvent(QCloseEvent* _event)
{
    if (this->m_locked)
    {
        _event->ignore();
        return;
    }
    QWidget::closeEvent(_event);
}

void LockWindow::focusOutEvent(QFocusEvent* _event)
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

void LockWindow::keyPressEvent(QKeyEvent* _event)
{
    if (this->m_locked && _event->key() == Qt::Key_C && (_event->modifiers() & Qt::ControlModifier))
    {
        this->unlockMachine();
        _event->accept();
        return;
    }

    QWidget::keyPressEvent(_event);
}
