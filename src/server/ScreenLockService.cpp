#include "server/ScreenLockService.h"

#include "server/ScreenLockWindow.h"

#include <QTimer>

namespace
{

constexpr int MillisecondsPerSecond{1000};

}  // namespace

ScreenLockService::ScreenLockService(QObject* _parent)
    : QObject{_parent},
      m_screenLockWindow{std::make_unique<ScreenLockWindow>()},
      m_lockTestTimer{new QTimer{this}}
{
    this->m_lockTestTimer->setSingleShot(true);
    connect(this->m_screenLockWindow.get(),
            &ScreenLockWindow::unlockRequested,
            this,
            &ScreenLockService::unlockScreen);
    connect(this->m_lockTestTimer, &QTimer::timeout, this, [this] {
        this->unlockScreen();
        emit this->timedLockTestFinished();
    });
}

ScreenLockService::~ScreenLockService()
{
    this->m_lockTestTimer->stop();
    this->m_screenLockWindow->unlockScreen();
}

void ScreenLockService::lockScreen()
{
    bool const wasLocked{this->isScreenLocked()};
    this->m_screenLockWindow->lockScreen();
    // Emit only on a real unlocked-to-locked transition, not on idempotent lock requests.
    if (!wasLocked && this->isScreenLocked())
    {
        emit this->lockStateChanged(true);
    }
}

void ScreenLockService::unlockScreen()
{
    this->m_lockTestTimer->stop();
    bool const wasLocked{this->isScreenLocked()};
    this->m_screenLockWindow->unlockScreen();
    // Emit only on a real locked-to-unlocked transition.
    if (wasLocked && !this->isScreenLocked())
    {
        emit this->lockStateChanged(false);
    }
}

void ScreenLockService::runTimedLockTest(int _seconds)
{
    this->lockScreen();
    this->m_lockTestTimer->start(qMax(1, _seconds) * MillisecondsPerSecond);
}

bool ScreenLockService::isScreenLocked() const noexcept
{
    return this->m_screenLockWindow->isScreenLocked();
}
