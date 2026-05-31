# Qt 项目学习指南

## 1. 先理解项目结构

项目入口是：

- [CMakeLists.txt](D:/CodeRepositories/edoyun/remote_control/CMakeLists.txt)

整体分四块：

- `src/common`：客户端和服务端共用的协议、数据包
- `src/client`：Qt 客户端界面，负责连接、浏览文件、远程查看屏幕
- `src/server`：Qt 服务端，负责监听端口、执行命令
- `src/tests`：冒烟测试，用代码模拟客户端请求服务端

核心目标：

- `remote_client_qt`：客户端程序
- `remote_server_qt`：服务端程序
- `remote_smoke_test`：测试程序

## 2. 先跑起来

快速启动文档：

- [QT_CREATOR_QUICKSTART.md](D:/CodeRepositories/edoyun/remote_control/QT_CREATOR_QUICKSTART.md)

最简单方式：

1. 用 Qt Creator 打开 [CMakeLists.txt](D:/CodeRepositories/edoyun/remote_control/CMakeLists.txt)
2. Kit 选 `Desktop Qt 6.7.3 MSVC2022 64bit`
3. 启动目标选 `remote_client_qt`
4. 运行客户端

客户端启动时会自动检测 `127.0.0.1:9527`。如果服务端没开，它会自动启动 `remote_server_qt.exe`。

## 3. 阅读顺序

先看客户端入口：

- [src/client/main.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/main.cpp)

这里做了几件事：

- 创建 `QApplication`
- 解析命令行参数
- 尝试启动本地服务端
- 创建并显示 `MainWindow`

然后看主窗口：

- [include/client/MainWindow.h](D:/CodeRepositories/edoyun/remote_control/include/client/MainWindow.h)
- [src/client/MainWindow.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/MainWindow.cpp)

这里是 Qt 新手最应该认真读的文件。重点看：

- `setupUi(this)`：加载 `.ui` 界面
- `wireSignals()`：按钮、表格、树控件如何连接信号槽
- `populateDriveTree()`：如何把磁盘列表显示到树
- `updateDirectoryView()`：如何把远程目录内容显示到界面
- `setBusyState()` / `updateActionState()`：如何控制按钮启用禁用

## 4. 再看网络客户端

相关文件：

- [include/client/RemoteClient.h](D:/CodeRepositories/edoyun/remote_control/include/client/RemoteClient.h)
- [src/client/RemoteClient.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/RemoteClient.cpp)

它是客户端和服务端通信的核心。

重点理解这个模式：

```text
客户端按钮点击
-> MainWindow 调用 RemoteClient
-> RemoteClient 创建 TCP 连接
-> 发送 Packet
-> 收到响应
-> emit 信号
-> MainWindow 更新 UI
```

例如点击“测试连接”：

```cpp
m_client->testConnection();
```

然后 `RemoteClient` 发 `TestConnection` 命令，收到回复后发出：

```cpp
connectionTested(bool success, const QString& message)
```

主窗口收到这个信号，再弹窗或更新状态栏。

## 5. 再看服务端

服务端入口：

- [src/server/main.cpp](D:/CodeRepositories/edoyun/remote_control/src/server/main.cpp)

服务端核心：

- [src/server/RemoteServer.cpp](D:/CodeRepositories/edoyun/remote_control/src/server/RemoteServer.cpp)
- [src/server/RemoteSession.cpp](D:/CodeRepositories/edoyun/remote_control/src/server/RemoteSession.cpp)
- [src/server/CommandService.cpp](D:/CodeRepositories/edoyun/remote_control/src/server/CommandService.cpp)

调用链是：

```text
remote_server_qt 启动
-> RemoteServer 监听 9527
-> 有客户端连接
-> 创建 RemoteSession
-> 解析 Packet
-> CommandService 执行命令
-> 返回 Packet
```

`CommandService` 是服务端的“功能菜单”，比如：

- `handleListDrives()`：列出磁盘
- `handleListDirectory()`：列目录
- `handleRunFile()`：打开文件
- `handleDownloadFile()`：下载文件
- `handleWatchScreen()`：截图
- `handleMouseEvent()`：模拟鼠标
- `handleLockMachine()` / `handleUnlockMachine()`：锁定/解锁窗口

## 6. 最关键的协议层

相关文件：

- [include/common/Protocol.h](D:/CodeRepositories/edoyun/remote_control/include/common/Protocol.h)
- [include/common/Packet.h](D:/CodeRepositories/edoyun/remote_control/include/common/Packet.h)
- [src/common/Packet.cpp](D:/CodeRepositories/edoyun/remote_control/src/common/Packet.cpp)

这里定义了客户端和服务端“说什么话”。

命令枚举在 `Protocol.h`：

```cpp
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
```

每一次 TCP 通信，本质都是：

```text
命令 Command + 数据 payload
```

打包由 `Packet::serialize()` 完成，拆包由 `Packet::tryParse()` 完成。

