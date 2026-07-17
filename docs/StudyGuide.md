# 项目代码学习指南

本文面向希望通过本项目学习 Qt Widgets、信号槽和 TCP 通信的开发者。构建与运行方式请先阅读项目 [README](../README.md)。

## 1. 先看整体架构

项目分为三个应用层模块和一个公共模块：

```text
RemoteControlClient
    │
    │ TCP Packet
    ▼
RemoteControlServer ──► CommandService ──► Windows/文件系统操作

RemoteControlSmokeTest ──► 使用相同协议验证服务端
```

| 目录 | 作用 |
| --- | --- |
| `src/common`、`include/common` | 命令、数据结构和 Packet 编解码 |
| `src/client`、`include/client` | 界面、网络请求和远程屏幕交互 |
| `src/server`、`include/server` | TCP 监听、会话管理和命令执行 |
| `src/tests` | 端到端 smoke test |

## 2. 推荐阅读顺序

### 第一步：程序入口

- [客户端入口](../src/client/ClientMain.cpp)
- [服务端入口](../src/server/ServerMain.cpp)

重点理解：

- `QApplication` 如何启动事件循环
- `QCommandLineParser` 如何定义参数
- 客户端如何解析服务端地址和端口参数
- 服务端如何创建监听和系统托盘
- `Run.ps1` 如何分别启动客户端、服务端和 smoke test

### 第二步：客户端界面

- [MainWindow.h](../include/client/MainWindow.h)
- [MainWindow.cpp](../src/client/MainWindow.cpp)
- [MainWindow.ui](../src/client/MainWindow.ui)

建议重点阅读：

- `setupUi()`：加载 Qt Designer 生成的界面
- `wireSignals()`：建立控件与业务逻辑之间的信号槽连接
- `populateDriveTree()`、`updateDirectoryView()`：更新树和文件表格
- `setBusyState()`、`updateActionState()`：维护界面状态

### 第三步：客户端网络层

- [RemoteClient.h](../include/client/RemoteClient.h)
- [RemoteClient.cpp](../src/client/RemoteClient.cpp)

一次典型请求的流程：

```text
用户操作
  → MainWindow 调用 RemoteClient
  → PendingRequest 建立 TCP 连接并发送 Packet
  → 解析服务端响应
  → RemoteClient 发出业务信号
  → MainWindow 更新界面
```

可以先跟踪 `testConnection()`，它的数据最少、调用链最短。

### 第四步：服务端

- [RemoteServer.cpp](../src/server/RemoteServer.cpp)
- [RemoteSession.cpp](../src/server/RemoteSession.cpp)
- [CommandService.cpp](../src/server/CommandService.cpp)

服务端调用链：

```text
RemoteServer 接受连接
  → 为连接创建 RemoteSession
  → RemoteSession 解析 Packet
  → CommandService 分派并执行命令
  → 序列化响应 Packet
```

`CommandService` 集中了磁盘枚举、目录浏览、文件操作、截图、鼠标和锁屏等功能，是理解服务端业务的核心。

### 第五步：公共协议

- [Protocol.h](../include/common/Protocol.h)
- [Packet.h](../include/common/Packet.h)
- [Packet.cpp](../src/common/Packet.cpp)

重点关注：

- `Command`：客户端与服务端共享的命令编号
- payload 编解码辅助函数
- `Packet::serialize()`：序列化
- `Packet::tryParse()`：处理 TCP 字节流中的完整数据包

修改协议时，客户端、服务端和 smoke test 必须同步验证。

### 第六步：远程屏幕交互

- [WatchWindow.h](../include/client/WatchWindow.h)
- [WatchWindow.cpp](../src/client/WatchWindow.cpp)
- [WatchWindow.ui](../src/client/WatchWindow.ui)
- [LockWindow.cpp](../src/server/LockWindow.cpp)

这一部分适合学习：

- 自定义 `QWidget` 绘制
- 鼠标事件坐标转换
- `QTimer` 合并高频事件
- 截图数据在网络中的传输与显示

## 3. Qt 知识点索引

| 知识点 | 项目中的示例 |
| --- | --- |
| GUI 事件循环 | `QApplication` |
| Qt Designer | `MainWindow.ui`、`WatchWindow.ui`、`LockWindow.ui` |
| 信号槽 | `MainWindow::wireSignals()`、`RemoteClient` |
| TCP 客户端/服务端 | `QTcpSocket`、`QTcpServer` |
| 对象生命周期 | `QObject` parent、`std::unique_ptr` |
| 定时任务 | `QTimer` |
| 文件系统 | `QFile`、`QDir`、`QFileInfo`、`QSaveFile` |
| 图片与绘制 | `QImage`、`QPainter`、`QScreen` |
| 系统集成 | `QSystemTrayIcon`、`QSettings`、Windows API |
| CMake Qt 集成 | `AUTOMOC`、`AUTOUIC`、Qt target linking |

## 4. 建议的学习练习

1. 从“测试连接”按钮开始，为调用链逐步加断点。
2. 新增一个不修改系统状态的协议命令，例如返回服务端版本信息。
3. 为 Packet 边界情况补充测试，例如半包、粘包和非法长度。
4. 观察远程屏幕刷新频率，分析 `QTimer` 和网络开销。
5. 将界面提示文本与业务错误分层，练习 Qt 的错误传播方式。

## 5. 调试建议

- 客户端和服务端分别启动调试时，确认端口一致。
- 遇到 Qt 类型跳转失败，先确认 `build/vscode-debug/compile_commands.json` 存在，再重启 `clangd`。
- 遇到 `ui_*.h` 缺失，先完成一次构建；这些头文件由 `AUTOUIC` 生成。
- 修改 `Q_OBJECT` 类后出现链接错误时，检查头文件是否包含在 CMake target 中，并重新运行 configure。
- 协议修改后优先运行 `RemoteControlSmokeTest`，避免只验证界面路径。
