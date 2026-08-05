#include "internal/RemoteControlTransportLog.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QThread>

Q_LOGGING_CATEGORY(remoteControlTransportLog, "remote_control.server.transport")

namespace
{

constexpr int HexadecimalBase{16};

}  // namespace

void writeTransportLog(TransportLogLevel _level, QString const& _event, QJsonObject const& _fields)
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
        case TransportLogLevel::Debug:
            qCDebug(remoteControlTransportLog).noquote() << json;
            break;
        case TransportLogLevel::Info:
            qCInfo(remoteControlTransportLog).noquote() << json;
            break;
        case TransportLogLevel::Warning:
            qCWarning(remoteControlTransportLog).noquote() << json;
            break;
        case TransportLogLevel::Critical:
            qCCritical(remoteControlTransportLog).noquote() << json;
            break;
    }
}
