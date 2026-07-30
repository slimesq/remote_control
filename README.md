# Remote Control Qt

基于 Qt Widgets 和 TCP 的 Windows 远程控制学习项目，包含客户端、服务端、协议测试和
端到端 smoke test。项目支持使用 VS Code 或 Qt Creator 开发，并兼容 Qt 5.15 与 Qt 6
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
include/          公共头文件
src/common/       协议与数据包
src/client/       Qt 客户端
src/server/       Qt 服务端
src/tests/        协议测试与端到端 smoke test
scripts/          构建与运行入口
.vscode/          VS Code 构建、调试和 clangd 配置
.claude/skills/   项目编码与审查规则
```

生成的程序：

| Target | 作用 |
| --- | --- |
| `RemoteControlClient` | 远程控制客户端 |
| `RemoteControlServer` | 远程控制服务端 |
| `RemoteControlSmokeTest` | 连接到运行中服务端的端到端回归测试 |
| `RemoteControlProtocolTests` | 无系统副作用的协议边界与 UTF-8 编解码测试 |

## 环境要求

- Windows
- CMake 3.25 或更高版本
- C++17
- Visual Studio Build Tools/MSVC
- Qt 5.15 或 Qt 6，包含 Core、Gui、Widgets、Network
- VS Code 或 Qt Creator

## 快速开始

### VS Code

1. 用 VS Code 打开仓库目录。
2. 安装工作区推荐的 CMake、C++、clangd 和 Qt 扩展。
3. 按 `Ctrl+Shift+B` 执行 Debug 增量构建。
4. 按 `F5`，选择客户端、服务端或 smoke test；运行 smoke test 前需要先启动服务端。

构建脚本和 CMake Tools 都会把当前配置的 `compile_commands.json` 同步到项目根目录，供 `clangd` 完成跳转和索引。

### PowerShell

```powershell
# 构建 Debug
.\scripts\Build.ps1

# 终端 1：启动服务端
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug

# 终端 2：启动客户端
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug

# 运行无系统副作用的协议测试
ctest --test-dir .\build\msvc-debug --output-on-failure -R RemoteControlProtocolTests
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
   - `RemoteControlSmokeTest`
   - `RemoteControlProtocolTests`

Qt Creator 通常会自动为 CMake target 创建运行配置。常用参数：

| Target | 参数 | 作用 |
| --- | --- | --- |
| `RemoteControlClient` | 无 | 启动客户端 |
| `RemoteControlServer` | 无 | 启动带托盘的服务端 |
| `RemoteControlServer` | `--no-tray` | 启动无托盘服务端 |
| `RemoteControlServer` | `--no-tray --lock-test 2` | 执行两秒模拟锁定测试 |
| `RemoteControlSmokeTest` | `127.0.0.1 9527` | 测试本地服务端 |

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
- 服务端监听地址：所有本机网络接口（`QHostAddress::Any`）

客户端只负责连接指定服务端，不会启动或管理服务端进程。本地开发时，请在两个终端中分别启动服务端和客户端：

```powershell
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug
```

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

## 运行模型

- `TestConnection`、`ListDrives` 和 `RunFile` 使用一次性短连接。客户端通过事件循环异步处理，不会同步等待网络；服务端由 `RemoteSession` 转交 `CommandService`。
- `ListDirectory`、`DownloadFile` 和 `DeleteFile` 使用一次性文件任务连接。服务端将
  socket 和请求转移到 2～4 个可复用的 `FileRequestWorker`；待处理队列最多保留 64 个
  请求。
- 客户端下载、远程画面和远程控制分别使用独立的常驻 `QThread` 和 worker object。
- `WatchScreen` 使用独立监控长连接，一次只允许一帧处于请求中；客户端在每帧完成后按最高约 30 FPS 自适应调度下一帧，服务端最多接受 4 条监控连接。
- `ControlChannel` 使用独立控制长连接，命令按顺序等待响应，连续鼠标移动会合并；服务端最多接受 4 条控制连接。
- 监控和控制使用不同连接，避免较大的截图数据阻塞鼠标与模拟锁定命令。

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
| `RemoteControlSmokeTest` | 连接、磁盘与目录、并发/空/缺失文件下载、递归删除、监控长连接、控制长连接和文件执行 |

`RemoteControlSmokeTest` 会连接并操作正在运行的服务端，包括截图、鼠标控制路径和文件执行验证，只应在受控测试环境运行。

## 文档

- [构建与运行脚本](scripts/README.md)
- [项目代码学习指南](docs/StudyGuide.md)

## 安全提示

该项目包含远程文件执行、删除、屏幕查看和输入控制能力。当前协议未提供身份认证或
TLS 加密，不应直接暴露到公网或不可信网络。请仅在学习、测试或明确授权的受控环境中
使用。

项目中的“锁定”是应用级模拟锁定：服务端显示全屏覆盖窗口、隐藏任务栏、限制鼠标并
抢占键盘输入，`Ctrl+C` 可用于紧急解锁。它不等同于 Windows 会话锁定，也不能作为系统
安全边界。
