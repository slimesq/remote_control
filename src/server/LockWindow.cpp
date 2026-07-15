#include "LockWindow.h"

#include "ui_LockWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFocusEvent>
#include <QKeyEvent>

#include <windows.h>

LockWindow::LockWindow(QWidget* _parent)
    : QWidget(_parent)
    , m_ui(std::make_unique<Ui::LockWindow>())
{
    this->m_ui->setupUi(this);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setFocusPolicy(Qt::StrongFocus);
}

LockWindow::~LockWindow()
{
    releaseKeyboard();
    this->m_locked = false;
    this->updateSystemUi(false);
}

void LockWindow::lockMachine()
{
    if (this->m_locked) {
        raise();
        activateWindow();
        return;
    }
    this->m_locked = true;
    this->updateSystemUi(true);
    showFullScreen();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    grabKeyboard();
}

void LockWindow::unlockMachine()
{
    if (!this->m_locked) {
        return;
    }
    this->m_locked = false;
    releaseKeyboard();
    this->updateSystemUi(false);
    hide();
}

bool LockWindow::isLocked() const
{
    return this->m_locked;
}

void LockWindow::closeEvent(QCloseEvent* _event)
{
    if (this->m_locked) {
        _event->ignore();
        return;
    }
    QWidget::closeEvent(_event);
}

void LockWindow::focusOutEvent(QFocusEvent* _event)
{
    if (this->m_locked) {
        raise();
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        grabKeyboard();
    }
    QWidget::focusOutEvent(_event);
}

void LockWindow::keyPressEvent(QKeyEvent* _event)
{
    if (this->m_locked && _event->key() == Qt::Key_C && (_event->modifiers() & Qt::ControlModifier)) {
        this->unlockMachine();
        _event->accept();
        return;
    }

    QWidget::keyPressEvent(_event);
}

void LockWindow::updateSystemUi(bool _locked)
{
    if (_locked) {
        QApplication::setOverrideCursor(Qt::BlankCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }

    if (HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        ShowWindow(taskbar, _locked ? SW_HIDE : SW_SHOW);
    }

    if (_locked) {
        RECT rect { 0, 0, 1, 1 };
        ClipCursor(&rect);
    } else {
        ClipCursor(nullptr);
    }
}
