# 设计思想与设计模式

本文说明 Remote Control Qt 为什么采用当前结构，以及修改代码时必须保持哪些设计约束。它关注
职责边界、异步协作、资源所有权、状态转换和工程取舍，不重复功能操作步骤、完整线程图或协议字段。

- 项目实现了什么：参见[项目功能与技术实现](FeaturesAndDesign.md)。
- 客户端对象和线程如何连接：参见[客户端系统架构](ClientArchitecture.md)。
- 服务端 IOCP 如何工作：参见[IOCP 服务端系统架构](ServerArchitecture.md)。
- Packet 和命令如何编码：参见[远程控制协议参考](ProtocolReference.md)。

文中使用“体现某种模式”或“某种风格”，表示代码借用了该模式的核心思想；除非明确说明，
不代表项目实现了教科书中的完整 GoF 模式。

## 阅读路线

- **用 5 分钟建立整体认识**：[设计总览](#设计总览)。
- **已经读完客户端代码**：[客户端设计](#客户端设计)和[跨模块设计思想](#跨模块设计思想)。
- **正在学习 IOCP 服务端**：[服务端设计](#服务端设计)和
  [安全关闭顺序](ServerArchitecture.md#安全关闭顺序)。
- **理解一个功能如何组合多种原则**：[典型功能如何组合这些思想](#典型功能如何组合这些思想)。
- **准备修改项目代码**：先阅读对应设计章节，再使用
  [修改代码时的检查清单](#修改代码时的检查清单)。

按源码进度学习时，客户端设计配合
[学习指南阶段三](StudyGuide.md#阶段三客户端网络与线程)阅读，服务端设计配合
[学习指南阶段六](StudyGuide.md#阶段六iocp-transport)阅读。

## 设计总览

客户端依靠 Qt 事件循环，以 Reactor 风格处理异步网络事件；服务端使用 IOCP 的 Proactor 模型，
并通过有界任务池隔离阻塞工作。两端共享 Packet 协议，并共同使用状态机、资源所有权、背压和
关闭协议保证异步操作的正确性。

项目贯穿以下原则：

- **按变化原因划分边界**
  - 问题：UI、协议、网络和 Windows API 容易相互污染。
  - 做法：UI 调用客户端 facade；transport 用 PIMPL 隔离 Windows 网络；Windows 主机能力留在
    adapter。
- **用消息跨越线程**
  - 问题：GUI 与 worker 共享可变对象容易产生竞态。
  - 做法：使用 Qt signal/slot 和 `Qt::QueuedConnection` 投递不可变参数或值快照。
- **显式表达生命周期**
  - 问题：多个布尔值容易组合出非法状态。
  - 做法：为请求、连接、下载、目录缓存和屏幕帧建立枚举状态机。
- **所有资源都必须有所有者**
  - 问题：异步回调可能访问已经释放的对象。
  - 做法：组合使用 Qt parent、`std::unique_ptr`、`std::shared_ptr`、`std::weak_ptr` 和 RAII。
- **高频或重负载的关键队列应有上限**
  - 问题：慢客户端或高频输入可能无限占用内存。
  - 做法：使用有界任务池、连接配额、发送积压限制、单帧在途和鼠标移动合并。
- **完成事件驱动下一步**
  - 问题：固定频率生产会让慢消费者持续积压。
  - 做法：一帧或一批发送完成后，才调度下一帧、目录批次或下载块。
- **关闭是一段协议**
  - 问题：单独调用 `quit()` 或关闭 handle 不能保证安全。
  - 做法：先拒绝新工作，再清理或取消，排空完成通知，最后 join 并释放资源。

## 客户端设计

### Facade：统一客户端业务入口

**要解决的问题**

`MainWindow` 和 `RemoteScreenWindow` 不应了解单请求对象、三个工作线程、socket、超时定时器和
generation 的组合方式，否则每个界面都会复制网络生命周期逻辑。

**当前实现**

`RemoteClient` 对界面提供连接测试、目录、文件、下载、屏幕和控制等业务接口。它在内部创建
`OneShotRequest`，管理三个 worker 线程，校验异步结果的 generation，再把业务 signal 转发给 UI。

代表实现：[RemoteClient.h](../include/client/RemoteClient.h)和
[RemoteClient.cpp](../src/client/RemoteClient.cpp)。界面与 facade 的协作关系参见
[客户端总体架构](ClientArchitecture.md#总体架构)。

**必须保持的约束**

- UI 通过 `RemoteClient` 的业务接口发起网络操作，不直接持有 worker 或 socket。
- worker 的结果先在 `RemoteClient` 中完成 generation 校验，再转发给界面。
- `RemoteClient` 仍应聚焦网络协调；纯界面状态、目录树展示和进度窗口留在对应 widget/window。

**取舍**

Facade 降低了 UI 与网络实现的耦合，也集中管理线程关闭和过期结果。代价是 `RemoteClient` 同时是
facade 和应用协调器，不是完全无逻辑的薄转发层；继续增加功能时要防止它膨胀为 God Object。

### Worker Object：让持续任务拥有明确线程归属

**要解决的问题**

屏幕流、控制流和下载持续时间较长，并拥有 socket、定时器或文件对象。如果这些对象由 GUI 线程
同步操作，界面会被网络等待和文件写入拖慢；如果任意线程直接访问，又会违反 QObject 线程归属。

**当前实现**

`ScreenStreamWorker`、`ControlStreamWorker` 和 `FileDownloadWorker` 创建后分别
`moveToThread()`。GUI 使用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 投递任务，
worker 通过 signal 返回结果。socket 和文件对象延迟到 worker 线程中创建。

代表实现：线程创建、结果转发和 `stopWorkerThread()` 位于
[RemoteClient.cpp](../src/client/RemoteClient.cpp)；三个 worker 的职责和关闭关系参见
[客户端线程划分](ClientArchitecture.md#线程划分)。

**必须保持的约束**

- worker 的 socket、timer 和文件只能在 worker 所属线程中使用和销毁。
- 跨线程调用必须使用排队投递，不能从 GUI 线程直接调用操作 socket 的成员函数。
- worker 构造时创建的 timer 尚未启动，并作为子对象随 worker 一起迁移；不能在
  `moveToThread()` 前启动它。socket 和 `QSaveFile` 则在 worker 线程中延迟创建。
- worker 必须在所属线程完成资源收尾并请求事件循环退出，外部线程才能等待其结束；具体顺序参见
  [对象所有权与关闭顺序](ClientArchitecture.md#对象所有权与关闭顺序)。
- `OneShotRequest` 不属于 Worker Object；它位于 GUI 线程，但使用异步 `QTcpSocket`。

**取舍**

三个专用常驻线程使职责和线程归属直观，避免每次操作创建线程。代价是每个客户端固定占用三个
线程；这不是客户端线程池，也不是完整的 Active Object 或 Actor 模型。

### Reactor 风格：由 Qt 事件循环报告网络就绪

**要解决的问题**

客户端不能在 GUI 或 worker 中调用阻塞式读取并等待服务端响应。

**当前实现**

`OneShotRequest` 和三个 worker 连接 `QTcpSocket::connected`、`readyRead`、`disconnected`、
`errorOccurred` 以及 `QTimer::timeout`。代码只发起非阻塞连接或写入，后续步骤由 Qt 事件循环在
socket 就绪、断开或超时时回调。

**必须保持的约束**

- 一个槽函数不能假设一次 `readyRead` 对应一个完整 Packet。
- 回调中仍需检查当前状态、命令类型、payload 和超时条件。
- 异步 I/O 只表示不阻塞等待；在 GUI 线程解析超大响应仍可能造成短时负载。

**取舍**

项目使用的是 Qt 提供的 Reactor 风格机制，并没有自行实现 Reactor。事件驱动减少同步等待，但调用
顺序不再等同于普通函数栈，因此必须配合状态机、generation 和明确的完成 signal。

### Generation Token：丢弃过期异步结果

**要解决的问题**

用户切换 endpoint、停止监控或开始下一次下载后，旧 worker 的结果可能已经排入 GUI 事件队列。
仅关闭 socket 无法撤回所有已经排队的 signal。

**当前实现**

`RemoteClient` 分别维护 endpoint、download、screen stream 和 control stream generation。
发起操作时把当前值作为不可变快照传给 worker；结果返回时，只有与当前 generation 匹配才会转发。

代表实现：[RemoteClient.cpp](../src/client/RemoteClient.cpp)负责 generation 的推进与校验；两级下载
generation 的完整场景参见[客户端结果有效性](ClientArchitecture.md#结果有效性)。

**必须保持的约束**

- generation 的递增和最终比较都在 `RemoteClient` 所属 GUI 线程完成。
- worker 只携带快照并原样返回，不能自行决定当前 generation。
- generation 不是互斥锁，也不是取消操作；底层 socket、timer 和文件仍需单独停止或清理。
- endpoint generation 标识服务器配置，download/stream generation 标识该 endpoint 下的一次业务会话。
- endpoint 改变时必须同时停止 screen/control stream 并投递下载取消，由这些操作分别递增或携带
  对应的业务 generation；screen/control 回调本身不携带 endpoint generation。

**取舍**

这种异步失效令牌成本低，不必从 Qt 队列中删除旧事件。代价是每条异步结果链都必须完整携带并
校验相应 generation，漏检会让旧结果污染新界面。

## 服务端设计

### PIMPL：隔离 IOCP 实现细节

**要解决的问题**

公开 transport 接口不应暴露 `SOCKET`、`HANDLE`、`OVERLAPPED`、锁、线程池和连接上下文，
否则应用层会依赖 Windows 网络实现，公共头的任何变化也会扩大重新编译范围。

**当前实现**

`RemoteControlTransport` 只公开 `start()`、`stop()` 和 `listeningPort()`，通过
`std::unique_ptr<Impl>` 持有内部实现。完整的 `RemoteControlTransport::Impl` 只在 target 的
`internal` 目录和实现文件中可见。

代表实现：[RemoteControlTransport.h](../server_transport/include/RemoteControlTransport.h)和
[RemoteControlTransportImpl.h](../server_transport/internal/RemoteControlTransportImpl.h)。

**必须保持的约束**

- 新增 Windows socket 状态时优先放入 `Impl`，不要泄漏到公开头文件。
- 公开层只负责稳定接口和委托；实际停机由 `Impl::stop()` 保证幂等。
- 注入的 `RemoteControlHostServices` 必须比 transport 活得更久。
- 一个 transport 实例只有一次启动生命周期。`stop()` 后 stopping 状态和任务池均为终态；若要重新
  启动，必须创建新的 transport 实例。

**取舍**

PIMPL 隔离了依赖和编译影响，代价是一次独占堆分配和间接访问。本项目使用静态库，因此主要收益是
边界清晰和编译隔离，而不是承诺跨版本二进制 ABI 稳定。

### Dependency Inversion 与 Port/Adapter：反转主机能力依赖

**要解决的问题**

IOCP transport 需要磁盘、文件打开、鼠标、截图和锁屏能力，但不能直接依赖 Qt 窗口或散落的
Windows API，否则 transport 无法独立测试，也难以保证 GUI 操作回到 GUI 线程。

**当前实现**

`RemoteControlHostServices` 是 transport 所需的主机能力 port。生产环境注入
`WindowsRemoteControlHostServices` adapter，由它调用 Windows 平台能力，并把锁屏请求排队到
`ScreenLockService` 所属 GUI 线程。transport 测试则注入无系统副作用的 fake 实现。

代表实现：[RemoteControlHostServices.h](../server_transport/include/RemoteControlHostServices.h)定义 port，
[WindowsRemoteControlHostServices.cpp](../src/server/WindowsRemoteControlHostServices.cpp)提供生产 adapter。

**必须保持的约束**

- host services 可能从 IOCP 或任务池线程调用，具体实现必须满足相应线程安全要求。
- Qt GUI 对象只能通过排队调用回到 GUI 线程。
- host services 的生命周期必须覆盖 transport 的所有 worker 和完成通知。
- transport 测试默认不能产生鼠标、锁屏、文件执行等系统副作用。

**取舍**

依赖倒置让 transport 不依赖具体 UI 和 Windows 服务实现，也便于测试。当前接口聚合了六类能力，
因此不应声称严格实现了 Interface Segregation Principle；只有能力继续增长并造成调用方依赖膨胀时，
才值得拆成 file、screen、input、lock 等更小 port。

### Proactor：由 IOCP 报告异步操作完成

**项目定位**

IOCP/Overlapped I/O 的通用完成模型、API 语义和最小 worker 循环参见外部
[IOCP 学习路线](https://github.com/slimesq/IOCP/blob/main/docs/IOCP%E5%AD%A6%E4%B9%A0%E8%B7%AF%E7%BA%BF.md)。
本节只记录该模型如何约束当前 transport，不重复 IOCP 入门教学。

**当前实现**

transport 先投递 `AcceptEx`、`WSARecv` 和 `WSASend`，再由共享 completion worker 消费结果并
推进协议与连接状态；项目把 operation 类型、buffer 和连接保活信息集中在 `IoOperation` 中。

代表实现：[RemoteControlTransport.cpp](../server_transport/src/RemoteControlTransport.cpp)；连接角色和完整
I/O 流程参见[服务端系统架构](ServerArchitecture.md#一条连接的-io-流程)。

**必须保持的约束**

- 每条连接最多只有一个 receive 和一个 send 在途。
- pending 必须在异步 API 调用前登记；同步投递失败立即回滚，成功投递后由最终 completion 恰好回收一次。
- 部分发送完成后必须继续发送剩余字节，不能把一次完成误认为整个 Packet 已发送。
- completion worker 只推进短小状态，不执行慢磁盘、shell 或 GDI 工作。
- 停机期间必须拒绝新 I/O，并让已经投递的 operation 返回 completion 后再释放完成端口；完整步骤
  参见[安全关闭顺序](ServerArchitecture.md#安全关闭顺序)。

**取舍**

IOCP 用少量 completion worker 支撑较多连接，但对象生命周期和关闭顺序明显比 Qt 客户端复杂。
服务端外围的阻塞任务池不是 Proactor，它们只是隔离无法异步化或不适合在 completion worker 中执行的工作。

### Producer–Consumer 与工作负载隔离

**要解决的问题**

文件系统、shell 和截图编码可能阻塞。如果在 IOCP completion worker 中执行，某一类慢任务会拖延
所有连接；如果每次创建线程，又会产生频繁的线程申请和销毁。

**当前实现**

`TaskPool` 使用固定 `std::thread`、有界 `deque`、mutex 和 condition variable。协议处理是 producer，
任务线程是 consumer。服务端分别设置 shell-command、file 和 screen-capture 三个池，形成
bulkhead-inspired 的工作负载隔离。

代表实现：[RemoteControlTransportImpl.h](../server_transport/internal/RemoteControlTransportImpl.h)定义
`TaskPool`，[RemoteControlTransportRuntime.cpp](../server_transport/src/RemoteControlTransportRuntime.cpp)
实现其队列与线程生命周期。

**必须保持的约束**

- 队列满或 pool 停机时，`submit()` 必须立即失败，调用方必须转成状态响应或关闭原因。
- pool 停机后不能接受新任务，所有正在执行的任务必须有机会结束，线程才能被 join。
- 慢任务不能持有连接锁等待网络发送；应生成有限结果后交回有序发送队列。

**取舍**

固定任务池避免频繁创建线程，并防止一种负载占满 completion worker。代价是服务端存在多组线程和
跨池协调；它体现了隔舱思想，但不是完整的容错或熔断框架。

## 跨模块设计思想

### Observer 风格：用 signal/slot 发布结果

Qt signal/slot 让 worker 不依赖具体窗口，让 UI 不必轮询网络状态。跨线程连接会把结果排入接收者
事件循环，同线程连接则可能同步执行槽函数。

代表实现：[RemoteClient.cpp](../src/client/RemoteClient.cpp)负责 worker 结果转发；窗口与 signal 的边界
参见[客户端总体架构](ClientArchitecture.md#总体架构)。

约束和取舍：

- UI 更新必须最终在 GUI 线程执行。
- 接收者销毁后依赖 Qt 自动断开，不保留悬空回调目标。
- signal 发出后可能启动模态对话框的嵌套事件循环，因此 `OneShotRequest::CallbackScope` 会等回调
  完全退出后再安排删除。
- `CallbackScope` 只放在 timeout、connected、ready-read、disconnected 和 error 等真正的 Qt
  事件入口，不在 `handlePacket()`、`fail()` 等内部 helper 中重复嵌套；完成路径先
  `markFinished()` 再 emit，避免嵌套事件循环触发第二次完成。
- `invokeMethod(Qt::QueuedConnection)` 是命令投递，不是 Observer 本身；项目只采用 Observer 风格，
  没有自行实现通用观察者框架。

### 显式有限状态机：限制合法转换

项目使用 enum 和集中转换逻辑表达状态，而不是为每个状态创建多态对象，因此这里是显式有限
状态机思想，不是 GoF State 模式。

- **`OneShotRequest::RequestState`**：处理完成、嵌套回调和延迟删除；请求只完成一次，活动回调
  退出前不删除对象。
- **`DirectoryLoadState`**：处理目录缓存与刷新；初次失败回到 `Unloaded`，刷新失败保留旧
  `Loaded` 缓存。
- **`ScreenStreamState`**：处理单帧请求和停机；同一客户端屏幕 worker 最多一帧在途。
- **`ControlStreamState`**：处理连接、握手、命令响应和停机；`Ready` 前不发送业务命令，一次只
  等待一个响应。
- **`DownloadState`**：处理下载互斥和销毁；同一 worker 只执行一个下载，`ShuttingDown` 不再
  接受工作。
- **`ConnectionStateMachine`**：处理服务端连接分类和并发关闭；首包角色不可切换，只有一个线程
  能赢得 `Closing` 转换。
- **`ScreenFrameFlowState`**：处理服务端重复帧请求；最多一帧执行，并只合并一个额外请求意图。

客户端状态的职责参见[客户端系统架构](ClientArchitecture.md)，服务端连接状态及转换参见
[连接角色与任务路由](ServerArchitecture.md#连接角色与任务路由)。

连接测试、磁盘列表和文件命令等 pending 标志表示可以并行的正交操作，不应为了“统一”而强行合成
一个全局状态机。

### RAII、Scope Guard 与所有权分层

项目组合使用 C++ RAII、Qt parent 和智能指针，不要求所有资源采用同一种所有权形式。

- **widget、timer、socket 等 QObject**：使用 Qt parent 或所属线程中的 `deleteLater()`，自动随
  对象树或线程生命周期清理。
- **UI 类和非 QObject 独占对象**：使用 `std::unique_ptr` 明确唯一所有者。
- **transport 连接上下文**：使用 `std::shared_ptr`，保证关联的 recv/send completion 返回前上下文
  仍然存在。
- **后台任务中的连接引用**：使用 `std::weak_ptr`，避免排队任务延长已关闭连接的生命周期。
- **`WinsockRuntime`、`TaskPool` 和 transport `Impl`**：在析构中执行成对清理，让异常和提前返回
  也能进入统一释放路径。
- **`CallbackScope`**：作为 scope guard，在嵌套事件循环期间延迟删除 `OneShotRequest`。
- **`QSaveFile`**：提供事务式本地写入；完整接收并 `commit()` 前不发布不完整的目标文件。
- **`IoOperation`**：通过 `std::unique_ptr` 在提交与 completion 路径间交接。post 失败时自动释放；
  post 成功后内核借用指针，completion 路径重新接管并释放。

关键约束：

- `RemoteControlServer` 的成员声明顺序必须保证 transport 先于 host services 销毁。
- 每个成功投递的 `IoOperation` 都必须由一个 completion 路径重新接管并释放。
- Windows `SOCKET`、`HANDLE` 等仍需在规定的 stop/close 路径中显式关闭；项目并非所有 native
  资源都已经封装成通用 RAII 类型。
- `QSaveFile` 体现事务式文件写入，不等同于数据库事务或两阶段提交。

### Backpressure：让生产速度服从消费能力

背压不是单个类，而是贯穿客户端和服务端的有界资源策略：客户端限制并合并高频屏幕与鼠标工作，
控制流保持单命令在途；服务端限制任务、连接、缓冲区和发送积压，并按消费进度继续目录或下载。
具体调度方式参见[客户端连接模型](ClientArchitecture.md#连接模型)、
[鼠标事件顺序与节流](ClientArchitecture.md#鼠标事件顺序与节流)和
[服务端默认运行参数与背压](ServerArchitecture.md#默认运行参数与背压)。

正常活动会话中的背压允许丢弃可替代的中间鼠标位置或重复截图意图，但不能跨越、重排或静默丢弃
按下、释放、锁定、解锁等离散命令。显式停止会话、切换 endpoint 或停机时，可以有意放弃尚未
完成的旧命令。

当前客户端没有统一限制所有 `OneShotRequest` 的全局并发数；UI pending 状态和服务端连接配额会
间接限制常规操作，但新增可批量触发的单请求操作时仍需单独评估并发上限。

### Completion-driven Pipeline：完成后再推进

客户端屏幕调度、服务端目录枚举和下载都采用“当前步骤完成后才生产下一步”的流水线。屏幕收到
单帧完成通知后才调度下一帧，目录和下载则在当前发送排空后才生成下一批数据。具体周期和批次大小
属于实现配置，分别参见[远程屏幕查看](FeaturesAndDesign.md#远程屏幕查看)和
[服务端默认运行参数与背压](ServerArchitecture.md#默认运行参数与背压)。

这种设计把网络发送完成当作消费反馈，天然限制内存和在途工作。代价是吞吐受单条流水线往返节奏
约束，但对当前远程控制学习项目，比预读大量数据后依赖庞大缓冲区更容易验证和关闭。

### Defensive Protocol Boundary：不信任网络输入

`Packet::tryParse()` 处理半包、粘包、非法长度、校验失败，并重新同步到下一个合法 Packet 帧头。
各 command handler 再验证连接角色和命令特定的 payload；`FileEntry::fromPayload()` 严格验证
schema version、known flags、
名称长度和 UTF-8 round-trip，status payload 只有显式 `StatusCode::Success` 才表示成功。普通 path
和 message 的 `decodeUtf8()` 使用 Qt 容错解码，不能把它描述为全局严格 UTF-8 校验。

代表实现：[Packet.cpp](../src/common/Packet.cpp)和 [Protocol.cpp](../src/common/Protocol.cpp)。

Packet checksum 只能发现普通传输或解析错误，不能提供身份认证、加密或密码学完整性；项目仍只适合
受控学习环境，不能因为存在校验值就直接暴露到公网。

### Idempotent Shutdown：把关闭视为状态协议

幂等关闭要求重复调用只改变一次生命周期，并让资源始终由确定的所有者释放。客户端用 worker
线程中的收尾回调结束线程拥有的资源，服务端用停止标志和连接状态机拒绝新工作、选出唯一关闭者，
同时保留已经投递的 I/O 所需对象直到 completion 返回。客户端和服务端的完整步骤分别参见
[对象所有权与关闭顺序](ClientArchitecture.md#对象所有权与关闭顺序)和
[安全关闭顺序](ServerArchitecture.md#安全关闭顺序)。

必须保持：

1. 关闭入口可以重复调用，但资源只能由唯一获胜路径释放一次。
2. 停机开始后不能创建或接受新工作。
3. 已提交的异步操作及其上下文必须存活到最终完成通知。
4. join 之前必须先通知目标线程退出，线程不能等待自身。

## 典型功能如何组合这些思想

本节只保留“功能使用了哪些原则”和必须跨文档维持的约束；完整流程统一链接到功能或架构文档。

- **下载**
  - 组合原则：Facade、Worker Object、Reactor、Generation Token、事务式文件写入、显式状态机和
    Completion-driven Pipeline。
  - 进一步阅读：[下载功能详解](FeaturesAndDesign.md#下载功能详解)、
    [结果有效性](ClientArchitecture.md#结果有效性)。
- **远程屏幕与鼠标**
  - 组合原则：单帧在途、Completion-driven Pipeline、共享帧缓存、Backpressure、握手状态机和不
    跨越离散事件的移动合并。
  - 进一步阅读：[远程屏幕查看](FeaturesAndDesign.md#远程屏幕查看)、
    [鼠标控制](FeaturesAndDesign.md#鼠标控制)、
    [鼠标事件顺序与节流](ClientArchitecture.md#鼠标事件顺序与节流)。
- **并发关闭**
  - 组合原则：显式状态机、RAII/共享所有权、pending I/O 配对和 Idempotent Shutdown。
  - 进一步阅读：[安全关闭顺序](ServerArchitecture.md#安全关闭顺序)。

下载必须保持 generation 过滤与底层连接取消是两个独立责任：前者丢弃旧结果，后者停止实际传输。
并发关闭则必须保证超时、peer 断线、I/O 错误和 transport 停机竞争时只有一个关闭者，并让已经投递的
operation 安全返回。

每条连接的锁也按职责拆分：`socketMutex` 串行 I/O 提交与关闭，`sendMutex` 保护有序发送，
`fileTransferMutex` 和 `screenFrameMutex` 分别保护对应流状态。持锁期间不能调用阻塞的 host service、
等待网络，也不能调用可能再次获取连接锁的 `closeConnection()`；应先记录结果并退出临界区。

## 不应过度套用的模式名称

- **Command**：协议使用 `Command` enum 和 `switch` 路由，但没有统一的命令对象、`execute()` 和
  可撤销操作。
- **Strategy**：host services 的可替换实现主要体现依赖倒置和适配，并非运行时切换的一组算法策略。
- **Mediator**：`RemoteClient` 更接近 facade/coordinator，没有抽象所有对象之间的通用协作协议。
- **MVC / MVVM**：Qt widget 中仍混合部分展示状态和交互逻辑，没有独立 model/view-model 层。
- **Singleton**：函数内 `static` 或 process-wide 资源生命周期不等于显式 Singleton 模式。
- **Object Pool**：固定线程和 GDI buffer 的复用不构成通用对象池。
- **Actor / Active Object**：Qt queued callback 与 mailbox 有相似之处，但项目没有完整的 actor
  隔离、监督和消息模型。
- **严格 Hexagonal / Clean Architecture**：transport 具有 port/adapter 边界，但内部仍使用 Qt 文件
  和数据类型。
- **严格 Interface Segregation**：`RemoteControlHostServices` 当前仍聚合文件、屏幕、输入和锁屏能力。
- **GoF State**：当前状态由 enum 与 `if`/`switch` 推进，没有为每种状态建立多态对象。

准确命名比模式数量更重要。添加新抽象前，应先确认它是否减少真实耦合、明确所有权或保护不变量，
而不是只让代码看起来更符合某个模式名称。

## 修改代码时的检查清单

### 客户端

- 新 UI 操作是否仍通过 `RemoteClient`，而不是直接操作 worker/socket？
- 新跨线程参数是否按值或只读快照传递，接收对象是否属于预期线程？
- endpoint 或会话变化后，旧结果是否有 generation 校验？
- 新状态是否能用已有状态机表达，是否引入了非法布尔组合？
- 新队列或高频事件是否有容量、合并策略或单项在途限制？
- 关闭时是否先停止生产和定时器，再清理 worker 资源并 join？

### 服务端

- 新命令是否把慢工作从 completion worker 投递到合适的有界任务池？
- 新 I/O 是否严格配对 pending 计数和 completion 释放？
- 新连接状态转换是否保持首包角色不变和唯一关闭者？
- 新发送路径是否保持每连接一个 `WSASend` 在途及有序队列？
- 是否只用对应的 per-connection mutex 保护短临界区，并在调用 host service、等待或关闭连接前解锁？
- 任务、连接、缓冲区和长连接角色是否都有明确上限与拒绝行为？
- Qt GUI 和 Windows 主机能力是否仍通过 host-services adapter，而没有进入 transport 公共接口？

### 协议与文档

- 是否同时更新共享枚举、payload 编解码、客户端处理、服务端路由和协议测试？
- 是否区分“请求已接受”“操作已完成”和“程序已执行结束”等不同成功条件？
- 是否在对应架构文档记录线程/连接变化，并在本文更新受影响的设计约束和取舍？
- 是否运行 Debug 构建、CTest，以及与并发或关闭有关的压力测试？
