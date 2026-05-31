#include "PlatformIntegration.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include <Windows.h>
#include <shellapi.h>

namespace {

QString quoteArgument(const QString& value)
{
    QString escaped = value;
    escaped.replace('"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

}

bool PlatformIntegration::isRunningAsAdmin()
{
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    BOOL isAdmin = FALSE;
    if (AllocateAndInitializeSid(
        &authority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool PlatformIntegration::relaunchElevated(const QStringList& arguments, QString* errorMessage)
{
    const QString program = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

    QStringList quotedArguments;
    for (const QString& argument : arguments) {
        quotedArguments << quoteArgument(argument);
    }
    const QString nativeArgs = quotedArguments.join(' ');

    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"runas",
        reinterpret_cast<LPCWSTR>(program.utf16()),
        nativeArgs.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(nativeArgs.utf16()),
        nullptr,
        SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Failed to relaunch with administrator privileges.");
        }
        return false;
    }
    return true;
}

bool PlatformIntegration::installStartupEntry(QString* errorMessage)
{
    QSettings settings(startupRegistryPath(), QSettings::NativeFormat);
    settings.setValue(startupValueName(), startupCommand());
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Failed to create the startup entry.");
        }
        return false;
    }
    return true;
}

bool PlatformIntegration::removeStartupEntry(QString* errorMessage)
{
    QSettings settings(startupRegistryPath(), QSettings::NativeFormat);
    settings.remove(startupValueName());
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Failed to remove the startup entry.");
        }
        return false;
    }
    return true;
}

bool PlatformIntegration::startupEntryExists()
{
    QSettings settings(startupRegistryPath(), QSettings::NativeFormat);
    return settings.contains(startupValueName());
}

QString PlatformIntegration::startupRegistryPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

QString PlatformIntegration::startupValueName()
{
    return QStringLiteral("RemoteServerQt");
}

QString PlatformIntegration::startupCommand()
{
    return quoteArgument(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}
