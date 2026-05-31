#pragma once

#include <QString>
#include <QStringList>

class PlatformIntegration
{
public:
    static bool isRunningAsAdmin();
    static bool relaunchElevated(const QStringList& arguments, QString* errorMessage = nullptr);
    static bool installStartupEntry(QString* errorMessage = nullptr);
    static bool removeStartupEntry(QString* errorMessage = nullptr);
    static bool startupEntryExists();

private:
    static QString startupRegistryPath();
    static QString startupValueName();
    static QString startupCommand();
};
