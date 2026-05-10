#include "LockWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QVBoxLayout>

#include <windows.h>

LockWindow::LockWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setStyleSheet(QStringLiteral("background-color: black; color: white;"));

    auto* layout = new QVBoxLayout(this);
    m_label = new QLabel(tr("Please contact the administrator to unlock this machine."), this);
    QFont font = m_label->font();
    font.setPointSize(22);
    font.setBold(true);
    m_label->setFont(font);
    m_label->setAlignment(Qt::AlignCenter);
    layout->addStretch();
    layout->addWidget(m_label);
    layout->addStretch();
}

LockWindow::~LockWindow()
{
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
}

void LockWindow::unlockMachine()
{
    if (!m_locked) {
        return;
    }
    m_locked = false;
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
