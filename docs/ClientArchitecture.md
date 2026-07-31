# 客户端系统架构

本文用于说明客户端组件职责、线程边界、对象通信和网络连接模型。推荐阅读顺序参见
[项目代码学习指南](StudyGuide.md)。

## 总体架构

```mermaid
flowchart LR
    subgraph GuiThread["GUI 线程（QApplication 事件循环）"]
        ClientMain["ClientMain<br/>创建 QApplication 和 MainWindow"]
        MainWindow["MainWindow<br/>文件浏览、目录缓存、操作状态"]
        WatchWindow["WatchWindow<br/>画面调度、鼠标事件节流"]
        ScreenWidget["RemoteScreenWidget<br/>绘制画面、采集鼠标输入"]
        RemoteClient["RemoteClient<br/>网络 facade、generation、结果转发"]
        PendingRequest["PendingRequest<br/>一次性异步请求"]

        ClientMain -->|"创建并显示"| MainWindow
        MainWindow -->|"拥有"| RemoteClient
        MainWindow -->|"按需创建"| WatchWindow
        WatchWindow -->|"拥有"| ScreenWidget
        MainWindow -->|"文件和连接请求"| RemoteClient
        WatchWindow -->|"帧和控制请求"| RemoteClient
        RemoteClient -->|"每个短请求创建一个"| PendingRequest
        RemoteClient -.->|"业务结果 signal"| MainWindow
        RemoteClient -.->|"画面和请求完成 signal"| WatchWindow
        WatchWindow -.->|"QImage"| ScreenWidget
    end

    subgraph WatchThread["监控 QThread"]
        WatchWorker["WatchConnectionWorker<br/>监控长连接、单帧在途"]
    end

    subgraph ControlThread["控制 QThread"]
        ControlWorker["ControlConnectionWorker<br/>控制长连接、顺序队列、移动合并"]
    end

    subgraph DownloadThread["下载 QThread"]
        DownloadWorker["DownloadWorker<br/>流式接收、QSaveFile 写入"]
    end

    subgraph RemoteServer["远程服务端"]
        ShortEndpoint["短连接/文件任务入口"]
        WatchEndpoint["WatchScreen 通道"]
        ControlEndpoint["ControlChannel 通道"]
    end

    RemoteClient -->|"invokeMethod<br/>QueuedConnection"| WatchWorker
    RemoteClient -->|"invokeMethod<br/>QueuedConnection"| ControlWorker
    RemoteClient -->|"invokeMethod<br/>QueuedConnection"| DownloadWorker

    WatchWorker -.->|"frameReady / failed"| RemoteClient
    ControlWorker -.->|"commandCompleted / commandFailed"| RemoteClient
    DownloadWorker -.->|"progress / finished"| RemoteClient

    PendingRequest <-->|"一次性 TCP 连接"| ShortEndpoint
    DownloadWorker <-->|"一次性下载连接"| ShortEndpoint
    WatchWorker <-->|"持久 TCP 连接"| WatchEndpoint
    ControlWorker <-->|"持久 TCP 连接"| ControlEndpoint
```

图中的实线表示对象创建、函数调用或跨线程任务投递，虚线表示 signal 返回。

## 线程划分

| 线程 | 主要对象 | 职责 |
| --- | --- | --- |
| GUI 线程 | `MainWindow`、`WatchWindow`、`RemoteClient`、`PendingRequest` | 界面、短连接异步请求和业务结果转发 |
| 监控线程 | `WatchConnectionWorker` | 维护监控长连接，一次只请求一帧 |
| 控制线程 | `ControlConnectionWorker` | 维护控制长连接，按顺序发送命令并合并鼠标移动 |
| 下载线程 | `DownloadWorker` | 流式接收文件并写入 `QSaveFile` |

客户端只有一个 GUI 线程，`MainWindow` 和 `WatchWindow` 共用
`QApplication::exec()` 启动的事件循环。创建多个窗口不会自动创建多个 GUI 线程。

`PendingRequest` 也位于 GUI 线程。它使用异步 `QTcpSocket`，由 Qt 事件循环驱动，因此
等待网络数据时不会同步阻塞界面。监控、控制和下载属于持续时间较长或负载较重的任务，
所以分别交给三个常驻工作线程。

## 连接模型

| 模型 | 客户端实现 | 用途 | 生命周期 |
| --- | --- | --- | --- |
| 一次性短连接 | `PendingRequest` | 连接测试、磁盘、目录、打开和删除 | 每个逻辑请求创建一个对象 |
| 一次性下载连接 | `DownloadWorker` | 流式下载文件 | 一次只执行一个下载 |
| 监控长连接 | `WatchConnectionWorker` | 持续请求远程画面 | 监控窗口关闭时断开 |
| 控制长连接 | `ControlConnectionWorker` | 鼠标、模拟锁定和解锁 | 控制窗口关闭时断开 |

`RemoteClient` 是 GUI 与网络实现之间的 facade。GUI 只调用业务接口，不直接操作 worker
或 socket。监控和控制结果返回后，`RemoteClient` 会根据 generation 丢弃旧会话结果；
有效结果以及下载进度再通过业务 signal 转发给界面。
