# 项目代码学习指南

本文面向希望通过本项目学习 Qt Widgets、信号槽、TCP 通信和 Windows IOCP 的开发者。
构建与运行方式请先阅读项目 [README](../README.md)。

本文只负责安排阅读顺序和解释跨模块难点。完整组件图放在客户端与服务端架构文档中，脚本参数
放在 `scripts/README.md`，避免同一配置在多个文件中重复维护。

如果已经读完客户端，可以直接从“第四步：服务端”继续。具备 C++/Qt 基础时，理解客户端主要
调用链通常需要 15～25 小时；理解服务端主要调用链也约需 15～25 小时。要达到能够安全修改
IOCP 生命周期和线程同步的程度，服务端建议累计投入 25～40 小时。

## 1. 先看整体架构

项目包含客户端、服务端、测试程序和一个公共协议库：

```text
RemoteControlClient
    ├─ 一次性请求 ─────┐
    ├─ 下载连接 ───────┤
    ├─ 控制长连接 ─────┼─► RemoteControlTransport ─► completion workers / task pools
    └─ 监控长连接 ─────┘

RemoteControlProtocolTests ───────────► 独立验证 Packet 和 Protocol
RemoteControlConnectionStateTests ───► 独立验证连接状态机和配额
RemoteControlTransportLifecycleTests ─► 验证 IOCP 启停和资源回收
RemoteControlTransportResilienceTests ► 注入损坏流量并执行并发请求
RemoteControlSmokeTests ──────────────► 使用相同协议端到端验证服务端
```

| 目录 | 作用 |
| --- | --- |
| `src/common`、`include/common` | 命令、数据结构和 Packet 编解码 |
| `src/client`、`include/client` | 界面、网络请求和远程屏幕交互 |
| `src/server`、`include/server` | IOCP 网络状态机、任务池和 Qt/Windows 命令执行 |
| `src/tests` | 协议、连接状态、传输韧性与端到端 smoke test |

客户端组件、线程边界和网络通道的完整关系参见
[客户端系统架构](ClientArchitecture.md)；服务端结构参见
[IOCP 服务端系统架构](ServerArchitecture.md)。

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
- `m_connectionTestPending`、`m_driveListPending`、`m_remotePathCommandPending`：阻止同类请求重复提交
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
- [FileDownloadWorker.cpp](../src/client/FileDownloadWorker.cpp)
- [ScreenStreamWorker.cpp](../src/client/ScreenStreamWorker.cpp)
- [ControlStreamWorker.cpp](../src/client/ControlStreamWorker.cpp)

普通轻量命令使用短连接：

```text
用户操作
  → MainWindow 调用 RemoteClient
  → OneShotRequest 建立 TCP 连接并发送 Packet
  → 解析服务端响应
  → RemoteClient 发出业务信号
  → MainWindow 更新界面
```

可以先跟踪 `testConnection()`，它的数据最少、调用链最短。`OneShotRequest` 属于
`RemoteClient` 所在的 GUI 线程，但 `QTcpSocket` 使用事件循环异步收发，因此不会同步阻塞
界面。每个 `OneShotRequest` 实例只处理一次逻辑请求；目录列表虽然可能返回多个数据包，
仍然属于同一次请求。

`requestDriveList()`、`requestDirectoryListing()`、`openRemoteFile()` 和 `deleteRemotePath()` 复用同一个
`OneShotRequest` 模型。其中目录请求的服务端响应由多个 `FileEntry` 包和一个
`hasNext == false` 的终止包组成。

`OneShotRequest::onReadyRead()` 将 `QTcpSocket::readAll()` 返回的新数据追加到持久缓冲区，
再循环调用 `Packet::tryParse()`。完整数据包会立即分派处理；不完整数据会留在缓冲区，
等待下一次 `readyRead()`，从而同时处理 TCP 拆包、粘包和目录多包响应。每次连接成功或
收到新数据都会重新启动 15 秒无活动超时，请求完成时停止计时器。

#### 为什么 `OneShotRequest` 需要 `CallbackScope`

`OneShotRequest`、`RemoteClient` 和 `MainWindow` 位于同一个 GUI 线程，因此默认的
`Qt::AutoConnection` 会采用直接连接：`OneShotRequest` 发出结果信号时，会同步执行
`MainWindow::wireSignals()` 中对应的 lambda。部分 lambda 会调用
`QMessageBox::information()` 或 `QMessageBox::warning()`；这些模态函数在消息框关闭前
会运行一个嵌套 GUI 事件循环。

另一方面，服务端会在一次性请求的响应发送完成后主动断开连接。因此响应数据和 TCP 断开
事件可能相继到达，产生下面的回调重入：

```text
OneShotRequest::onReadyRead()
  → 解析响应并标记请求完成
  → emit connectionTested(...) 等结果信号
  → MainWindow 的直接连接 lambda
  → QMessageBox 打开模态对话框并运行嵌套事件循环
  → 嵌套事件循环处理服务端发来的 disconnected 事件
  → OneShotRequest::onDisconnected()
  → requestDeletion()
```