## 7. 你作为 Qt 新手，重点学这些 Qt 知识

这个项目里最值得掌握：

- `QApplication`：Qt GUI 程序入口
- `QMainWindow` / `QDialog` / `QWidget`：窗口类
- `.ui` 文件：Qt Designer 设计界面
- `signal / slot`：Qt 最核心机制
- `QTcpSocket` / `QTcpServer`：网络通信
- `QTimer`：定时刷新远程画面
- `QFile` / `QDir` / `QFileInfo`：文件系统操作
- `QImage` / `QPainter`：显示远程截图
- `CMake + Qt`：项目组织方式

## 推荐第一天阅读的文件

第一天建议只读这 4 个文件：

1. [src/client/main.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/main.cpp)
2. [src/client/MainWindow.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/MainWindow.cpp)
3. [src/client/RemoteClient.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/RemoteClient.cpp)
4. [src/server/CommandService.cpp](D:/CodeRepositories/edoyun/remote_control/src/server/CommandService.cpp)

读懂这四个，项目的大骨架就立住了。下一步可以从“点击测试连接按钮”这一条链路开始逐行阅读，因为它最短、最适合入门。

## 8. 项目中的界面控件

### 客户端主窗口 MainWindow

文件：

- [src/client/MainWindow.ui](D:/CodeRepositories/edoyun/remote_control/src/client/MainWindow.ui)

主窗口本身：

- `QMainWindow`：主窗口
- `QWidget`：中心区域 `centralwidget`
- `QStatusBar`：底部状态栏

显示/容器类：

- `QFrame`：`heroFrame`
- `QGroupBox`：`connectionGroupBox`
- `QGroupBox`：`actionsGroupBox`
- `QGroupBox`：`directoryGroupBox`
- `QGroupBox`：`fileGroupBox`
- `QSplitter`：左右分割区域

文本显示：

- `QLabel`：`titleLabel`
- `QLabel`：`subtitleLabel`
- `QLabel`：`hostLabel`
- `QLabel`：`portLabel`

输入控件：

- `QLineEdit`：`hostEdit`，输入服务器地址
- `QSpinBox`：`portSpin`，输入端口号

按钮：

- `QPushButton`：`testButton`，测试连接
- `QPushButton`：`refreshButton`，浏览文件
- `QPushButton`：`watchButton`，远程监控
- `QPushButton`：`openFileButton`，打开文件
- `QPushButton`：`downloadFileButton`，下载文件
- `QPushButton`：`deleteFileButton`，删除文件

列表/表格：

- `QTreeWidget`：`treeWidget`，显示远程目录树
- `QTableWidget`：`fileTable`，显示文件列表

### 远程监控窗口 WatchWindow

文件：

- [src/client/WatchWindow.ui](D:/CodeRepositories/edoyun/remote_control/src/client/WatchWindow.ui)

窗口本身：

- `QDialog`：远程监控对话框

容器类：

- `QGroupBox`：`controlsGroupBox`
- `QGroupBox`：`previewGroupBox`
- `QFrame`：`screenFrame`
- `QWidget`：`screenContainer`

按钮：

- `QPushButton`：`lockButton`，锁定远程机器
- `QPushButton`：`unlockButton`，解锁远程机器

文本显示：

- `QLabel`：`instructionsLabel`

代码里还手动创建了一个自定义控件：

- `RemoteScreenWidget`：继承自 `QWidget`，用于显示远程屏幕截图和接收鼠标操作

相关文件：

- [include/client/WatchWindow.h](D:/CodeRepositories/edoyun/remote_control/include/client/WatchWindow.h)

### 服务端锁屏窗口 LockWindow

文件：

- [src/server/LockWindow.ui](D:/CodeRepositories/edoyun/remote_control/src/server/LockWindow.ui)

窗口本身：

- `QWidget`：`LockWindow`

容器类：

- `QFrame`：`messagePanel`

文本显示：

- `QLabel`：`headlineLabel`
- `QLabel`：`messageLabel`
- `QLabel`：`detailLabel`

### 代码里动态创建的界面对象

这些不是 `.ui` 里拖出来的，是代码运行时创建的：

- `QProgressDialog`：下载进度对话框

  文件：[src/client/MainWindow.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/MainWindow.cpp)

- `QMenu`：文件列表右键菜单

  文件：[src/client/MainWindow.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/MainWindow.cpp)

- `QAction`：右键菜单里的动作，如下载、删除、打开

  文件：[src/client/MainWindow.cpp](D:/CodeRepositories/edoyun/remote_control/src/client/MainWindow.cpp)

- `QSystemTrayIcon`：服务端系统托盘图标

  文件：[include/server/ServerTrayController.h](D:/CodeRepositories/edoyun/remote_control/include/server/ServerTrayController.h)

- `QMenu` / `QAction`：服务端托盘菜单和菜单项

  文件：[include/server/ServerTrayController.h](D:/CodeRepositories/edoyun/remote_control/include/server/ServerTrayController.h)

