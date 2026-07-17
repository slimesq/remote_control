#include "server/PlatformIntegration.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>

#include <Windows.h>
#include <shellapi.h>

#include <array>

namespace
{

constexpr INT_PTR ShellExecuteSuccessThreshold{32};
constexpr std::size_t MaximumMouseInputCount{4};
constexpr WORD CaptureBitsPerPixel{32};

/**
 * @brief Returns the Windows press flag for a mouse button.
 * @param _button Protocol mouse button.
 * @return Corresponding Windows press flag, or zero when unsupported.
 */
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

/**
 * @brief Returns the Windows release flag for a mouse button.
 * @param _button Protocol mouse button.
 * @return Corresponding Windows release flag, or zero when unsupported.
 */
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
 * @brief Quotes and escapes a Windows command-line argument.
 * @param _value Argument value to quote.
 * @return Quoted command-line argument.
 */
QString quoteArgument(QString const& _value)
{
    QString escaped{_value};
    escaped.replace('"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

/**
 * @brief Returns the process-wide lock that serializes global mouse input.
 * @return Mutex shared by all control connections.
 */
QMutex& mouseInputMutex()
{
    static QMutex mutex;
    return mutex;
}

}  // namespace

bool PlatformIntegration::sendGlobalMouseEvent(QPoint const& _position,
                                               remote_control::MouseAction _action,
                                               remote_control::MouseButton _button)
{
    // Keep cursor positioning and input injection atomic across concurrent control connections.
    QMutexLocker const locker{&mouseInputMutex()};

    // 1. Handle move-only events without constructing native input records.
    if (_button == remote_control::MouseButton::None)
    {
        if (_action != remote_control::MouseAction::Click)
        {
            return false;
        }
        return SetCursorPos(_position.x(), _position.y()) == TRUE;
    }

    // 2. Translate the protocol action into an ordered set of Windows flags.
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

    // 3. Move the cursor before injecting the corresponding native button records.
    std::array<INPUT, MaximumMouseInputCount> inputs{};
    for (std::size_t index{0}; index < flagCount; ++index)
    {
        inputs[index].type = INPUT_MOUSE;
        inputs[index].mi.dwFlags = flags[index];
    }

    if (SetCursorPos(_position.x(), _position.y()) != TRUE)
    {
        return false;
    }
    UINT const requestedInputCount{static_cast<UINT>(flagCount)};
    UINT const sentInputCount{
        SendInput(requestedInputCount, inputs.data(), static_cast<int>(sizeof(INPUT)))};
    return sentInputCount == requestedInputCount;
}

void PlatformIntegration::setSystemUiLocked(bool _locked)
{
    // 1. Apply application and shell visibility changes before cursor confinement.
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

    // 2. Constrain or release the cursor after the visible system state is updated.
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

QImage PlatformIntegration::capturePrimaryScreen()
{
    int const width{GetSystemMetrics(SM_CXSCREEN)};
    int const height{GetSystemMetrics(SM_CYSCREEN)};
    if (width <= 0 || height <= 0)
    {
        return {};
    }

    // 1. Copy the desktop into a compatible bitmap using worker-safe Windows GDI handles.
    auto const screenDc{GetDC(nullptr)};
    if (!screenDc)
    {
        return {};
    }
    auto const memoryDc{CreateCompatibleDC(screenDc)};
    if (!memoryDc)
    {
        ReleaseDC(nullptr, screenDc);
        return {};
    }
    auto const bitmap{CreateCompatibleBitmap(screenDc, width, height)};
    if (!bitmap)
    {
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return {};
    }

    auto const previousObject{SelectObject(memoryDc, bitmap)};
    if (!previousObject || previousObject == HGDI_ERROR)
    {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return {};
    }
    BOOL const copied{BitBlt(memoryDc, 0, 0, width, height, screenDc, 0, 0, SRCCOPY | CAPTUREBLT)};

    // 2. Convert the native bitmap into an implicitly shared QImage for PNG encoding.
    QImage image{width, height, QImage::Format_RGB32};
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = CaptureBitsPerPixel;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    int const copiedRows{copied && !image.isNull() ? GetDIBits(memoryDc,
                                                               bitmap,
                                                               0,
                                                               static_cast<UINT>(height),
                                                               image.bits(),
                                                               &bitmapInfo,
                                                               DIB_RGB_COLORS)
                                                   : 0};

    SelectObject(memoryDc, previousObject);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    return copiedRows == height ? image : QImage{};
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
    // 1. Convert the executable and arguments to Windows command-line form.
    QString const program{QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};

    QStringList quotedArguments;
    for (QString const& argument : _arguments)
    {
        quotedArguments << quoteArgument(argument);
    }
    QString const nativeArgs{quotedArguments.join(' ')};

    // 2. Request elevation through the Windows "runas" verb.
    auto const result{ShellExecuteW(
        nullptr,
        L"runas",
        reinterpret_cast<LPCWSTR>(program.utf16()),
        nativeArgs.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(nativeArgs.utf16()),
        nullptr,
        SW_SHOWNORMAL)};

    // 3. Convert ShellExecuteW failure codes into the API result.
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
