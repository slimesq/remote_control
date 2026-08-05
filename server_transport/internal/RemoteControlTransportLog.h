#pragma once

#include <QJsonObject>
#include <QString>

/** @brief Severity assigned to one structured server event. */
enum class TransportLogLevel
{
    Debug,     ///< Detailed connection lifecycle information.
    Info,      ///< Normal server lifecycle information.
    Warning,   ///< Rejected input, capacity, or recoverable I/O information.
    Critical,  ///< Server startup or invariant failures.
};

/**
 * @brief Writes one compact JSON object through the remote-control logging category.
 * @param _level Event severity.
 * @param _event Stable machine-readable event name.
 * @param _fields Event-specific structured fields.
 */
void writeTransportLog(TransportLogLevel _level,
                       QString const& _event,
                       QJsonObject const& _fields = {});