另外，`.ui` 里还有很多 `QVBoxLayout`、`QHBoxLayout`、`spacer`，这些是布局管理器/弹簧，主要负责控件摆放，一般不算“控件”。

## 9. 项目中的 Qt 对象类

这里按 `QObject` 体系来列。也就是说，这些类可以使用 Qt 的信号槽、父子对象管理和事件系统。

注意：`QString`、`QByteArray`、`QImage`、`QDir`、`QFileInfo` 这类虽然是 Qt 类，但不是 `QObject` 对象类。

### 项目自己定义的 Qt 对象类

这些类是项目里自己写的，并且直接或间接继承自 `QObject`。

| 类名 | 继承自 | 是否控件 | 作用 |
| --- | --- | --- | --- |
| `MainWindow` | `QMainWindow` | 是 | 客户端主窗口 |
| `WatchWindow` | `QDialog` | 是 | 远程监控窗口 |
| `RemoteScreenWidget` | `QWidget` | 是 | 显示远程屏幕、接收鼠标操作 |
| `LockWindow` | `QWidget` | 是 | 服务端锁屏窗口 |
| `RemoteClient` | `QObject` | 否 | 客户端网络请求封装 |
| `PendingRequest` | `QObject` | 否 | 单次 TCP 请求对象，定义在 `RemoteClient.cpp` 内部 |
| `RemoteServer` | `QObject` | 否 | 服务端监听器封装 |
| `RemoteSession` | `QObject` | 否 | 服务端单个 TCP 连接会话 |
| `CommandService` | `QObject` | 否 | 服务端命令处理中心 |
| `ServerTrayController` | `QObject` | 否 | 服务端托盘菜单控制器 |

### 项目中用到的 Qt 官方对象类

界面窗口/控件类：

| 类名 | 作用 |
| --- | --- |
| `QApplication` | GUI 程序入口对象 |
| `QCoreApplication` | 非 GUI/核心应用入口能力，项目中也通过它退出程序、取程序路径 |
| `QMainWindow` | 主窗口基类 |
| `QDialog` | 对话框窗口基类 |
| `QWidget` | 所有 Widgets 控件的基础类 |
| `QLabel` | 文本标签 |
| `QPushButton` | 按钮 |
| `QLineEdit` | 单行输入框 |
| `QSpinBox` | 数字输入框 |
| `QTreeWidget` | 树形控件 |
| `QTableWidget` | 表格控件 |
| `QFrame` | 框架、分隔线或面板控件 |
| `QGroupBox` | 分组容器 |
| `QSplitter` | 可拖动分割区域 |
| `QStatusBar` | 状态栏 |
| `QProgressDialog` | 下载进度对话框 |
| `QMenu` | 菜单 |
| `QAction` | 菜单项或动作 |
| `QSystemTrayIcon` | 系统托盘图标 |
| `QMessageBox` | 消息框 |
| `QFileDialog` | 文件选择对话框 |

布局类，也属于 `QObject` 体系：

| 类名 | 作用 |
| --- | --- |
| `QVBoxLayout` | 垂直布局 |
| `QHBoxLayout` | 水平布局 |

网络、定时器、系统对象类：

| 类名 | 作用 |
| --- | --- |
| `QTcpSocket` | TCP 客户端连接 |
| `QTcpServer` | TCP 服务端监听 |
| `QTimer` | 定时器 |
| `QScreen` | 屏幕对象，用于截图 |
| `QSettings` | 配置或注册表读写 |
| `QFile` | 文件读写对象 |
| `QSaveFile` | 安全保存文件对象 |
| `QBuffer` | 内存缓冲区 IO 对象 |

### 容易混淆但不是 QObject 的 Qt 类

这些项目里也大量使用，但它们不是 `QObject` 对象类：

| 类名 | 说明 |
| --- | --- |
| `QString` | 字符串 |
| `QStringList` | 字符串列表 |
| `QByteArray` | 字节数组 |
| `QImage` | 图片数据 |
| `QPixmap` | 图片或窗口截图数据，项目中间接使用 |
| `QPainter` | 绘图工具 |
| `QDir` | 目录操作 |
| `QFileInfo` | 文件信息 |
| `QFileInfoList` | 文件信息列表 |
| `QDataStream` | 二进制流读写 |
| `QCommandLineParser` | 命令行解析 |
| `QCommandLineOption` | 命令行选项 |
| `QTreeWidgetItem` | 树节点项，不是 `QObject` |
| `QTableWidgetItem` | 表格项，不是 `QObject` |
| `QTreeWidgetItemIterator` | 树节点迭代器 |
| `QTemporaryDir` | 临时目录 |
| `QRandomGenerator` | 随机数生成器 |

可以简单记：

```text
继承 QObject 的：是 Qt 对象类
继承 QWidget 的：既是 Qt 对象类，也是界面控件
QString / QByteArray / QImage 这类：是 Qt 工具类，不是 QObject
```
