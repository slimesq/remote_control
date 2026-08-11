# Remote Control Qt

基于 Qt Widgets、TCP 和 Windows IOCP 的远程控制学习项目。客户端使用 Qt 异步网络接口，
服务端使用 IOCP 和固定大小任务池；项目支持 VS Code 与 Qt Creator，并兼容 Qt 5.15 和 Qt 6
的 MSVC Kit。

## 功能

- 测试客户端与服务端连接
- 浏览远程磁盘和目录
- 打开、下载和删除远程文件
- 查看远程屏幕并发送鼠标操作
- 应用级模拟锁定和解锁远程界面
- 服务端托盘、开机启动和管理员权限辅助功能

## 项目结构

```text
include/          client、common 与 server 的项目头文件
server_transport/ 独立的 Windows IOCP 传输 target（include/internal/src）
src/common/       协议与数据包
src/client/       Qt 客户端
src/server/       Qt 服务端应用层与 Windows 主机能力适配
tests/            协议、状态机、韧性测试与端到端 smoke test
scripts/          构建与运行入口
.vscode/          VS Code 构建、调试和 clangd 配置
.claude/skills/   项目编码与审查规则
```

生成的程序：

| Target | 作用 |
| --- | --- |
| `RemoteControlClient` | 远程控制客户端 |
| `RemoteControlServer` | 远程控制服务端 |
| `RemoteControlSmokeTests` | 连接到运行中服务端的端到端回归测试 |
| `RemoteControlProtocolTests` | 无系统副作用的协议边界与 UTF-8 编解码测试 |
| `RemoteControlClientWorkerLifecycleTests` | 下载取消/替换隔离与客户端 worker 安全关闭测试 |
| `RemoteControlTransportLifecycleTests` | IOCP 启动、并发连接与安全关闭压力测试 |
| `RemoteControlConnectionStateTests` | 连接状态转换、并发关闭和容量配额测试 |
| `RemoteControlTransportResilienceTests` | 真实 TCP 故障注入与并发请求压力测试 |

## 文档入口

| 目标 | 从这里开始 |
| --- | --- |
| 第一次构建或查找参数 | [构建与运行脚本](scripts/README.md) |
| 了解项目功能及其技术实现 | [项目功能与技术实现](docs/FeaturesAndDesign.md) |
| 系统学习项目代码 | [项目代码学习指南](docs/StudyGuide.md) |
| 理解客户端对象、线程和连接 | [客户端系统架构](docs/ClientArchitecture.md) |
| 理解 IOCP、状态机和安全停机 | [IOCP 服务端系统架构](docs/ServerArchitecture.md) |
| 查询 Packet、命令和 payload | [远程控制协议参考](docs/ProtocolReference.md) |

学习指南按“协议 → 客户端 → 服务端边界 → IOCP → 测试”组织，每个阶段都包含入口文件、
需要回答的问题和完成标准。已经读完客户端时，可以直接从服务端阶段继续。

## 环境要求

- Windows
- CMake 3.25 或更高版本
- C++17
- Visual Studio Build Tools/MSVC
- Ninja（可独立安装，也可使用 Visual Studio 附带版本）
- Qt 5.15 或 Qt 6，包含 Core、Gui、Widgets、Network
- PowerShell 5.1 或更高版本
- VS Code 或 Qt Creator

## 快速开始

### VS Code

1. 用 VS Code 打开仓库目录。
2. 安装工作区推荐扩展：CMake Tools 负责 CMake preset/target，C/C++ 提供 MSVC 调试器，
   clangd 负责代码跳转、补全和诊断，Qt 扩展负责 Qt Kit 与 `.ui` 文件支持。
3. 按 `Ctrl+Shift+B` 执行 Debug 增量构建。
4. 按 `F5`，选择客户端、服务端或 smoke test；运行 smoke test 前需要先启动服务端。

> `RemoteControlSmokeTests` 会实际请求截图、发送鼠标控制、验证文件执行，并对临时文件执行
> 下载与删除。请只连接受控测试环境；只想验证无系统副作用的测试时，应运行 CTest。

