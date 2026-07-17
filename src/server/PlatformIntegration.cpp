#include "server/PlatformIntegration.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QSettings>

#include <Windows.h>
#include <shellapi.h>

#include <array>

namespace
{

constexpr INT_PTR ShellExecuteSuccessThreshold{32};
constexpr std::size_t MaximumMouseInputCount{4};

DWORD mouseDownFlag(remote_control::MouseButton _button)
{
    switch (_button)
    {
        case remote_control::MouseButton::Left:
            return MOUSEEVENTF_LEFTDOWN;
        case remote_control::MouseButton::Right:
            return MOUSEEVENTF_RIGHTDOWN;
        case remote_control::MouseButton::Middle:
            return MOUSEEVENTF_MIDDLEDOWN;
        case remote_control::MouseButton::None:
            return 0;
    }
    return 0;
}

DWORD mouseUpFlag(remote_control::MouseButton _button)
{
    switch (_button)
    {
        case remote_control::MouseButton::Left:
            return MOUSEEVENTF_LEFTUP;
        case remote_control::MouseButton::Right:
            return MOUSEEVENTF_RIGHTUP;
        case remote_control::MouseButton::Middle:
            return MOUSEEVENTF_MIDDLEUP;
        case remote_control::MouseButton::None:
            return 0;
    }
    return 0;
}

/**
 * @brief Quotes one Windows command-line argument and escapes embedded quotes.
 * @param _value Argument value to quote.
 * @return Quoted command-line argument.
 */
QString quoteArgument(QString const& _value)
{
    QString escaped{_value};
    escaped.replace('"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

}  // namespace

bool PlatformIntegration::sendGlobalMouseEvent(QPoint const& _position,
                                               remote_control::MouseAction _action,
                                               remote_control::MouseButton _button)
{
    if (_button == remote_control::MouseButton::None)
    {
        if (_action != remote_control::MouseAction::Click)
        {
            return false;
        }
        QCursor::setPos(_position);
        return true;
    }

    DWORD const downFlag{mouseDownFlag(_button)};
    DWORD const upFlag{mouseUpFlag(_button)};
    if (downFlag == 0 || upFlag == 0)
    {
        return false;
    }

    std::array<DWORD, MaximumMouseInputCount> flags{};
    std::size_t flagCount{0};
    auto const appendClick{[&flags, &flagCount, downFlag, upFlag] {
        flags[flagCount++] = downFlag;
        flags[flagCount++] = upFlag;
    }};

    switch (_action)
    {
        case remote_control::MouseAction::Click:
            appendClick();
            break;
        case remote_control::MouseAction::DoubleClick:
            appendClick();
            appendClick();
            break;
        case remote_control::MouseAction::Press:
            flags[flagCount++] = downFlag;
            break;
        case remote_control::MouseAction::Release:
            flags[flagCount++] = upFlag;
            break;
    }

    std::array<INPUT, MaximumMouseInputCount> inputs{};
    for (std::size_t index{0}; index < flagCount; ++index)
    {
        inputs[index].type = INPUT_MOUSE;
        inputs[index].mi.dwFlags = flags[index];
    }

    QCursor::setPos(_position);
    UINT const requestedInputCount{static_cast<UINT>(flagCount)};
    UINT const sentInputCount{
        SendInput(requestedInputCount, inputs.data(), static_cast<int>(sizeof(INPUT)))};
    return sentInputCount == requestedInputCount;
}

void PlatformIntegration::setSystemUiLocked(bool _locked)
{
    if (_locked)
    {
        QApplication::setOverrideCursor(Qt::BlankCursor);
    }
    else
    {
        QApplication::restoreOverrideCursor();
    }

    if (auto* const taskbar{FindWindowW(L"Shell_TrayWnd", nullptr)})
    {
        ShowWindow(taskbar, _locked ? SW_HIDE : SW_SHOW);
    }

    if (_locked)
    {
        QPoint const cursorPosition{QCursor::pos()};
        RECT const cursorBounds{
            cursorPosition.x(), cursorPosition.y(), cursorPosition.x() + 1, cursorPosition.y() + 1};
        ClipCursor(&cursorBounds);
    }
    else
    {
        ClipCursor(nullptr);
    }
}

bool PlatformIntegration::isRunningAsAdmin()
{
    SID_IDENTIFIER_AUTHORITY authority{SECURITY_NT_AUTHORITY};
    PSID adminGroup{nullptr};
    BOOL isAdmin{FALSE};
    if (AllocateAndInitializeSid(&authority,
                                 2,
                                 SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 &adminGroup))
    {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool PlatformIntegration::relaunchElevated(QStringList const& _arguments, QString* _errorMessage)
{
    QString const program{QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};

    QStringList quotedArguments;
    for (QString const& argument : _arguments)
    {
        quotedArguments << quoteArgument(argument);
    }
    QString const nativeArgs{quotedArguments.join(' ')};

    auto const result{ShellExecuteW(
        nullptr,
        L"runas",
        reinterpret_cast<LPCWSTR>(program.utf16()),
        nativeArgs.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(nativeArgs.utf16()),
        nullptr,
        SW_SHOWNORMAL)};

    if (reinterpret_cast<INT_PTR>(result) <= ShellExecuteSuccessThreshold)
    {
        if (_errorMessage)
        {
            *_errorMessage = QObject::tr("Failed to relaunch with administrator privileges.");
        }
        return false;
    }
    return true;
}

bool PlatformIntegration::installStartupEntry(QString* _errorMessage)
{
    QSettings settings{startupRegistryPath(), QSettings::NativeFormat};
    settings.setValue(startupValueName(), startupCommand());
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        if (_errorMessage)
        {
            *_errorMessage = QObject::tr("Failed to create the startup entry.");
        }
        return false;
    }
    return true;
}

bool PlatformIntegration::removeStartupEntry(QString* _errorMessage)
{
    QSettings settings{startupRegistryPath(), QSettings::NativeFormat};
    settings.remove(startupValueName());
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        if (_errorMessage)
        {
            *_errorMessage = QObject::tr("Failed to remove the startup entry.");
        }
        return false;
    }
    return true;
}

bool PlatformIntegration::startupEntryExists()
{
    QSettings const settings{startupRegistryPath(), QSettings::NativeFormat};
    return settings.contains(startupValueName());
}

QString PlatformIntegration::startupRegistryPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

QString PlatformIntegration::startupValueName()
{
    return QStringLiteral("RemoteControlServer");
}

QString PlatformIntegration::startupCommand()
{
    return quoteArgument(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}
