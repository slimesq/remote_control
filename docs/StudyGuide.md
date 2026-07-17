# 项目代码学习指南

本文面向希望通过本项目学习 Qt Widgets、信号槽和 TCP 通信的开发者。构建与运行方式请先阅读项目 [README](../README.md)。

## 1. 先看整体架构

项目分为三个应用层模块和一个公共模块：

```text
RemoteControlClient
    ├─ 轻量短连接 ─► RemoteSession ─► CommandService
    ├─ 文件任务连接 ─► FileRequestPool ─► FileRequestWorker ─► 目录/下载/删除
    ├─ 控制长连接 ─► ControlStreamThread ─► 鼠标/锁定/解锁
    └─ 监控长连接 ─► WatchStreamThread ─► 截图

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
- [ControlConnectionWorker.cpp](../src/client/ControlConnectionWorker.cpp)
- [DownloadWorker.cpp](../src/client/DownloadWorker.cpp)
- [WatchConnectionWorker.cpp](../src/client/WatchConnectionWorker.cpp)

普通轻量命令使用短连接：

```text
用户操作
  → MainWindow 调用 RemoteClient
  → PendingRequest 建立 TCP 连接并发送 Packet
  → 解析服务端响应
  → RemoteClient 发出业务信号
  → MainWindow 更新界面
```

可以先跟踪 `testConnection()`，它的数据最少、调用链最短。

远程屏幕使用独立工作线程和持久连接：

```text
WatchWindow 定时请求下一帧
  → RemoteClient 将任务投递给 WatchConnectionWorker
  → 工作线程复用 QTcpSocket 发送 WatchScreen Packet
  → 工作线程接收并解码 PNG
  → GUI 线程接收 QImage 并刷新画面
```

同一时刻只允许一帧处于请求中，上一帧完成后才会发送下一帧，避免慢网络下积压请求。

鼠标、锁定和解锁使用单独的 `ControlConnectionWorker` 长连接。控制命令一次只发送一个并等待状态响应，连续鼠标移动只保留队列中最新的位置，防止输入积压。下载使用 `DownloadWorker` 在线程中接收数据并写入 `QSaveFile`；服务端每读一个固定大小的 chunk 就立即发送，不会把整个文件保存在内存中。

### 第四步：服务端

- [RemoteServer.cpp](../src/server/RemoteServer.cpp)
- [RemoteSession.cpp](../src/server/RemoteSession.cpp)
- [CommandService.cpp](../src/server/CommandService.cpp)
- [ControlStreamThread.cpp](../src/server/ControlStreamThread.cpp)
- [FileRequestPool.cpp](../src/server/FileRequestPool.cpp)
- [FileRequestWorker.cpp](../src/server/FileRequestWorker.cpp)
- [WatchStreamThread.cpp](../src/server/WatchStreamThread.cpp)
- [PlatformIntegration.cpp](../src/server/PlatformIntegration.cpp)

服务端调用链：

```text
RemoteServer 接受连接
  → 为连接创建 RemoteSession
  → RemoteSession 解析 Packet
  → 按命令类型保留短连接或转交工作线程
  → 序列化响应 Packet
```

`CommandService` 只处理轻量命令以及必须在 GUI 线程操作的锁屏窗口。`RemoteSession` 会把目录、下载和删除请求连同 socket 转交给 `FileRequestPool`。线程池按需创建 2 至 4 个常驻线程，空闲的 `FileRequestWorker` 会继续处理排队任务，避免为每次文件请求重复创建和销毁线程；队列同时限制了待处理请求数量。`ControlChannel` 和 `WatchScreen` 分别转交给独立的持久连接线程，避免截图数据阻塞鼠标输入。锁定和解锁由控制线程接收，再以 queued invocation 投递给 GUI 线程。

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
- [ControlConnectionWorker.cpp](../src/client/ControlConnectionWorker.cpp)
- [WatchConnectionWorker.cpp](../src/client/WatchConnectionWorker.cpp)
- [ControlStreamThread.cpp](../src/server/ControlStreamThread.cpp)
- [WatchStreamThread.cpp](../src/server/WatchStreamThread.cpp)
- [LockWindow.cpp](../src/server/LockWindow.cpp)

这一部分适合学习：

- 自定义 `QWidget` 绘制
- 鼠标事件坐标转换
- `QTimer` 合并高频事件
- `QThread`、queued signal/slot 和对象线程归属
- 持久 TCP 连接与单帧流量控制
- 独立屏幕通道与控制通道，避免 head-of-line blocking
- 截图数据在网络中的传输与显示

## 3. Qt 知识点索引

| 知识点 | 项目中的示例 |
| --- | --- |
| GUI 事件循环 | `QApplication` |
| Qt Designer | `MainWindow.ui`、`WatchWindow.ui`、`LockWindow.ui` |
| 信号槽 | `MainWindow::wireSignals()`、`RemoteClient` |
| TCP 客户端/服务端 | `QTcpSocket`、`QTcpServer` |
| 对象生命周期 | `QObject` parent、`std::unique_ptr` |
| 工作线程 | `QThread`、worker object、queued connection |
| 定时任务 | `QTimer` |
| 文件系统 | `QFile`、`QDir`、`QFileInfo`、`QSaveFile` |
| 图片与绘制 | `QImage`、`QPainter`、Windows GDI |
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
