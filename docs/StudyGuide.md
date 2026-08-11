# 项目代码学习指南

本文用于安排代码阅读顺序、明确每个阶段的学习目标，并给出判断“是否真正理解”的标准。
开始逐段阅读代码前，可以先通过[项目功能与技术实现](FeaturesAndDesign.md)建立功能视角。构建和
运行参数统一查阅项目 [README](../README.md)，组件细节分别查阅
[客户端系统架构](ClientArchitecture.md)、[IOCP 服务端系统架构](ServerArchitecture.md)和
[远程控制协议参考](ProtocolReference.md)。

具备 C++ 和 Qt 基础时，只理解客户端主要调用链通常需要 15～25 小时，理解 IOCP 服务端主要调用链
通常还需要 15～25 小时。完成下表的全部阅读和练习约需 36～61 小时；若要安全修改并发关闭和停机
流程，通常需要在服务端部分累计投入 25～40 小时。

## 1. 学习方法

不要以“逐行读完”作为完成标准。每个阶段都建议完成下面四件事：

1. 从一个用户操作或测试入口画出主调用链。
2. 标注链路中每个对象所属的线程和所有者。
3. 说明异步操作的开始条件、完成条件和失败路径。
4. 使用断点或测试验证自己的判断。

第一次阅读只跟踪正常路径；第二次再补充超时、断线、过期结果和关闭流程。看到 Qt signal 时，
需要同时确认发送者线程、接收者线程和连接类型；看到裸指针时，需要确认它是 QObject parent
管理的对象还是非 owning 引用。

## 2. 项目地图

```text
RemoteControlClient
    ├─ OneShotRequest ───────────────► 一次性请求
    ├─ FileDownloadWorker ──────────► 一次性下载连接
    ├─ ScreenStreamWorker ──────────► 屏幕长连接
    └─ ControlStreamWorker ─────────► 控制长连接
                                      │
                                      ▼
RemoteControlServer ─► RemoteControl::ServerTransport ─► IOCP / task pools
```

| 目录 | 主要职责 |
| --- | --- |
| `include/common`、`src/common` | 协议类型和 Packet 编解码 |
| `include/client`、`src/client` | Qt 客户端界面、网络 facade 和 worker |
| `include/server`、`src/server` | 服务端 Qt 生命周期和 Windows 主机能力 |
| `server_transport` | 独立 IOCP transport target、状态机和任务池 |
| `tests` | 协议、状态机、transport 和端到端验证 |

`RemoteControlServer` 是唯一服务端程序；`RemoteControl::ServerTransport` 是被服务端和测试共同
链接的静态库 target，不是第二个服务端。

## 3. 分阶段阅读路线

| 阶段 | 主题 | 建议投入 | 完成标志 |
| --- | --- | ---: | --- |
| 0 | 建立可运行基线 | 1～2 小时 | 能构建并运行 CTest |
| 1 | 公共协议 | 2～4 小时 | 能手工说明一个 Packet 的布局 |
| 2 | 程序入口与客户端界面 | 3～5 小时 | 能从按钮跟踪到业务接口 |
| 3 | 客户端网络与线程 | 8～12 小时 | 能解释四种连接模型和关闭顺序 |
| 4 | 远程屏幕交互 | 3～5 小时 | 能解释单帧在途和鼠标节流 |
| 5 | 服务端应用边界 | 3～5 小时 | 能说明 Qt/Windows 与 IOCP 的边界 |
| 6 | IOCP transport | 12～20 小时 | 能解释一次请求和安全停机 |
| 7 | 测试与修改练习 | 4～8 小时 | 能用测试验证自己的修改 |

### 阶段零：建立可运行基线

先完成一次 Debug 构建，并运行无系统副作用的测试：

```powershell
.\scripts\Build.ps1
ctest --test-dir .\build\msvc-debug --output-on-failure
```

本阶段需要确认：

- `RemoteControlClient`、`RemoteControlServer` 和各测试 target 都能生成。
- 根目录 `compile_commands.json` 已生成，clangd 可以跳转到项目源码。
- 知道端到端 smoke test 会操作服务端文件、屏幕和鼠标，不能把它当作普通 CTest 随意运行。

