# Remote Control Qt

基于 Qt Widgets、TCP 和 Windows IOCP 的远程控制综合学习项目。它建立在 IOCP 基础知识之上，
重点展示 Qt 客户端、Windows 服务端、异步传输和并发停机如何组合成完整应用。客户端使用 Qt
异步网络接口，服务端使用 IOCP 和固定大小任务池；项目支持 VS Code 与 Qt Creator，并兼容
Qt 5.15 和 Qt 6 的 MSVC Kit。

## 功能

- 测试客户端与服务端连接
- 浏览远程磁盘和目录
- 打开、下载和删除远程文件
- 查看远程屏幕并发送鼠标操作
- 应用级模拟锁定和解锁服务端主机界面
- 服务端托盘、当前用户登录启动和管理员权限辅助功能

## 项目结构

- [`include/`](include/)：client、common 与 server 的项目头文件。
- [`server_transport/`](server_transport/)：独立的 Windows IOCP 传输 target，内部按
  `include`、`internal` 和 `src` 组织。
- [`src/common/`](src/common/)：协议与数据包实现。
- [`src/client/`](src/client/)：Qt 客户端实现。
- [`src/server/`](src/server/)：Qt 服务端应用层与 Windows 主机能力适配。
- [`tests/`](tests/)：协议、状态机、韧性测试与端到端 smoke test。
- [`scripts/`](scripts/)：构建与运行入口。
- [`.vscode/`](.vscode/)：VS Code 构建、调试和 clangd 配置。
- [`.claude/skills/`](.claude/skills/)：项目编码与审查规则。

生成的程序：

- `RemoteControlClient`：远程控制客户端。
- `RemoteControlServer`：远程控制服务端。

