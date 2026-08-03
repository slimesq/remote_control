#include "server/RemoteControlServerLog.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QThread>

Q_LOGGING_CATEGORY(remoteControlServerLog, "remote_control.server")

namespace
{

constexpr int HexadecimalBase{16};

}  // namespace

void writeServerLog(ServerLogLevel _level, QString const& _event, QJsonObject const& _fields)
{
    QJsonObject record{_fields};
    record.insert(QStringLiteral("event"), _event);
    record.insert(QStringLiteral("timestamp_utc"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    record.insert(QStringLiteral("thread_id"),
                  QStringLiteral("0x%1").arg(
                      reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, HexadecimalBase));

    QByteArray const json{QJsonDocument{record}.toJson(QJsonDocument::Compact)};
    switch (_level)
    {
        case ServerLogLevel::Debug:
            qCDebug(remoteControlServerLog).noquote() << json;
            break;
        case ServerLogLevel::Info:
            qCInfo(remoteControlServerLog).noquote() << json;
            break;
        case ServerLogLevel::Warning:
            qCWarning(remoteControlServerLog).noquote() << json;
            break;
        case ServerLogLevel::Critical:
            qCCritical(remoteControlServerLog).noquote() << json;
            break;
    }
}
