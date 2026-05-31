#include "LockWindow.h"

#include "ui_LockWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFocusEvent>
#include <QKeyEvent>

#include <windows.h>

LockWindow::LockWindow(QWidget* parent)
    : QWidget(parent)
    , m_ui(std::make_unique<Ui::LockWindow>())
{
    m_ui->setupUi(this);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setFocusPolicy(Qt::StrongFocus);
}

LockWindow::~LockWindow()
{
    releaseKeyboard();
    m_locked = false;
    updateSystemUi(false);
}

void LockWindow::lockMachine()
{
    if (m_locked) {
        raise();
        activateWindow();
        return;
    }
    m_locked = true;
    updateSystemUi(true);
    showFullScreen();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    grabKeyboard();
}

void LockWindow::unlockMachine()
{
    if (!m_locked) {
        return;
    }
    m_locked = false;
    releaseKeyboard();
    updateSystemUi(false);
    hide();
}

bool LockWindow::isLocked() const
{
    return m_locked;
}

void LockWindow::closeEvent(QCloseEvent* event)
{
    if (m_locked) {
        event->ignore();
        return;
    }
    QWidget::closeEvent(event);
}

void LockWindow::focusOutEvent(QFocusEvent* event)
{
    if (m_locked) {
        raise();
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        grabKeyboard();
    }
    QWidget::focusOutEvent(event);
}

void LockWindow::keyPressEvent(QKeyEvent* event)
{
    if (m_locked && event->key() == Qt::Key_C && (event->modifiers() & Qt::ControlModifier)) {
        unlockMachine();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void LockWindow::updateSystemUi(bool locked)
{
    if (locked) {
        QApplication::setOverrideCursor(Qt::BlankCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }

    if (HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        ShowWindow(taskbar, locked ? SW_HIDE : SW_SHOW);
    }

    if (locked) {
        RECT rect { 0, 0, 1, 1 };
        ClipCursor(&rect);
    } else {
        ClipCursor(nullptr);
    }
}