构建脚本和 CMake Tools 都会把当前配置的 `compile_commands.json` 同步到项目根目录，供 `clangd` 完成跳转和索引。

### PowerShell

```powershell
# 构建 Debug
.\scripts\Build.ps1

# 终端 1：启动服务端
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug

# 终端 2：启动客户端
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug

# 运行全部无系统副作用的 CTest 测试
ctest --test-dir .\build\msvc-debug --output-on-failure
```

更多参数参见 [脚本说明](scripts/README.md)。

### Qt Creator

1. 使用 Qt Creator 打开根目录的 `CMakeLists.txt`。
2. 选择 Qt 5.15 或 Qt 6 的 MSVC 64-bit Kit。
3. 使用独立构建目录，避免与 VS Code 共用 CMake cache：
   - Debug：`build/qtcreator-debug`
   - Release：`build/qtcreator-release`
4. 选择需要的 CMake target：
   - `RemoteControlClient`
   - `RemoteControlServer`
   - `RemoteControlSmokeTests`
   - `RemoteControlProtocolTests`
   - `RemoteControlClientWorkerLifecycleTests`
   - `RemoteControlTransportLifecycleTests`
   - `RemoteControlConnectionStateTests`
   - `RemoteControlTransportResilienceTests`

Qt Creator 通常会自动为 CMake target 创建运行配置。常用参数：

| Target | 参数 | 作用 |
| --- | --- | --- |
| `RemoteControlClient` | 无 | 启动客户端 |
| `RemoteControlServer` | 无 | 启动带托盘的服务端 |
| `RemoteControlServer` | `--no-tray` | 启动无托盘服务端 |
| `RemoteControlServer` | `--no-tray --lock-test 2` | 执行两秒模拟锁定测试 |
| `RemoteControlSmokeTests` | `127.0.0.1 9527` | 测试本地服务端 |

如果 Qt Creator 的运行环境找不到 Qt DLL，可以创建 `Custom Executable`：

```text
Program: powershell.exe
Arguments: -ExecutionPolicy Bypass -File "%{sourceDir}/scripts/Run.ps1" -Target <client|server|smoke> -BuildDir "%{buildDir}"
Working directory: %{sourceDir}
```

脚本优先读取 `QTDIR`。Qt Kit 不在常见安装目录时，可以在 Qt Creator 的运行环境中设置：

```text
QTDIR=<Qt Kit 根目录，例如 C:\Qt\6.8.3\msvc2022_64>
```

Qt Creator 生成的 `CMakeLists.txt.user`、`*.creator.user` 和 `build/` 内容属于本地配置，不应提交。

## 默认连接

- 客户端默认连接地址：`127.0.0.1`
- 客户端和服务端默认端口：`9527`
- 服务端监听地址：所有本机 IPv4 网络接口（`INADDR_ANY`）

客户端只负责连接指定服务端，不会启动或管理服务端进程。本地开发时，请在两个终端中分别启动服务端和客户端：
具体命令见前面的“PowerShell”快速开始。

## 命令行配置

客户端参数：

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| `--server-host <host>` | `127.0.0.1` | 设置远程服务端地址 |
| `--server-port <port>` | `9527` | 设置远程服务端端口 |

服务端参数：

| 参数 | 运行行为 |
| --- | --- |
| `-p, --port <port>` | 设置监听端口，默认 `9527` |
| `--no-tray` | 运行服务端但不创建系统托盘图标 |
| `--lock-test <seconds>` | 服务端启动后模拟锁定指定秒数，再自动解锁；服务端继续运行 |
| `--elevate` | 使用 Windows UAC 启动新的管理员权限服务端，当前进程退出；其余参数会保留 |
| `--install-startup` | 写入当前用户的 Windows 启动项后退出，不启动监听服务 |
| `--remove-startup` | 删除当前用户的 Windows 启动项后退出，不启动监听服务 |

