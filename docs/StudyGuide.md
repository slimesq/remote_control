# 项目代码学习指南

本文面向希望通过本项目学习 Qt Widgets、信号槽和 TCP 通信的开发者。构建与运行方式请先阅读项目 [README](../README.md)。

## 1. 先看整体架构

项目包含客户端、服务端、测试程序和一个公共协议库：

```text
RemoteControlClient
    ├─ 轻量短连接 ─► RemoteSession ─► CommandService
    ├─ 文件任务连接 ─► FileRequestPool ─► FileRequestWorker ─► 目录/下载/删除
    ├─ 控制长连接 ─► ControlStreamThread ─► 鼠标/锁定/解锁
    └─ 监控长连接 ─► WatchStreamThread ─► 截图

RemoteControlProtocolTests ──► 独立验证 Packet 和 Protocol
RemoteControlSmokeTest ──────► 使用相同协议端到端验证服务端
```

| 目录 | 作用 |
| --- | --- |
| `src/common`、`include/common` | 命令、数据结构和 Packet 编解码 |
| `src/client`、`include/client` | 界面、网络请求和远程屏幕交互 |
| `src/server`、`include/server` | TCP 监听、会话管理和命令执行 |
| `src/tests` | 协议测试与端到端 smoke test |

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

- `setupUi()`：调用 `uic` 生成的代码，创建并配置 Qt Designer 中定义的控件
- `wireSignals()`：建立控件与业务逻辑之间的信号槽连接
- `populateDriveTree()`、`updateDirectoryView()`：更新树和文件表格
- `updateActionState()`：根据连接、选择和异步请求状态更新控件可用性
- `m_connectionTestPending`、`m_driveListPending`、`m_fileCommandPending`：阻止同类请求重复提交
- `m_activeDownloadPath`：同时表示活动下载状态和正在下载的远程路径
- `DirectoryLoadState`、`DirectoryEntriesRole`：维护目录加载状态和缓存

目录状态转换：

```text
首次加载：Unloaded → Loading → Loaded
                         └─失败→ Unloaded

强制刷新：Loaded → Refreshing → Loaded
                         └─失败→ Loaded（保留旧缓存）
```

### 第三步：客户端网络层

- [RemoteClient.h](../include/client/RemoteClient.h)
- [RemoteClient.cpp](../src/client/RemoteClient.cpp)
- [DownloadWorker.cpp](../src/client/DownloadWorker.cpp)
- [WatchConnectionWorker.cpp](../src/client/WatchConnectionWorker.cpp)
- [ControlConnectionWorker.cpp](../src/client/ControlConnectionWorker.cpp)

普通轻量命令使用短连接：

```text
用户操作
  → MainWindow 调用 RemoteClient
  → PendingRequest 建立 TCP 连接并发送 Packet
  → 解析服务端响应
  → RemoteClient 发出业务信号
  → MainWindow 更新界面
```

可以先跟踪 `testConnection()`，它的数据最少、调用链最短。`PendingRequest` 属于
`RemoteClient` 所在的 GUI 线程，但 `QTcpSocket` 使用事件循环异步收发，因此不会同步阻塞
界面。每个 `PendingRequest` 实例只处理一次逻辑请求；目录列表虽然可能返回多个数据包，
仍然属于同一次请求。

`requestDrives()`、`requestDirectory()`、`runFile()` 和 `deleteFile()` 复用同一个 `PendingRequest` 模型。其中目录请求的服务端响应由多个 `FileEntry` 包和一个 `hasNext == false` 的终止包组成。

`PendingRequest::onReadyRead()` 将 `QTcpSocket::readAll()` 返回的新数据追加到持久缓冲区，
再循环调用 `Packet::tryParse()`。完整数据包会立即分派处理；不完整数据会留在缓冲区，
等待下一次 `readyRead()`，从而同时处理 TCP 拆包、粘包和目录多包响应。每次连接成功或
收到新数据都会重新启动 15 秒无活动超时，请求完成时停止计时器。

