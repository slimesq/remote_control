# Remote Control Qt

基于 Qt Widgets 和 TCP 的 Windows 远程控制示例项目，包含客户端、服务端和 smoke test。项目支持使用 VS Code 或 Qt Creator 开发，并兼容 Qt 5.15 与 Qt 6 的 MSVC Kit。

## 功能

- 测试客户端与服务端连接
- 浏览远程磁盘和目录
- 打开、下载和删除远程文件
- 查看远程屏幕并发送鼠标操作
- 锁定和解锁远程界面
- 服务端托盘、开机启动和管理员权限辅助功能

## 项目结构

```text
include/          公共头文件
src/common/       协议与数据包
src/client/       Qt 客户端
src/server/       Qt 服务端
src/tests/        smoke test
scripts/          构建与运行入口
.vscode/          VS Code 构建、调试和 clangd 配置
```

生成的程序：

| Target | 作用 |
| --- | --- |
| `RemoteControlClient` | 远程控制客户端 |
| `RemoteControlServer` | 远程控制服务端 |
| `RemoteControlSmokeTest` | 协议与主要功能回归测试 |
| `RemoteControlProtocolTests` | 无系统副作用的协议边界与 UTF-8 编解码测试 |

## 环境要求

- Windows
- CMake 3.21 或更高版本
- C++17
- Visual Studio Build Tools/MSVC
- Qt 5.15 或 Qt 6，包含 Core、Gui、Widgets、Network
- VS Code 或 Qt Creator

## 快速开始

### VS Code

1. 用 VS Code 打开仓库目录。
2. 安装工作区推荐的 `clangd` 和 Microsoft C++ 扩展。
3. 按 `Ctrl+Shift+B` 构建 Debug。
4. 按 `F5`，选择客户端、服务端或 smoke test。

VS Code 会生成 `build/vscode-debug/compile_commands.json`，供 `clangd` 完成跳转和索引。

### PowerShell

```powershell
# 构建 Debug
.\scripts\Build.ps1

# 启动本地服务端和客户端
.\scripts\Run.ps1 -Target stack -BuildDir .\build\vscode-debug

# 运行无系统副作用的协议测试
ctest --test-dir .\build\vscode-debug --output-on-failure -R RemoteControlProtocolTests
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

Qt Creator 通常会自动为 CMake target 创建运行配置。常用参数：

| Target | 参数 | 作用 |
| --- | --- | --- |
| `RemoteControlClient` | 无 | 启动客户端 |
| `RemoteControlServer` | 无 | 启动带托盘的服务端 |
| `RemoteControlServer` | `--no-tray` | 启动无托盘服务端 |
| `RemoteControlServer` | `--no-tray --lock-test 2` | 执行两秒锁屏测试 |
| `RemoteControlSmokeTest` | `127.0.0.1 9527` | 测试本地服务端 |

如果 Qt Creator 的运行环境找不到 Qt DLL，可以创建 `Custom Executable`：

```text
Program: powershell.exe
Arguments: -ExecutionPolicy Bypass -File "%{sourceDir}/scripts/Run.ps1" -Target <client|server|stack|smoke> -BuildDir "%{buildDir}"
Working directory: %{sourceDir}
```

脚本优先读取 `QTDIR`。Qt Kit 不在常见安装目录时，可以在 Qt Creator 的运行环境中设置：

```text
QTDIR=<Qt Kit 根目录，例如 C:\Qt\6.8.3\msvc2022_64>
```

Qt Creator 生成的 `CMakeLists.txt.user`、`*.creator.user` 和 `build/` 内容属于本地配置，不应提交。

## 默认连接

- 地址：`127.0.0.1`
- 端口：`9527`

客户端默认会探测本地服务端；未检测到监听时，会尝试启动客户端同目录下的 `RemoteControlServer.exe`。

## 文档

- [构建与运行脚本](scripts/README.md)
- [项目代码学习指南](docs/StudyGuide.md)

## 代码命名规范

- 类、结构体、枚举及枚举值：大驼峰
- 函数和局部变量：小驼峰
- 形参：`_` 加小驼峰，例如 `_serverHost`
- 类的非静态成员：`m_` 加小驼峰，例如 `m_serverHost`
- 类内部访问自身成员时显式使用 `this->`

仓库根目录的 `.clang-tidy` 固化了上述命名规则。VS Code 使用 `build/vscode-debug/compile_commands.json` 运行静态分析。

## 安全提示

该项目包含远程文件执行、删除、屏幕查看和输入控制能力。当前协议未提供身份认证或 TLS 加密，不应直接暴露到公网或不可信网络。请仅在学习、测试或明确授权的受控环境中使用。
