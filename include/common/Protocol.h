#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace remote_control
{

/** @brief Default TCP port used by the client and server. */
inline constexpr quint16 DefaultServerPort{9527};

/** @brief Commands supported by the remote-control wire protocol. */
enum class Command : quint16
{
    ListDrives = 1,
    ListDirectory = 2,
    RunFile = 3,
    DownloadFile = 4,
    MouseEvent = 5,
    WatchScreen = 6,
    LockMachine = 7,
    UnlockMachine = 8,
    DeleteFile = 9,
    TestConnection = 1981,
};

/** @brief Mouse actions that can be forwarded to the remote host. */
enum class MouseAction : quint16
{
    Click = 0,
    DoubleClick = 1,
    Press = 2,
    Release = 3,
};

/** @brief Mouse buttons represented by a forwarded mouse event. */
enum class MouseButton : quint16
{
    Left = 0,
    Right = 1,
    Middle = 2,
    None = 8,
};

#pragma pack(push, 1)
/** @brief Fixed-layout mouse event payload transferred over the wire. */
struct MouseEventPacket
{
    /** @brief Encoded MouseAction value. */
    quint16 action{static_cast<quint16>(MouseAction::Click)};

    /** @brief Encoded MouseButton value. */
    quint16 button{static_cast<quint16>(MouseButton::None)};

    /** @brief Absolute remote screen x-coordinate. */
    qint32 x{0};

    /** @brief Absolute remote screen y-coordinate. */
    qint32 y{0};
};
#pragma pack(pop)

/** @brief Describes one entry in a streamed remote directory listing. */
struct FileEntry
{
    /** @brief Whether the directory request or payload is invalid. */
    bool isInvalid{false};

    /** @brief Whether the entry represents a directory. */
    bool isDirectory{false};

    /** @brief Whether another entry packet follows this entry. */
    bool hasNext{true};

    /** @brief File or directory name without the parent path. */
    QString fileName;

    /** @brief Serializes this entry into its protocol payload. */
    [[nodiscard]] QByteArray toPayload() const;

    /** @brief Parses an entry from a protocol payload. */
    [[nodiscard]] static FileEntry fromPayload(QByteArray const& _payload);
};

/** @brief Creates the common success/failure response payload. */
[[nodiscard]] QByteArray makeStatusPayload(bool _success, QString const& _message = {});

/** @brief Parses a common success/failure response payload. */
[[nodiscard]] bool parseStatusPayload(QByteArray const& _payload,
                                      bool _defaultSuccess,
                                      QString* _messageOut = nullptr);

/** @brief Decodes UTF-8 protocol bytes into a QString. */
[[nodiscard]] QString decodeUtf8(QByteArray const& _data);

/** @brief Encodes a QString as UTF-8 protocol bytes. */
[[nodiscard]] QByteArray encodeUtf8(QString const& _text);

}  // namespace remote_control

Q_DECLARE_METATYPE(remote_control::Command)
Q_DECLARE_METATYPE(remote_control::FileEntry)
Q_DECLARE_METATYPE(QList<remote_control::FileEntry>)