#### 为什么 `PendingRequest` 需要 `CallbackScope`

`PendingRequest`、`RemoteClient` 和 `MainWindow` 位于同一个 GUI 线程，因此默认的
`Qt::AutoConnection` 会采用直接连接：`PendingRequest` 发出结果信号时，会同步执行
`MainWindow::wireSignals()` 中对应的 lambda。部分 lambda 会调用
`QMessageBox::information()` 或 `QMessageBox::warning()`；这些模态函数在消息框关闭前
会运行一个嵌套 GUI 事件循环。

另一方面，服务端会在短连接响应发送完成后主动断开连接：普通命令由
`RemoteSession::processPacket()` 断开，目录和删除等文件命令由
`FileRequestWorker::releaseSocket()` 断开。因此响应数据和 TCP 断开事件可能相继到达，
产生下面的回调重入：

```text
PendingRequest::onReadyRead()
  → 解析响应并标记请求完成
  → emit connectionTested(...) 等结果信号
  → MainWindow 的直接连接 lambda
  → QMessageBox 打开模态对话框并运行嵌套事件循环
  → 嵌套事件循环处理服务端发来的 disconnected 事件
  → PendingRequest::onDisconnected()
  → requestDeletion()
```

此时外层 `onReadyRead()` 仍停留在调用栈中。如果 `requestDeletion()` 立即调用
`deleteLater()`，嵌套事件循环可能在消息框关闭前处理 `DeferredDelete` 事件；消息框关闭
后，外层 `onReadyRead()` 将恢复执行并继续访问已经销毁的 `PendingRequest`。这里主要防止
的是回调期间的提前销毁和 use-after-free，而不只是重复删除。

`CallbackScope` 只创建在 `PendingRequest` 的 Qt 事件入口中：定时器 `timeout` 回调以及
`onConnected()`、`onReadyRead()`、`onDisconnected()`、`onErrorOccurred()`。构造时增加
`m_callbackDepth`，析构时减少它。`requestDeletion()` 在回调仍活动时把状态改为
`RequestState::CleanupDeferred`；最外层 `CallbackScope` 退出后再切换到
`RequestState::DeletionScheduled` 并调用一次 `deleteLater()`。

```text
Active
  → Finished
  → CleanupDeferred      （仍有 Qt 回调处于调用栈中）
  → DeletionScheduled    （最外层 Qt 回调已经退出）
```

这个保护针对的是会自行清理的 `PendingRequest`，不是要求项目中每个普通 slot 都创建
`CallbackScope`。

三个 generation 分别管理不同范围的过期结果：

- `m_endpointGeneration`：地址或端口变化后，丢弃旧 endpoint 的一次性请求结果。
- `m_watchGeneration`：停止监控后，丢弃旧监控会话返回的帧、错误和完成通知。
- `m_controlGeneration`：停止控制后，丢弃旧控制会话返回的命令结果。

它们不会互相比较。endpoint 改变时，`setEndpoint()` 会分别让一次性请求、监控会话和
控制会话失效。

远程屏幕使用独立工作线程和持久连接：

```text
WatchWindow 立即请求首帧
  → RemoteClient 将任务投递给 WatchConnectionWorker
  → 工作线程复用 QTcpSocket 发送 WatchScreen Packet
  → 工作线程接收并解码 PNG
  → GUI 线程接收 QImage 并刷新画面
  → WatchWindow 按最高约 30 FPS 调度下一帧
```

同一时刻只允许一帧处于请求中，上一帧完成后才会调度下一帧，避免慢网络下积压请求。
如果一帧处理达到 33 ms，下一帧会立即开始；否则只等待 33 ms 中的剩余时间。因此实际帧率
取 30 FPS 上限与当前截图、编码、网络和解码能力中的较低值。
`m_watchPending` 只表示当前是否有一帧正在等待结果，不表示监控窗口或长连接是否开启；
一帧成功、失败或超时后会恢复为 `false`，关闭监控时也会立即清除。