### 阶段一：公共协议

按下面顺序阅读：

1. [Protocol.h](../include/common/Protocol.h)：命令、状态码和共享数据结构。
2. [Packet.h](../include/common/Packet.h)：Packet 对外接口。
3. [Packet.cpp](../src/common/Packet.cpp)：序列化、半包、粘包和错误恢复。
4. [Protocol.cpp](../src/common/Protocol.cpp)：状态和目录条目的 payload 编解码。
5. [ProtocolTests.cpp](../tests/ProtocolTests.cpp)：用测试确认边界行为。

完成后应能回答：

- TCP 为什么不能假设一次 `readyRead` 对应一个 Packet？
- `StatusCode::Failure` 和 `StatusCode::Success` 在哪里定义、怎样编码？
- 目录列表为什么需要一个 `hasNext == false` 的终止条目？
- 下载的第一个响应 Packet 为什么只携带文件总长度？

精确字段布局统一查阅 [远程控制协议参考](ProtocolReference.md)。

### 阶段二：程序入口与客户端界面

入口文件：

- [ClientMain.cpp](../src/client/ClientMain.cpp)
- [MainWindow.h](../include/client/MainWindow.h)
- [MainWindow.cpp](../src/client/MainWindow.cpp)
- [MainWindow.ui](../src/client/MainWindow.ui)

建议跟踪最短的“测试连接”调用链，再跟踪目录加载：

```text
QApplication::exec()
  → 用户触发 QAction / QPushButton
  → MainWindow slot
  → RemoteClient 业务接口
  → 结果 signal
  → MainWindow 更新状态和控件
```

重点理解 `setupUi()`、`wireSignals()`、`updateActionState()` 和目录树的
`DirectoryLoadState`。目录状态按下面的方向变化：

```text
Unloaded → Loading → Loaded
              └─失败→ Unloaded

Loaded → Refreshing → Loaded
              └─失败→ Loaded（保留旧缓存）
```

完成标志：能够说明 `QTreeWidgetItem` 中保存的远程路径、加载状态和目录缓存分别由谁写入、
何时失效，以及为什么同类 pending 状态通常只在 GUI 线程访问而不需要加锁。

### 阶段三：客户端网络与线程

先阅读 [客户端系统架构](ClientArchitecture.md)，再按下面顺序进入代码：

1. [RemoteClient.h](../include/client/RemoteClient.h)：对 GUI 暴露的业务接口和线程成员。
2. [RemoteClient.cpp](../src/client/RemoteClient.cpp)：构造、析构、`OneShotRequest` 和结果转发。
3. [FileDownloadWorker.cpp](../src/client/FileDownloadWorker.cpp)：流式下载和 `QSaveFile`。
4. [ScreenStreamWorker.cpp](../src/client/ScreenStreamWorker.cpp)：屏幕长连接和单帧在途。
5. [ControlStreamWorker.cpp](../src/client/ControlStreamWorker.cpp)：控制握手、顺序队列和移动合并。

从 `RemoteClient::testConnection()` 开始跟踪 `OneShotRequest`。它和 GUI 位于同一线程，但
`QTcpSocket` 由事件循环异步驱动，因此等待网络时不会同步阻塞界面。目录请求虽然返回多个
Packet，仍然属于一个逻辑请求和一个 `OneShotRequest`。Packet 解析和目录条目累计也在 GUI
回调中执行，因此“异步网络”只表示不阻塞等待，并不代表所有 CPU 处理都在工作线程。

三个常驻 worker 分别属于屏幕、控制和下载线程。GUI 使用
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 投递任务；`RemoteClient` 析构时也向每个
worker 投递收尾回调，由该回调在所属线程中依次执行 `shutdown()` 和 `quit()`，析构线程再调用
`wait()`。同一个回调明确保证清理先于退出，并避免把停机拆成两段相互等待的阻塞操作。

下载需要同时检查 endpoint generation 和 download generation：前者淘汰旧服务器的结果，
后者区分同一服务器上的前后两次下载或取消。endpoint 改变时两者都会递增；新下载开始时只
递增 download generation。取消通过中止下载 socket 实现，不会向服务端发送额外命令。

#### 重点案例：为什么需要 `CallbackScope`