此时外层 `onReadyRead()` 仍停留在调用栈中。如果 `requestDeletion()` 立即调用
`deleteLater()`，嵌套事件循环可能在消息框关闭前处理 `DeferredDelete` 事件；消息框关闭
后，外层 `onReadyRead()` 将恢复执行并继续访问已经销毁的 `OneShotRequest`。这里主要防止
的是回调期间的提前销毁和 use-after-free，而不只是重复删除。

`CallbackScope` 只创建在 `OneShotRequest` 的 Qt 事件入口中：定时器 `timeout` 回调以及
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

这个保护针对的是会自行清理的 `OneShotRequest`，不是要求项目中每个普通 slot 都创建
`CallbackScope`。

三个 generation 分别管理不同范围的过期结果：

- `m_endpointGeneration`：地址或端口变化后，丢弃旧 endpoint 的一次性请求结果。
- `m_screenStreamGeneration`：停止监控后，丢弃旧监控会话返回的帧、错误和完成通知。
- `m_controlStreamGeneration`：停止控制后，丢弃旧控制会话返回的命令结果。

它们不会互相比较。endpoint 改变时，`setEndpoint()` 会分别让一次性请求、监控会话和
控制会话失效。

远程屏幕使用独立工作线程和持久连接：

```text
RemoteScreenWindow 立即请求首帧
  → RemoteClient 将任务投递给 ScreenStreamWorker
  → 工作线程复用 QTcpSocket 发送 WatchScreen Packet
  → 工作线程接收并解码 PNG
  → GUI 线程接收 QImage 并刷新画面
  → RemoteScreenWindow 按最高约 30 FPS 调度下一帧
```

同一时刻只允许一帧处于请求中。上一帧完成后，窗口根据 33 ms 的目标周期补足剩余等待时间，
因此实际帧率取 30 FPS 上限与截图、编码、网络和解码能力中的较低值。连接模型、generation 和
`m_screenFramePending` 的职责参见 [客户端系统架构](ClientArchitecture.md)。

`RemoteClient` 启动三个常驻工作线程，分别承载 `ScreenStreamWorker`、
`ControlStreamWorker` 和 `FileDownloadWorker`。GUI 线程通过
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 投递任务，析构时使用
`Qt::BlockingQueuedConnection` 先让 worker 停止，再退出并等待线程。

鼠标、模拟锁定和解锁使用单独的 `ControlStreamWorker` 长连接。控制命令一次只发送
一个并等待状态响应，连续鼠标移动只保留队列中最新的位置，防止输入积压。下载使用
`FileDownloadWorker` 在线程中接收数据并写入 `QSaveFile`；服务端每次最多读取 64 KiB 并
异步发送，不会把整个文件保存在内存中。每个发送批次完成后，IOCP completion worker 才把
下一次读取投递回文件任务池，因此慢客户端不会让文件 worker 阻塞等待网络。

### 第四步：服务端

先只阅读核心网络路径：

- [RemoteControlServer.cpp](../src/server/RemoteControlServer.cpp)：Qt 应用层适配器
- [RemoteControlTransport.h](../include/server/RemoteControlTransport.h)：公开的 PIMPL 边界
- [RemoteControlTransportInternal.h](../src/server/RemoteControlTransportInternal.h)：连接状态与 I/O 对象
- [RemoteControlTransport.cpp](../src/server/RemoteControlTransport.cpp)：IOCP 启停和收发完成通知
- [RemoteControlTransportProtocol.cpp](../src/server/RemoteControlTransportProtocol.cpp)：首包分类和命令路由
- [RemoteControlTransportFileTransfer.cpp](../src/server/RemoteControlTransportFileTransfer.cpp)：目录和下载的分批推进
- [RemoteControlTransportRuntime.cpp](../src/server/RemoteControlTransportRuntime.cpp)：任务池、注册表和状态机实现

托盘、UAC、注册表、模拟锁屏和 GDI 截图属于外围 Windows 集成，理解核心 IOCP 链路后再阅读
`ServerTrayController`、`ScreenLockService` 和 `WindowsPlatformIntegration`。

服务端主调用链：

```text
RemoteControlServer 启动 RemoteControlTransport
  → RemoteControlTransport 预投递 AcceptEx
  → Windows 内核完成 accept/recv/send 并把通知放入 completion port
  → IOCP completion worker 接收完成通知并解析 Packet
  → 网络状态机路由命令，阻塞任务投递到固定任务池
  → completion worker 按连接顺序异步发送响应
```

阅读时持续验证三个核心不变量：每条连接最多一个接收和一个发送在途；每次成功投递的
`OVERLAPPED` 最终恰好产生一次计数回收；只有赢得 `Closing` 转换的线程关闭 socket。目录和
下载通过 `FileTransferState` 在发送完成后分批续传，任务线程不会等待网络。

详细的线程数量、连接阶段、容量、背压、超时和停机顺序统一记录在
[IOCP 服务端系统架构](ServerArchitecture.md)。

