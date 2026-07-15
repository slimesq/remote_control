#pragma once

#include <QString>
#include <QStringList>

class PlatformIntegration
{
public:
    static bool isRunningAsAdmin();
    static bool relaunchElevated(const QStringList& _arguments, QString* _errorMessage = nullptr);
    static bool installStartupEntry(QString* _errorMessage = nullptr);
    static bool removeStartupEntry(QString* _errorMessage = nullptr);
    static bool startupEntryExists();

private:
    static QString startupRegistryPath();
    static QString startupValueName();
    static QString startupCommand();
};
