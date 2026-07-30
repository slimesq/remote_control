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
    ControlChannel = 10,
    TestConnection = 1981,
};

/** @brief Status values encoded in the first byte of a command-status payload. */
enum class StatusCode : quint8
{
    Failure = 0,  ///< The command failed.
    Success = 1,  ///< The command completed successfully.
};

/** @brief Mouse actions that can be forwarded to the remote host. */
enum class MouseAction : quint16
{
    Click = 0,
    DoubleClick = 1,
    Press = 2,
    Release = 3,
    Move = 4,
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
    quint16 action{static_cast<quint16>(MouseAction::Move)};  ///< Encoded mouse action.
    quint16 button{static_cast<quint16>(MouseButton::None)};  ///< Encoded mouse button.
    qint32 x{0};  ///< Absolute remote-screen x-coordinate.
    qint32 y{0};  ///< Absolute remote-screen y-coordinate.
};
#pragma pack(pop)

/** @brief Describes one entry in a streamed remote directory listing. */
struct FileEntry
{
    bool isInvalid{false};    ///< Whether the directory request or payload is invalid.
    bool isDirectory{false};  ///< Whether this entry represents a directory.
    bool hasNext{true};       ///< Whether another entry packet follows this entry.
    QString fileName;         ///< File or directory name without the parent path.

    /**
     * @brief Serializes this entry into its protocol payload.
     * @return Serialized file-entry payload.
     */
    [[nodiscard]] QByteArray toPayload() const;

    /**
     * @brief Parses an entry from a protocol payload.
     * @param _payload Serialized file-entry payload.
     * @return Parsed entry, marked invalid when validation fails.
     */
    [[nodiscard]] static FileEntry fromPayload(QByteArray const& _payload);
};

/**
 * @brief Creates the common success/failure response payload.
 * @param _success Whether the command succeeded.
 * @param _message Optional user-facing result message.
 * @return Serialized status payload.
 */
[[nodiscard]] QByteArray makeStatusPayload(bool _success, QString const& _message = {});

/**
 * @brief Parses a common success/failure response payload.
 * @param _payload Serialized status payload.
 * @param _messageOut Optional output for the decoded message.
 * @return true only for an explicit StatusCode::Success; otherwise false.
 */
[[nodiscard]] bool parseStatusPayload(QByteArray const& _payload, QString* _messageOut = nullptr);

/**
 * @brief Decodes UTF-8 protocol bytes into a QString.
 * @param _data UTF-8 bytes to decode.
 * @return Decoded string.
 */
[[nodiscard]] QString decodeUtf8(QByteArray const& _data);

/**
 * @brief Encodes a QString as UTF-8 protocol bytes.
 * @param _text String to encode.
 * @return UTF-8 encoded bytes.
 */
[[nodiscard]] QByteArray encodeUtf8(QString const& _text);

}  // namespace remote_control

Q_DECLARE_METATYPE(remote_control::Command)
Q_DECLARE_METATYPE(remote_control::FileEntry)
Q_DECLARE_METATYPE(QList<remote_control::FileEntry>)
