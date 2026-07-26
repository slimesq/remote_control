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

## CMake Presets

项目根目录的 `CMakePresets.json` 保存可共享的配置、构建、测试和工作流预设：

| Preset | 构建目录 | 作用 |
| --- | --- | --- |
| `msvc-debug` | `build/msvc-debug` | MSVC x64 + Ninja Debug |
| `msvc-release` | `build/msvc-release` | MSVC x64 + Ninja Release |

`CMakePresets.json` 只保存可共享的生成器、构建类型和目标规则，不包含本机绝对路径。`CMakeUserPresets.json` 保存当前计算机的 Qt Kit、Ninja、MSVC 和 Windows SDK 路径，并且已被 `.gitignore` 忽略。

首次克隆项目或本机工具链发生变化时，运行初始化脚本：

```powershell
.\scripts\Setup-CMakeUserPresets.ps1
```

脚本通过 `vswhere` 和 `VsDevCmd.bat` 自动查找 MSVC 与 Windows SDK，并自动查找 Qt 和 Ninja，然后生成 `CMakeUserPresets.json`。VS Code 中也可以运行任务 `CMake: Setup Local Presets`。

VS Code 的 CMake Tools 应选择 `local-msvc-debug` 或 `local-msvc-release` Configure Preset，再选择对应的 `local-msvc-*-client` 或 `local-msvc-*-server` Build Preset。换电脑时只需重新运行初始化脚本，共享文件不需要改动。

构建时可以选择完整项目、客户端或服务端：

| Build Preset | 目标 |
| --- | --- |
| `local-msvc-debug` | Debug 全部目标 |
| `local-msvc-debug-client` | Debug 客户端 `RemoteControlClient` |
| `local-msvc-debug-server` | Debug 服务端 `RemoteControlServer` |
| `local-msvc-debug-tests` | Debug 测试程序 |
| `local-msvc-release-client` | Release 客户端 `RemoteControlClient` |
| `local-msvc-release-server` | Release 服务端 `RemoteControlServer` |
| `local-msvc-release-tests` | Release 测试程序 |

常用命令：

```powershell
# 配置
cmake --preset local-msvc-debug

# 构建
cmake --build --preset local-msvc-debug

# 只构建 Debug 客户端
cmake --build --preset local-msvc-debug-client

# 只构建 Debug 服务端
cmake --build --preset local-msvc-debug-server

# 测试
ctest --preset local-msvc-debug

# 连续执行配置、构建和测试
cmake --workflow --preset local-msvc-debug
```

本地预设已经包含初始化脚本生成的 MSVC 环境。`Build.ps1` 会在本地 preset 缺失或失效时自动生成；工具链发生变化后，可以使用 `-RefreshPresets` 强制刷新。

## Build.ps1

负责检查本机 preset、调用 CMake、同步 `compile_commands.json`、按需部署运行库和清理构建目录。默认执行 Debug 增量构建：

```powershell
.\scripts\Build.ps1
```

支持的参数：

| 参数 | 可选值 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `-Action` | `configure`、`build`、`clean` | `build` | 选择操作 |
| `-Config` | `Debug`、`Release` | `Debug` | 选择构建类型 |
| `-Target` | `all`、`client`、`server`、`tests` | `all` | 选择构建目标 |
| `-RefreshPresets` | 开关 | 关闭 | 重新检测 Qt、Ninja、MSVC 和 SDK |
| `-Deploy` | 开关 | 关闭 | 强制重新部署 Qt 与 MSVC 运行库 |

常用示例：

```powershell
# 仅运行 CMake configure，并生成 compile_commands.json
.\scripts\Build.ps1 -Action configure

# 构建 Debug
.\scripts\Build.ps1 -Action build -Config Debug

# 只构建 Debug 客户端
.\scripts\Build.ps1 -Target client

# 只构建 Debug 服务端
.\scripts\Build.ps1 -Target server

# 只构建测试程序
.\scripts\Build.ps1 -Target tests

# 构建 Release
.\scripts\Build.ps1 -Action build -Config Release

# 工具链升级或安装路径变化后强制刷新
.\scripts\Build.ps1 -RefreshPresets

# 强制重新部署运行库
.\scripts\Build.ps1 -Deploy

# 删除 Debug 构建目录
.\scripts\Build.ps1 -Action clean -Config Debug
```

构建目录分别为：

```text
build/msvc-debug
build/msvc-release
```

首次构建、工具链刷新或缺少必要 Qt DLL 时，脚本会调用 `windeployqt`。之后的普通构建不会重复部署；需要强制更新时使用 `-Deploy`。当前配置的 `compile_commands.json` 会同步到项目根目录供 `clangd` 使用。

## Run.ps1

负责运行已经构建好的程序，必须通过 `-Target` 选择目标：

| Target | 作用 |
| --- | --- |
| `client` | 运行远程控制客户端 |
| `server` | 运行远程控制服务端 |
| `smoke` | 运行协议 smoke test |

常用示例：

```powershell
# 在终端 1 启动服务端
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug

# 在终端 2 启动客户端
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug

# 无托盘运行服务端
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug -NoTray

# 运行 smoke test
.\scripts\Run.ps1 -Target smoke -BuildDir .\build\msvc-debug
```

其他参数：

| 参数 | 说明 |
| --- | --- |
| `-BuildDir` | 指定构建目录；存在多个构建目录时建议提供 |
| `-ServerHost` | 服务端地址，默认为 `127.0.0.1` |
| `-Port` | 服务端端口，默认为 `9527` |
| `-NoTray` | 让服务端以无托盘模式运行 |
| `-LockTestSeconds` | 服务端锁屏测试持续时间 |

## VS Code 和 Qt Creator

- VS Code 的构建和调试任务已经自动调用 `Build.ps1`，通常使用 `Ctrl+Shift+B` 或 `F5` 即可。
- Qt Creator 可直接使用 CMake target；需要自定义运行环境时，可调用 `Run.ps1`。详细配置参见项目根目录 [README](../README.md) 的 Qt Creator 小节。

## 查看命令帮助

两个入口脚本均提供 PowerShell 原生帮助：

```powershell
Get-Help .\scripts\Build.ps1 -Full
Get-Help .\scripts\Run.ps1 -Full
```