这里的“锁定”不是 Windows 会话锁定。`ScreenLockWindow` 显示全屏覆盖窗口，
`WindowsPlatformIntegration` 隐藏任务栏并限制鼠标，`Ctrl+C` 可用于紧急解锁。该快捷键发出
`unlockRequested()`，再由 `ScreenLockService` 统一停止测试计时器、解锁并发布状态变化。因此它
适合演示远程控制流程，但不能作为操作系统安全边界。

### 第五步：公共协议

依次阅读 [Protocol.h](../include/common/Protocol.h)、[Protocol.cpp](../src/common/Protocol.cpp)、
[Packet.h](../include/common/Packet.h)、[Packet.cpp](../src/common/Packet.cpp) 和
[ProtocolTestMain.cpp](../src/tests/ProtocolTestMain.cpp)。重点理解 little-endian Packet 布局、
半包/粘包处理、状态 payload、目录终止条目以及不同命令允许出现的连接阶段。

字段布局、命令表和修改检查清单统一记录在
[远程控制协议参考](ProtocolReference.md)，不在学习路线中重复维护。

### 第六步：远程屏幕交互

- [RemoteScreenWindow.h](../include/client/RemoteScreenWindow.h)
- [RemoteScreenWindow.cpp](../src/client/RemoteScreenWindow.cpp)
- [RemoteScreenWindow.ui](../src/client/RemoteScreenWindow.ui)
- [ControlStreamWorker.cpp](../src/client/ControlStreamWorker.cpp)
- [ScreenStreamWorker.cpp](../src/client/ScreenStreamWorker.cpp)
- [RemoteControlTransport.cpp](../src/server/RemoteControlTransport.cpp)
- [ScreenLockWindow.cpp](../src/server/ScreenLockWindow.cpp)

这一部分适合学习：

- 自定义 `QWidget` 绘制
- 鼠标事件坐标转换
- `QTimer` 合并高频事件
- `QThread`、queued signal/slot 和对象线程归属
- 持久 TCP 连接与单帧流量控制
- 独立屏幕通道与控制通道，避免 head-of-line blocking
- 截图数据在网络中的传输与显示

### 第七步：用测试验证理解

推荐按下面顺序阅读和运行测试：

1. `RemoteControlProtocolTests`：先验证 Packet、状态 payload 和目录条目编码。
2. `RemoteControlConnectionStateTests`：观察单向状态转换、并发关闭和连接配额。
3. `RemoteControlTransportLifecycleTests`：观察连接到达期间的停止、I/O 取消和线程回收。
4. `RemoteControlTransportResilienceTests`：观察损坏流量、半包断开和并发请求隔离。
5. `RemoteControlSmokeTests`：最后连接完整服务端，验证所有业务路径。

前四个测试可直接通过 CTest 运行，不会操作鼠标、锁屏或文件；smoke test 会产生真实系统影响，
只应连接受控测试环境。

## 3. Qt 知识点索引

| 知识点 | 项目中的示例 |
| --- | --- |
| GUI 事件循环 | `QApplication` |
| Qt Designer | `MainWindow.ui`、`RemoteScreenWindow.ui`、`ScreenLockWindow.ui` |
| 信号槽 | `MainWindow::wireSignals()`、`RemoteClient` |
| TCP 客户端 | `QTcpSocket` |
| Windows 异步服务端 | IOCP、`AcceptEx`、`WSARecv`、`WSASend` |
| 对象生命周期 | `QObject` parent、`std::unique_ptr` |
| 工作线程 | `QThread`、worker object、queued/blocking queued connection |
| 定时任务 | `QTimer` |
| 文件系统 | `QFile`、`QDir`、`QFileInfo`、`QSaveFile` |
| 图片与绘制 | `QImage`、`QPainter`、Windows GDI |
| 系统集成 | `QSystemTrayIcon`、`QSettings`、Windows API |
| CMake Qt 集成 | `AUTOMOC`、`AUTOUIC`、Qt target linking |

## 4. 建议的学习练习

1. 从“测试连接”按钮开始，为调用链逐步加断点。
2. 在 `handleInitialPacket()`、`handleReceiveCompletion()` 和 `handleSendCompletion()` 设置断点，
   跟踪同一 `connection_id` 的完整生命周期。
3. 新增一个不修改系统状态的协议命令，例如返回服务端版本信息。
4. 阅读连接状态测试，再补充“关闭与分类竞争”的测试场景。
5. 观察远程屏幕刷新频率，分析截图、PNG 编码、网络和客户端调度各自的耗时。

## 5. 调试建议

- 客户端和服务端分别启动调试时，确认端口一致。
- 遇到 Qt 类型跳转失败，先确认项目根目录的 `compile_commands.json` 存在且来自当前构建
  目录，再重启 `clangd`。
- 遇到 `ui_*.h` 缺失，先完成一次构建；这些头文件由 `AUTOUIC` 生成。
- 修改 `Q_OBJECT` 类后出现链接错误时，检查头文件是否包含在 CMake target 中，并重新运行 configure。
- 修改公共协议或 IOCP 生命周期后先运行完整 CTest；涉及实际业务行为时，再启动服务端运行
  `RemoteControlSmokeTests`。