`RemoteClient` 启动三个常驻工作线程，分别承载 `WatchConnectionWorker`、
`ControlConnectionWorker` 和 `DownloadWorker`。GUI 线程通过
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 投递任务，析构时使用
`Qt::BlockingQueuedConnection` 先让 worker 停止，再退出并等待线程。

鼠标、锁定和解锁使用单独的 `ControlConnectionWorker` 长连接。控制命令一次只发送一个并等待状态响应，连续鼠标移动只保留队列中最新的位置，防止输入积压。下载使用 `DownloadWorker` 在线程中接收数据并写入 `QSaveFile`；服务端每次最多读取 64 KiB 并立即发送，不会把整个文件保存在内存中。

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

`CommandService` 处理连接测试、磁盘列表、打开文件，以及必须在 GUI 线程操作的锁屏窗口。`RemoteSession` 会把目录、下载和删除请求连同 socket 转交给 `FileRequestPool`。线程池按需创建 2 至 4 个常驻线程，空闲的 `FileRequestWorker` 会继续处理排队任务，最多排队 64 个文件请求。`ControlChannel` 和 `WatchScreen` 分别转交给独立的持久连接线程，每类最多 4 条连接，避免截图数据阻塞鼠标输入。鼠标注入在控制线程中执行；锁定和解锁则以 queued invocation 投递给 GUI 线程。

### 第五步：公共协议

- [Protocol.h](../include/common/Protocol.h)
- [Protocol.cpp](../src/common/Protocol.cpp)
- [Packet.h](../include/common/Packet.h)
- [Packet.cpp](../src/common/Packet.cpp)
- [ProtocolTestMain.cpp](../src/tests/ProtocolTestMain.cpp)

重点关注：

- `Command`：客户端与服务端共享的命令编号
- payload 编解码辅助函数
- `Packet::serialize()`：序列化
- `Packet::tryParse()`：处理 TCP 字节流中的完整数据包
- 包头、长度、命令、payload 和 16 位累加校验值的布局
- 最大 64 MiB payload、非法长度恢复、半包和连续包处理

修改协议时，客户端、服务端、协议测试和 smoke test 必须同步验证。

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
| 工作线程 | `QThread`、worker object、queued/blocking queued connection |
| 定时任务 | `QTimer` |
| 文件系统 | `QFile`、`QDir`、`QFileInfo`、`QSaveFile` |
| 图片与绘制 | `QImage`、`QPainter`、Windows GDI |
| 系统集成 | `QSystemTrayIcon`、`QSettings`、Windows API |
| CMake Qt 集成 | `AUTOMOC`、`AUTOUIC`、Qt target linking |

## 4. 建议的学习练习

1. 从“测试连接”按钮开始，为调用链逐步加断点。
2. 新增一个不修改系统状态的协议命令，例如返回服务端版本信息。
3. 阅读现有 Packet 边界测试，再补充损坏校验值或最大 payload 附近的测试。
4. 观察远程屏幕刷新频率，分析 `QTimer` 和网络开销。
5. 将界面提示文本与业务错误分层，练习 Qt 的错误传播方式。

## 5. 调试建议

- 客户端和服务端分别启动调试时，确认端口一致。
- 遇到 Qt 类型跳转失败，先确认 `build/msvc-debug/compile_commands.json` 存在，再重启 `clangd`。
- 遇到 `ui_*.h` 缺失，先完成一次构建；这些头文件由 `AUTOUIC` 生成。
- 修改 `Q_OBJECT` 类后出现链接错误时，检查头文件是否包含在 CMake target 中，并重新运行 configure。
- 修改公共协议后先运行 CTest 中的 `RemoteControlProtocolTests`，再启动服务端运行 `RemoteControlSmokeTest`。