`Run.ps1` 对外提供常用的地址、端口、无托盘和模拟锁定测试参数；启动项和提权操作需要直接运行 `RemoteControlServer.exe`。

## 架构摘要

- `RemoteControlClient` 的界面位于主线程；下载、屏幕流和控制流分别使用常驻 `QThread`。
- 一次性命令各自创建异步 TCP 连接；屏幕和控制使用两条独立长连接，避免大图像阻塞输入命令。
- `RemoteControlServer` 是唯一服务端程序；`RemoteControl::ServerTransport` 是供程序和测试复用的
  独立 IOCP 传输 target，并不是第二个服务端。
- `RemoteControlHostServices` 是传输层与 Windows/Qt 业务层的边界；服务端通过
  `WindowsRemoteControlHostServices` 注入磁盘、屏幕、鼠标、文件打开和锁屏能力。
- `RemoteControlTransport` 使用少量 IOCP completion worker 处理所有 socket 完成通知，阻塞工作
  进入命令、文件或截图任务池。
- 连接由 `ConnectionRegistry` 持有，并通过 `ConnectionStateMachine` 从等待首包单向进入一个
  固定业务阶段，最终经过 `Closing` 到达 `Closed`。
- 目录和下载按发送完成节奏分批推进；每条连接只有一个发送在途，并使用有界队列提供背压。

完整的对象、线程、连接和关闭关系分别见
[客户端系统架构](docs/ClientArchitecture.md)与
[IOCP 服务端系统架构](docs/ServerArchitecture.md)。

客户端目录树使用 `DirectoryLoadState` 表示 `Unloaded`、`Loading`、`Loaded` 和
`Refreshing`。目录加载成功后缓存 `QList<FileEntry>`；普通点击可以复用缓存，强制刷新
失败时仍保留原缓存。

## 测试

```powershell
# 无需服务端，不修改系统状态
ctest --test-dir .\build\msvc-debug --output-on-failure

# 需要先启动服务端
.\scripts\Run.ps1 -Target smoke -BuildDir .\build\msvc-debug
```

| 测试 | 覆盖范围 |
| --- | --- |
| `RemoteControlProtocolTests` | Packet 非法长度恢复、拆分包头、FileEntry/UTF-8、无效负载和状态负载 |
| `RemoteControlClientWorkerLifecycleTests` | 本地 TCP 下载取消、替换下载隔离、临时文件回滚和三类 worker 析构回收 |
| `RemoteControlTransportLifecycleTests` | 连续创建 transport、连接到达期间停止、pending accept/receive 取消与线程回收 |
| `RemoteControlConnectionStateTests` | 单向状态转换、并发关闭唯一性、总连接容量和长连接配额回收 |
| `RemoteControlTransportResilienceTests` | 损坏前缀、错误校验、超长声明、半包断开、连接角色错配和 128 次并发请求 |
| `RemoteControlSmokeTests` | 连接、磁盘与目录、直接网络路径拒绝、并发/慢客户端下载、junction 自身安全删除、监控/控制长连接和文件执行 |

`RemoteControlSmokeTests` 会连接并操作正在运行的服务端，包括截图、鼠标控制路径和文件执行验证，只应在受控测试环境运行。

## 安全提示

该项目包含远程文件执行、删除、屏幕查看和输入控制能力。当前协议未提供身份认证或
TLS 加密，不应直接暴露到公网或不可信网络。请仅在学习、测试或明确授权的受控环境中
使用。

文件操作会拒绝直接的 UNC 路径和映射网络盘，但当前只根据盘符根目录判断 drive type，
尚未验证 junction 或 symbolic link 解析后的最终位置。因此它不是文件系统安全沙箱。

项目中的“锁定”是应用级模拟锁定：服务端显示全屏覆盖窗口、隐藏任务栏、限制鼠标并
抢占键盘输入，`Ctrl+C` 可用于紧急解锁。它不等同于 Windows 会话锁定，也不能作为系统
安全边界。
