#include "server/internal/WindowsRemoteControlHostServices.h"

#include "server/ScreenLockService.h"
#include "server/WindowsPlatformIntegration.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPoint>

WindowsRemoteControlHostServices::WindowsRemoteControlHostServices(
    ScreenLockService& _screenLockService)
    : m_screenLockService{_screenLockService}
{
}

QStringList WindowsRemoteControlHostServices::localDriveRoots() const
{
    QStringList drives;
    for (QFileInfo const& drive : QDir::drives())
    {
        QString drivePath{QDir::toNativeSeparators(drive.absoluteFilePath())};
        if (!WindowsPlatformIntegration::isLocalFilePath(drivePath))
        {
            continue;
        }
        while (drivePath.endsWith(QDir::separator()))
        {
            drivePath.chop(1);
        }
        if (!drivePath.isEmpty())
        {
            drives.append(drivePath);
        }
    }
    return drives;
}

bool WindowsRemoteControlHostServices::isFilePathAllowed(QString const& _path) const
{
    return WindowsPlatformIntegration::isLocalFilePath(_path);
}

bool WindowsRemoteControlHostServices::openFile(QString const& _path)
{
    return WindowsPlatformIntegration::openLocalFile(_path);
}

bool WindowsRemoteControlHostServices::sendMouseEvent(
    remote_control::MouseEventPacket const& _event)
{
    return WindowsPlatformIntegration::sendGlobalMouseEvent(
        QPoint{_event.x, _event.y},
        static_cast<remote_control::MouseAction>(_event.action),
        static_cast<remote_control::MouseButton>(_event.button));
}

QByteArray WindowsRemoteControlHostServices::captureScreenPng()
{
    return WindowsPlatformIntegration::capturePrimaryScreenPng();
}

bool WindowsRemoteControlHostServices::requestScreenLock(bool _locked)
{
    ScreenLockService* const service{&this->m_screenLockService};

    // ScreenLockService owns widgets, so execute the state change on its GUI thread.
    return QMetaObject::invokeMethod(
        service,
        [service, _locked] {
            if (_locked)
            {
                service->lockScreen();
            }
            else
            {
                service->unlockScreen();
            }
        },
        Qt::QueuedConnection);
}
