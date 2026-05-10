#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace remoteqt {

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

struct LegacyFileInfo {
    qint32 isInvalid = 0;
    qint32 isDirectory = 0;
    qint32 hasNext = 1;
    char fileName[256] = {};
};
#pragma pack(pop)

struct FileEntry {
    bool isInvalid = false;
    bool isDirectory = false;
    bool hasNext = true;
    QString fileName;

    QByteArray toPayload() const;
    static FileEntry fromPayload(const QByteArray& payload);
};

QByteArray makeStatusPayload(bool success, const QString& message = {});
bool parseStatusPayload(const QByteArray& payload, bool defaultSuccess, QString* messageOut = nullptr);

QString decodeLocal8Bit(const QByteArray& data);
QByteArray encodeLocal8Bit(const QString& text);

}

Q_DECLARE_METATYPE(remoteqt::Command)
Q_DECLARE_METATYPE(remoteqt::FileEntry)
Q_DECLARE_METATYPE(QList<remoteqt::FileEntry>)
