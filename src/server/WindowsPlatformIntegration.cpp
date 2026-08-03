#include "server/WindowsPlatformIntegration.h"

#include <QApplication>
#include <QBuffer>
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

/** @brief Stores the desktop state temporarily replaced by the simulated lock. */
struct SystemUiLockState final
{
    bool active{false};             ///< Whether this process currently owns the replacement state.
    bool taskbarFound{false};       ///< Whether the taskbar existed when locking began.
    bool taskbarWasVisible{false};  ///< Original taskbar visibility.
    bool cursorWasClipped{false};   ///< Whether another cursor restriction was already active.
    RECT cursorBounds{};            ///< Original cursor clipping rectangle.
};

/**
 * @brief Returns the process-wide simulated-lock state.
 * @return State restored by the matching unlock operation.
 */
SystemUiLockState& systemUiLockState()
{
    static SystemUiLockState state;
    return state;
}

/**
 * @brief Returns the complete Windows virtual-desktop rectangle.
 * @return Bounding rectangle containing all configured monitors.
 */
RECT virtualDesktopBounds() noexcept
{
    int const left{GetSystemMetrics(SM_XVIRTUALSCREEN)};
    int const top{GetSystemMetrics(SM_YVIRTUALSCREEN)};
    return {left,
            top,
            left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            top + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

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

/** @brief Reuses one worker thread's GDI bitmap and memory DC across screen captures. */
class ScreenCaptureContext final
{
public:
    /** @brief Releases the selected bitmap and its compatible memory DC. */
    ~ScreenCaptureContext()
    {
        this->reset();
    }

    ScreenCaptureContext() = default;
    ScreenCaptureContext(ScreenCaptureContext const&) = delete;
    ScreenCaptureContext(ScreenCaptureContext&&) = delete;
    ScreenCaptureContext& operator=(ScreenCaptureContext const&) = delete;
    ScreenCaptureContext& operator=(ScreenCaptureContext&&) = delete;

    /**
     * @brief Captures and encodes the primary desktop while the DIB storage remains valid.
     * @return Encoded PNG bytes, or an empty array when capture or encoding fails.
     */
    [[nodiscard]] QByteArray capturePng()
    {
        int const width{GetSystemMetrics(SM_CXSCREEN)};
        int const height{GetSystemMetrics(SM_CYSCREEN)};
        if (width <= 0 || height <= 0)
        {
            return {};
        }

        auto const screenDc{GetDC(nullptr)};
        if (!screenDc)
        {
            return {};
        }
        bool const ready{this->ensureBuffer(screenDc, width, height)};
        BOOL const copied{
            ready ? BitBlt(
                        this->m_memoryDc, 0, 0, width, height, screenDc, 0, 0, SRCCOPY | CAPTUREBLT)
                  : FALSE};
        ReleaseDC(nullptr, screenDc);
        if (!copied)
        {
            return {};
        }

        // Encode before returning so the reusable DIB never escapes this worker-thread context.
        QImage const image{static_cast<uchar*>(this->m_pixelData),
                           width,
                           height,
                           width * static_cast<int>(sizeof(quint32)),
                           QImage::Format_RGB32};
        QByteArray pngData;
        QBuffer buffer{&pngData};
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        {
            return {};
        }
        return pngData;
    }

private:
    /**
     * @brief Creates or resizes the top-down 32-bit DIB used by this worker.
     * @param _screenDc Desktop device context used for compatible allocation.
     * @param _width Required pixel width.
     * @param _height Required pixel height.
     * @return true when a reusable buffer is ready; otherwise false.
     */
    [[nodiscard]] bool ensureBuffer(HDC _screenDc, int _width, int _height)
    {
        if (this->m_memoryDc && this->m_bitmap && this->m_width == _width &&
            this->m_height == _height)
        {
            return true;
        }
        this->reset();

        auto const memoryDc{CreateCompatibleDC(_screenDc)};
        if (!memoryDc)
        {
            return false;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
        bitmapInfo.bmiHeader.biWidth = _width;
        bitmapInfo.bmiHeader.biHeight = -_height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = CaptureBitsPerPixel;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void* pixelData{nullptr};
        auto const bitmap{
            CreateDIBSection(_screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixelData, nullptr, 0)};
        if (!bitmap || !pixelData)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
            DeleteDC(memoryDc);
            return false;
        }

        auto const previousObject{SelectObject(memoryDc, bitmap)};
        if (!previousObject || previousObject == HGDI_ERROR)
        {
            DeleteObject(bitmap);
            DeleteDC(memoryDc);
            return false;
        }

        this->m_memoryDc = memoryDc;
        this->m_bitmap = bitmap;
        this->m_previousObject = previousObject;
        this->m_pixelData = pixelData;
        this->m_width = _width;
        this->m_height = _height;
        return true;
    }

    /** @brief Releases the reusable GDI capture resources in dependency order. */
    void reset() noexcept
    {
        if (this->m_memoryDc)
        {
            if (this->m_previousObject && this->m_previousObject != HGDI_ERROR)
            {
                static_cast<void>(SelectObject(this->m_memoryDc, this->m_previousObject));
            }
            DeleteDC(this->m_memoryDc);
        }
        if (this->m_bitmap)
        {
            DeleteObject(this->m_bitmap);
        }
        this->m_memoryDc = nullptr;
        this->m_bitmap = nullptr;
        this->m_previousObject = nullptr;
        this->m_pixelData = nullptr;
        this->m_width = 0;
        this->m_height = 0;
    }

    HDC m_memoryDc{nullptr};            ///< Compatible DC retained by one worker thread.
    HBITMAP m_bitmap{nullptr};          ///< Top-down 32-bit DIB selected into the memory DC.
    HGDIOBJ m_previousObject{nullptr};  ///< Original DC object restored before destruction.
    void* m_pixelData{nullptr};         ///< Writable DIB pixel storage.
    int m_width{0};                     ///< Allocated image width.
    int m_height{0};                    ///< Allocated image height.
};

/**
 * @brief Returns the reusable capture context owned by the current worker thread.
 * @return Thread-local capture context.
 */
ScreenCaptureContext& screenCaptureContext()
{
    thread_local ScreenCaptureContext context;
    return context;
}

}  // namespace

bool WindowsPlatformIntegration::sendGlobalMouseEvent(QPoint const& _position,
                                                      remote_control::MouseAction _action,
                                                      remote_control::MouseButton _button)
{
    // Keep cursor positioning and input injection atomic across concurrent control connections.
    QMutexLocker const locker{&mouseInputMutex()};

    // 1. Handle move-only events without constructing native input records.
    if (_button == remote_control::MouseButton::None)
    {
        if (_action != remote_control::MouseAction::Move)
        {
            return false;
        }
        return SetCursorPos(_position.x(), _position.y()) == TRUE;
    }

    // 2. Translate the protocol action into an ordered set of Windows flags.
    DWORD const downFlag{mouseDownFlag(_button)};
    DWORD const upFlag{mouseUpFlag(_button)};
    // A supported button must provide both native transitions for every action variant.
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
        case remote_control::MouseAction::Move:
        default:
            return false;
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

bool WindowsPlatformIntegration::setSystemUiLocked(bool _locked)
{
    SystemUiLockState& state{systemUiLockState()};
    if (_locked)
    {
        if (state.active)
        {
            return true;
        }

        // 1. Capture the state owned by Windows or another application before replacing it.
        RECT cursorBounds{};
        if (GetClipCursor(&cursorBounds) != TRUE)
        {
            return false;
        }
        RECT const desktopBounds{virtualDesktopBounds()};
        state.cursorBounds = cursorBounds;
        state.cursorWasClipped = EqualRect(&cursorBounds, &desktopBounds) == FALSE;
        if (auto* const taskbar{FindWindowW(L"Shell_TrayWnd", nullptr)})
        {
            state.taskbarFound = true;
            state.taskbarWasVisible = IsWindowVisible(taskbar) == TRUE;
            ShowWindow(taskbar, SW_HIDE);
        }

        // 2. Replace the cursor and confinement only after the original state is retained.
        QApplication::setOverrideCursor(Qt::BlankCursor);
        QPoint const cursorPosition{QCursor::pos()};
        RECT const cursorBoundsReplacement{
            cursorPosition.x(), cursorPosition.y(), cursorPosition.x() + 1, cursorPosition.y() + 1};
        if (ClipCursor(&cursorBoundsReplacement) != TRUE)
        {
            QApplication::restoreOverrideCursor();
            if (state.taskbarFound)
            {
                if (auto* const taskbar{FindWindowW(L"Shell_TrayWnd", nullptr)})
                {
                    ShowWindow(taskbar, state.taskbarWasVisible ? SW_SHOW : SW_HIDE);
                }
            }
            state = {};
            return false;
        }
        state.active = true;
        return true;
    }

    if (!state.active)
    {
        return true;
    }

    // 3. Restore every state component to its pre-lock value instead of assuming defaults.
    BOOL const cursorRestored{state.cursorWasClipped ? ClipCursor(&state.cursorBounds)
                                                     : ClipCursor(nullptr)};
    if (state.taskbarFound)
    {
        if (auto* const taskbar{FindWindowW(L"Shell_TrayWnd", nullptr)})
        {
            ShowWindow(taskbar, state.taskbarWasVisible ? SW_SHOW : SW_HIDE);
        }
    }
    QApplication::restoreOverrideCursor();
    state = {};
    return cursorRestored == TRUE;
}

QByteArray WindowsPlatformIntegration::capturePrimaryScreenPng()
{
    return screenCaptureContext().capturePng();
}

bool WindowsPlatformIntegration::isLocalFilePath(QString const& _path)
{
    QString const nativePath{QDir::toNativeSeparators(QDir::cleanPath(_path))};
    if (nativePath.size() < 3 || !nativePath[0].isLetter() || nativePath[1] != ':' ||
        nativePath[2] != QDir::separator())
    {
        return false;
    }

    QString const driveRoot{nativePath.left(3)};
    UINT const driveType{GetDriveTypeW(reinterpret_cast<LPCWSTR>(driveRoot.utf16()))};
    return driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE || driveType == DRIVE_CDROM ||
        driveType == DRIVE_RAMDISK;
}

bool WindowsPlatformIntegration::openLocalFile(QString const& _path)
{
    QString const nativePath{QDir::toNativeSeparators(_path)};
    auto const result{ShellExecuteW(nullptr,
                                    L"open",
                                    reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                    nullptr,
                                    nullptr,
                                    SW_SHOWNORMAL)};
    return reinterpret_cast<INT_PTR>(result) > ShellExecuteSuccessThreshold;
}

bool WindowsPlatformIntegration::isRunningAsAdmin()
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

bool WindowsPlatformIntegration::relaunchElevated(QStringList const& _arguments,
                                                  QString* _errorMessage)
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

bool WindowsPlatformIntegration::waitForProcessExit(quint32 _processId,
                                                    int _timeoutMs,
                                                    QString* _errorMessage)
{
    if (_processId == 0)
    {
        if (_errorMessage)
        {
            *_errorMessage = QObject::tr("The previous server process identifier is invalid.");
        }
        return false;
    }

    auto const process{OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(_processId))};
    if (!process)
    {
        // ERROR_INVALID_PARAMETER means the parent process exited before the child opened it.
        if (GetLastError() == ERROR_INVALID_PARAMETER)
        {
            return true;
        }
        if (_errorMessage)
        {
            *_errorMessage = QObject::tr("Unable to wait for the previous server process.");
        }
        return false;
    }

    DWORD const timeout{_timeoutMs > 0 ? static_cast<DWORD>(_timeoutMs) : 0};
    DWORD const waitResult{WaitForSingleObject(process, timeout)};
    CloseHandle(process);
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }

    if (_errorMessage)
    {
        *_errorMessage = waitResult == WAIT_TIMEOUT
            ? QObject::tr("Timed out while waiting for the previous server process to exit.")
            : QObject::tr("Failed while waiting for the previous server process.");
    }
    return false;
}

bool WindowsPlatformIntegration::installStartupEntry(QString* _errorMessage)
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

bool WindowsPlatformIntegration::removeStartupEntry(QString* _errorMessage)
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

bool WindowsPlatformIntegration::startupEntryExists()
{
    QSettings const settings{startupRegistryPath(), QSettings::NativeFormat};
    return settings.contains(startupValueName());
}

QString WindowsPlatformIntegration::startupRegistryPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

QString WindowsPlatformIntegration::startupValueName()
{
    return QStringLiteral("RemoteControlServer");
}

QString WindowsPlatformIntegration::startupCommand()
{
    return quoteArgument(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}