`OneShotRequest` 发出结果 signal 时，同线程 GUI slot 默认会被同步调用。某些 slot 会显示模态
`QMessageBox`，从而启动嵌套事件循环；服务端的 TCP 断开事件可能在外层回调返回前被处理：

```text
OneShotRequest::onReadyRead()
  → emit 结果 signal
  → GUI slot 打开 QMessageBox
  → 嵌套事件循环处理 disconnected
  → OneShotRequest::onDisconnected()
  → requestDeletion()
```

如果嵌套事件循环立即处理 `deleteLater()`，外层 `onReadyRead()` 恢复后可能访问已经销毁的对象。
`CallbackScope` 因此只放在真正的 Qt 事件入口，记录当前仍在调用栈中的回调层数；清理请求先进入
`CleanupDeferred`，最外层回调退出后才进入 `DeletionScheduled`。它防止的是回调期间提前销毁，
不是要求每个普通 slot 都使用相同保护。

完成后应能回答：

- 一次性请求、下载、屏幕流和控制流为什么使用不同连接模型？
- worker 为什么必须在所属线程创建和操作 `QTcpSocket`？
- endpoint、download、screen stream 和 control stream generation 分别淘汰哪些旧结果？
- 为什么下载结果需要同时匹配 endpoint generation 与 download generation？
- `quit()`、`wait()`、worker `shutdown()` 分别负责什么？

### 阶段四：远程屏幕交互

按职责而不是文件大小阅读：

- [RemoteScreenWindow.cpp](../src/client/RemoteScreenWindow.cpp)：窗口生命周期和 30 FPS 上限调度。
- [RemoteScreenWidget.cpp](../src/client/RemoteScreenWidget.cpp)：绘制、坐标映射和鼠标移动节流。
- [ScreenStreamWorker.cpp](../src/client/ScreenStreamWorker.cpp)：帧请求、解码和持久连接。
- [ControlStreamWorker.cpp](../src/client/ControlStreamWorker.cpp)：鼠标与锁定命令通道。

```text
请求一帧 → worker 发送 WatchScreen → 收到并解码 PNG → GUI 更新画面
       ▲                                             │
       └──────── 补足 33 ms 周期后请求下一帧 ────────┘
```

同一时刻只允许一帧在途；实际帧率是 30 FPS 上限与截图、编码、网络、解码能力中的较低值。
鼠标移动按固定间隔只发送最新位置；离散鼠标事件发出前先刷新待发送移动，关闭监控窗口时则丢弃
尚未发送的移动，避免打乱输入顺序或重新打开已停止的控制连接。

完成标志：能够说明 widget 坐标如何映射到远程图像坐标，以及屏幕数据和鼠标命令为什么使用
两条独立长连接。

### 阶段五：服务端应用边界

先暂时忽略 IOCP 内部实现，阅读：

1. [ServerMain.cpp](../src/server/ServerMain.cpp)：命令行模式和 QApplication 生命周期。
2. [RemoteControlServer.cpp](../src/server/RemoteControlServer.cpp)：创建 host services 和 transport。
3. [RemoteControlHostServices.h](../server_transport/include/RemoteControlHostServices.h)：transport 所需的主机能力接口。
4. [WindowsRemoteControlHostServices.cpp](../src/server/WindowsRemoteControlHostServices.cpp)：Qt/Windows 适配器。
5. [ScreenLockService.cpp](../src/server/ScreenLockService.cpp) 和
   [WindowsPlatformIntegration.cpp](../src/server/WindowsPlatformIntegration.cpp)：外围系统能力。

这里需要建立清晰边界：IOCP target 负责连接和协议推进；`RemoteControlHostServices` 提供文件、
屏幕、鼠标和锁定能力；需要操作 Qt GUI 对象的锁定请求会被投递回 GUI 线程。

项目中的“锁定”是应用级模拟锁定，不是 Windows 会话锁定，也不是安全边界。托盘、UAC、启动项
和 GDI 截图属于外围能力，应放在核心 transport 之后学习。

### 阶段六：IOCP transport

先阅读 [IOCP 服务端系统架构](ServerArchitecture.md)，然后分三遍阅读实现。

第一遍只看类型和所有权：