测试 target 及其覆盖范围统一列在[测试](#测试)章节。

## 文档入口

- 第一次构建或查找参数：[构建与运行脚本](scripts/README.md)。
- 了解项目功能及其技术实现：[项目功能与技术实现](docs/FeaturesAndDesign.md)。
- 理解设计原则、设计模式与工程取舍：[设计思想与设计模式](docs/DesignPrinciples.md)。
- 系统学习项目代码：[项目代码学习指南](docs/StudyGuide.md)。
- 理解客户端对象、线程和连接：[客户端系统架构](docs/ClientArchitecture.md)。
- 学习 IOCP 基础概念与渐进练习：
  [IOCP 基础学习路线](https://github.com/slimesq/IOCP/blob/main/docs/IOCP%E5%AD%A6%E4%B9%A0%E8%B7%AF%E7%BA%BF.md)。
- 理解 IOCP 在本项目中的状态机和安全停机：
  [IOCP 服务端系统架构](docs/ServerArchitecture.md)。
- 查询 Packet、命令和 payload：[远程控制协议参考](docs/ProtocolReference.md)。

[项目代码学习指南](docs/StudyGuide.md)是本仓库唯一的完整阅读顺序，按“协议 → 客户端 →
服务端边界 → IOCP → 测试”组织。通用 IOCP 原理和基础练习由外部 IOCP 仓库维护，本仓库文档
只解释它们在 `remote_control` 中的具体应用。已经读完客户端时，可以直接从服务端阶段继续。

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

> `RemoteControlSmokeTests` 会操作服务端主机，只能连接受控测试环境；副作用和安全边界见
> [测试](#测试)。

构建脚本和 CMake Tools 都会把当前配置的 `compile_commands.json` 同步到项目根目录，供
`clangd` 完成跳转和索引。

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
4. 选择需要运行的 CMake target；测试 target 及其覆盖范围见[测试](#测试)章节。

Qt Creator 通常会自动为 CMake target 创建运行配置。常用参数：

- 启动客户端：选择 `RemoteControlClient`，无需参数。
- 启动带托盘的服务端：选择 `RemoteControlServer`，无需参数。
- 启动无托盘服务端：选择 `RemoteControlServer`，参数为 `--no-tray`。
- 执行两秒模拟锁定测试：选择 `RemoteControlServer`，参数为
  `--no-tray --lock-test 2`。
- 测试本地服务端：选择 `RemoteControlSmokeTests`，参数为 `127.0.0.1 9527`。

如果 Qt Creator 的运行环境找不到 Qt DLL，可以创建 `Custom Executable`：

- `Program`：`powershell.exe`
- `Working directory`：`%{sourceDir}`
- `Arguments`：在同一个参数栏中依次填写以下内容，并以空格分隔：
  - `-ExecutionPolicy Bypass`
  - `-File "%{sourceDir}/scripts/Run.ps1"`
  - `-Target <client|server|smoke>`
  - `-BuildDir "%{buildDir}"`

脚本优先读取 `QTDIR`。Qt Kit 不在常见安装目录时，可以在 Qt Creator 的运行环境中设置：

```text
QTDIR=<Qt Kit 根目录，例如 C:\Qt\6.8.3\msvc2022_64>
```

Qt Creator 生成的 `CMakeLists.txt.user`、`*.creator.user` 和 `build/` 内容属于本地配置，不应提交。

## 默认连接

- 客户端默认连接地址：`127.0.0.1`
- 客户端和服务端默认端口：`9527`
- 服务端监听地址：所有本机 IPv4 网络接口（`INADDR_ANY`）

客户端只负责连接指定服务端，不会启动或管理服务端进程。本地开发时，请在两个终端中分别启动
服务端和客户端。具体命令见前面的“PowerShell”快速开始。

## 命令行配置

客户端参数：

- `--server-host <host>`：设置远程服务端地址，默认值为 `127.0.0.1`。
- `--server-port <port>`：设置远程服务端端口，默认值为 `9527`。

服务端参数：

- `-p, --port <port>`：设置监听端口，默认值为 `9527`。
- `--no-tray`：运行服务端，但不创建系统托盘图标。
- `--lock-test <seconds>`：服务端启动后模拟锁定指定秒数，再自动解锁；服务端继续运行。
- `--elevate`：使用 Windows UAC 启动新的管理员权限服务端，当前进程退出；其余参数会保留。
- `--install-startup`：写入当前用户的 Windows 登录启动项后退出，不启动监听服务。
- `--remove-startup`：删除当前用户的 Windows 登录启动项后退出，不启动监听服务。

`Run.ps1` 对外提供常用的地址、端口、无托盘和模拟锁定测试参数；当前用户登录启动项和提权操作
需要直接运行 `RemoteControlServer.exe`。

## 架构摘要

- 客户端 GUI 位于主线程；屏幕流、控制流和下载分别使用一个常驻 `QThread`，其余单请求操作
  使用 GUI 线程中的异步 socket。
- 屏幕和控制使用独立长连接，避免图像流阻塞输入命令；所有连接进入同一个服务端监听端口。
- `RemoteControlServer` 是唯一服务端程序；独立的 IOCP transport target 负责网络完成通知，
  有界任务池负责阻塞业务，并通过 `RemoteControlHostServices` 接入 Windows/Qt 主机能力。

完整的对象、线程、连接和关闭关系分别见
[客户端系统架构](docs/ClientArchitecture.md)与
[IOCP 服务端系统架构](docs/ServerArchitecture.md)。

## 测试

```powershell
# 无需服务端，不修改系统状态
ctest --test-dir .\build\msvc-debug --output-on-failure

# 需要先启动服务端
.\scripts\Run.ps1 -Target smoke -BuildDir .\build\msvc-debug
```

- `RemoteControlProtocolTests`：覆盖 Packet 非法长度恢复、拆分包头、FileEntry/UTF-8、
  无效负载和状态负载。
- `RemoteControlClientWorkerLifecycleTests`：覆盖本地 TCP 下载取消、替换下载隔离、
  临时文件回滚和三类 worker 析构回收。
- `RemoteControlTransportLifecycleTests`：覆盖连续创建 transport、连接到达期间停止、
  pending accept/receive 取消与线程回收。
- `RemoteControlConnectionStateTests`：覆盖单向状态转换、并发关闭唯一性、总连接容量和
  长连接配额回收。
- `RemoteControlTransportResilienceTests`：覆盖损坏前缀、错误校验、超长声明、半包断开、
  连接角色错配和 128 次并发请求。
- `RemoteControlSmokeTests`：覆盖连接、磁盘与目录、直接网络路径拒绝、并发与慢客户端下载、
  junction 自身安全删除、监控/控制长连接和文件执行。

`RemoteControlSmokeTests` 会连接并操作正在运行的服务端，包括截图、鼠标输入、临时文件创建、
下载、删除和文件执行验证，只应在受控测试环境运行。

## 安全提示

该项目包含远程文件执行、删除、屏幕查看和输入控制能力。当前协议未提供身份认证或
TLS 加密，不应直接暴露到公网或不可信网络。请仅在学习、测试或明确授权的受控环境中
使用。

文件操作会拒绝直接的 UNC 路径和映射网络盘，但当前只根据盘符根目录判断 drive type，
尚未验证 junction 或 symbolic link 解析后的最终位置。因此它不是文件系统安全沙箱。

项目中的“锁定”是应用级模拟锁定：服务端显示全屏覆盖窗口、隐藏任务栏、限制鼠标并
抢占键盘输入，`Ctrl+C` 可用于紧急解锁。它不等同于 Windows 会话锁定，也不能作为系统
安全边界。
