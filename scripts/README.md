# 项目脚本说明

返回[项目主页](../README.md)。

日常只需要使用 `Build.ps1` 和 `Run.ps1`。`internal` 目录存放公共实现，不应直接执行。

## 环境要求

- Windows
- Visual Studio Build Tools，包含 MSVC C++ 工具链
- CMake
- Qt 5.15 或 Qt 6 的 MSVC Kit
- PowerShell 5.1 或更高版本

脚本优先使用环境变量 `QTDIR` 指定的 Qt Kit。未设置时，会搜索常见 Qt 安装目录。

例如：

```powershell
$env:QTDIR = "C:\Qt\6.8.3\msvc2022_64"
```

## Build.ps1

负责 CMake configure、编译和清理。默认执行 Debug 构建：

```powershell
.\scripts\Build.ps1
```

支持的参数：

| 参数 | 可选值 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `-Action` | `configure`、`build`、`clean` | `build` | 选择操作 |
| `-Config` | `Debug`、`Release` | `Debug` | 选择构建类型 |

常用示例：

```powershell
# 仅运行 CMake configure，并生成 compile_commands.json
.\scripts\Build.ps1 -Action configure

# 构建 Debug
.\scripts\Build.ps1 -Action build -Config Debug

# 构建 Release
.\scripts\Build.ps1 -Action build -Config Release

# 删除 Debug 构建目录
.\scripts\Build.ps1 -Action clean -Config Debug
```

构建目录分别为：

```text
build/vscode-debug
build/vscode-release
```

构建完成后，脚本会调用 `windeployqt` 部署 Qt DLL。Debug 构建目录中的 `compile_commands.json` 同时供 VS Code 的 `clangd` 使用。

## Run.ps1

负责运行已经构建好的程序，必须通过 `-Target` 选择目标：

| Target | 作用 |
| --- | --- |
| `client` | 运行远程控制客户端 |
| `server` | 运行远程控制服务端 |
| `stack` | 必要时启动服务端，等待端口就绪后启动客户端 |
| `smoke` | 运行协议 smoke test |

常用示例：

```powershell
# 运行客户端
.\scripts\Run.ps1 -Target client -BuildDir .\build\vscode-debug

# 运行服务端
.\scripts\Run.ps1 -Target server -BuildDir .\build\vscode-debug

# 后台运行服务端
.\scripts\Run.ps1 -Target server -BuildDir .\build\vscode-debug -NoTray

# 启动本地服务端和客户端
.\scripts\Run.ps1 -Target stack -BuildDir .\build\vscode-debug -NoTray

# 运行 smoke test
.\scripts\Run.ps1 -Target smoke -BuildDir .\build\vscode-debug
```

其他参数：

| 参数 | 说明 |
| --- | --- |
| `-BuildDir` | 指定构建目录；存在多个构建目录时建议提供 |
| `-ServerHost` | 服务端地址，默认为 `127.0.0.1` |
| `-Port` | 服务端端口，默认为 `9527` |
| `-NoTray` | 让服务端以无托盘模式运行 |
| `-LockTestSeconds` | 服务端锁屏测试持续时间 |
| `-StartupTimeoutSeconds` | `stack` 等待服务端启动的超时时间，默认为 8 秒 |

## VS Code 和 Qt Creator

- VS Code 的构建和调试任务已经自动调用 `Build.ps1`，通常使用 `Ctrl+Shift+B` 或 `F5` 即可。
- Qt Creator 可直接使用 CMake target；需要自定义运行环境时，可调用 `Run.ps1`。详细配置参见项目根目录 [README](../README.md) 的 Qt Creator 小节。

## 查看命令帮助

两个入口脚本均提供 PowerShell 原生帮助：

```powershell
Get-Help .\scripts\Build.ps1 -Full
Get-Help .\scripts\Run.ps1 -Full
```
