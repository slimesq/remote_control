#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace remote_control {

enum class Command : quint16 {
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

enum class MouseAction : quint16 {
    Click = 0,
    DoubleClick = 1,
    Press = 2,
    Release = 3,
};

enum class MouseButton : quint16 {
    Left = 0,
    Right = 1,
    Middle = 2,
    None = 8,
};

#pragma pack(push, 1)
struct MouseEventPacket {
    quint16 action = static_cast<quint16>(MouseAction::Click);
    quint16 button = static_cast<quint16>(MouseButton::None);
    qint32 x = 0;
    qint32 y = 0;
};
#pragma pack(pop)

struct FileEntry {
    bool isInvalid = false;
    bool isDirectory = false;
    bool hasNext = true;
    QString fileName;

    QByteArray toPayload() const;
    static FileEntry fromPayload(const QByteArray& _payload);
};

QByteArray makeStatusPayload(bool _success, const QString& _message = {});
bool parseStatusPayload(const QByteArray& _payload, bool _defaultSuccess, QString* _messageOut = nullptr);

QString decodeUtf8(const QByteArray& _data);
QByteArray encodeUtf8(const QString& _text);

}

Q_DECLARE_METATYPE(remote_control::Command)
Q_DECLARE_METATYPE(remote_control::FileEntry)
Q_DECLARE_METATYPE(QList<remote_control::FileEntry>)