- [RemoteControlTransport.h](../server_transport/include/RemoteControlTransport.h)
- [RemoteControlTransportImpl.h](../server_transport/internal/RemoteControlTransportImpl.h) 中的
  `ConnectionPhase`、`ConnectionStateMachine`、`ConnectionContext`、`ConnectionRegistry` 和
  `IoOperation`

第二遍跟踪一条 `TestConnection`：

```text
start()
  → postAccept()
  → handleAcceptCompletion()
  → postReceive()
  → handleReceiveCompletion()
  → handleInitialPacket()
  → enqueuePacket()
  → handleSendCompletion()
  → closeConnection()
```

对应实现主要位于：

- [RemoteControlTransport.cpp](../server_transport/src/RemoteControlTransport.cpp)
- [RemoteControlTransportProtocol.cpp](../server_transport/src/RemoteControlTransportProtocol.cpp)

第三遍再阅读阻塞任务和关闭流程：

- [RemoteControlTransportFileTransfer.cpp](../server_transport/src/RemoteControlTransportFileTransfer.cpp)
- [RemoteControlTransportRuntime.cpp](../server_transport/src/RemoteControlTransportRuntime.cpp)
- [RemoteControlTransportLog.cpp](../server_transport/src/RemoteControlTransportLog.cpp)
- `RemoteControlTransport::stop()`

阅读过程中始终验证三个不变量：

1. 每条活动连接最多一个 `WSARecv` 和一个 `WSASend` 在途。
2. 每个成功投递的 `OVERLAPPED` 最终恰好产生一次 pending 计数回收。
3. 只有赢得 `Closing` 状态转换的线程移除注册表条目并关闭 socket。

完成标志：能够解释 `IoOperation` 为什么持有连接 `shared_ptr`、阻塞工作为什么进入固定任务池、
慢客户端下载为什么不会阻塞文件 worker，以及服务端为何必须排空 completion 后才能销毁 IOCP。

### 阶段七：用测试验证理解

推荐按由小到大的顺序阅读：

1. [ProtocolTests.cpp](../tests/ProtocolTests.cpp)：Packet 和 payload 边界。
2. [ClientWorkerLifecycleTests.cpp](../tests/ClientWorkerLifecycleTests.cpp)：下载取消、替换隔离和 worker 回收。
3. [ConnectionStateMachineTests.cpp](../tests/ConnectionStateMachineTests.cpp)：状态转换与配额。
4. [TransportLifecycleTests.cpp](../tests/TransportLifecycleTests.cpp)：启动、停止和资源回收。
5. [TransportResilienceTests.cpp](../tests/TransportResilienceTests.cpp)：损坏流量和并发隔离。
6. [SmokeTests.cpp](../tests/SmokeTests.cpp)：完整客户端到服务端业务。

前五项由 CTest 自动运行，不会执行鼠标、锁定、删除或文件运行。`RemoteControlSmokeTests` 会产生
真实系统影响，只应连接受控环境。

## 4. 建议练习

1. 为“测试连接”调用链画出对象、线程和 signal/slot 关系。
2. 给同一条服务端连接按 `connection_id` 整理 accept、recv、send 和 close 日志。
3. 新增一个无系统副作用的命令，例如返回服务端版本，并同步更新协议测试和文档。
4. 为连接状态机补充一个非法转换或并发关闭测试。
5. 测量远程画面中截图、PNG 编码、网络、解码和帧调度各自的耗时。
6. 解释 `stop()` 中任意两步交换顺序可能造成的资源生命周期问题。

## 5. 调试检查清单

- 客户端和服务端端口必须一致，服务端端口也不能被其他进程占用。
- clangd 跳转异常时，确认根目录 `compile_commands.json` 来自当前构建目录，再重启 clangd。
- `ui_*.h` 由 AUTOUIC 生成；缺失时先重新 configure 和 build。
- 修改带 `Q_OBJECT` 的类后出现链接错误时，确认头文件已列入对应 CMake target。
- 跨线程 signal/slot 行为异常时，先确认对象的 thread affinity 和实际连接类型。
- 修改协议、连接状态或关闭流程后先运行完整 CTest，再在受控环境运行 smoke test。
