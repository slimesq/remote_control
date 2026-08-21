# 项目脚本说明

返回 [项目主页](../README.md)。

日常开发通常只需要下面四条命令：

```powershell
.\scripts\Build.ps1
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug
ctest --test-dir .\build\msvc-debug --output-on-failure
```

脚本入口如下：

- **`Build.ps1`**：日常使用，用于配置、编译、部署和清理项目。
- **`Run.ps1`**：日常使用，用于运行客户端、服务端或 smoke test。
- **`Setup-CMakeUserPresets.ps1`**：检测本机工具链并生成 CMake user presets，通常由构建
  脚本自动调用。
- **`internal/`**：保存 Qt 查找、运行环境和程序定位等公共实现，不直接运行。

## 环境要求

- Windows
- Visual Studio Build Tools，包含 MSVC C++ 工具链
- CMake 3.25 或更高版本
- Ninja
- Qt 5.15 或 Qt 6 的 MSVC Kit
- PowerShell 5.1 或更高版本

脚本优先使用环境变量 `QTDIR` 指定的 Qt Kit。未设置时，会搜索常见 Qt 安装目录。
Ninja 按以下顺序查找：`NINJA_EXE` 指定的程序、`PATH` 中的 `ninja.exe`、Visual Studio
附带的 Ninja。三处都找不到时，初始化脚本会提示安装 Ninja 或设置 `NINJA_EXE`。

例如：

```powershell
$env:QTDIR = "C:\Qt\6.8.3\msvc2022_64"
```

## CMake Presets

项目根目录的 `CMakePresets.json` 保存可共享的配置、构建和测试预设：

- `msvc-debug`：使用 MSVC x64 + Ninja 构建 Debug，构建目录是 `build/msvc-debug`。
- `msvc-release`：使用 MSVC x64 + Ninja 构建 Release，构建目录是 `build/msvc-release`。

`CMakePresets.json` 只保存可共享的生成器、构建类型和目标规则，不包含本机绝对路径。
`CMakeUserPresets.json` 保存当前计算机的 Qt Kit、Ninja、MSVC 和 Windows SDK 路径，
以及生成脚本的 SHA-256 指纹，并且已被 `.gitignore` 忽略。

首次克隆项目或本机工具链发生变化时，运行初始化脚本：

```powershell
.\scripts\Setup-CMakeUserPresets.ps1
```

脚本通过 `vswhere` 和 `VsDevCmd.bat` 自动查找 MSVC 与 Windows SDK，并自动查找 Qt 和
Ninja，然后生成 `CMakeUserPresets.json`。VS Code 中也可以运行任务
`CMake: Setup Local Presets`。

VS Code 的 CMake Tools 应选择 `local-msvc-debug` 或 `local-msvc-release` Configure
Preset，再选择对应的完整项目、`client`、`server` 或 `tests` Build Preset。
换电脑时只需重新运行初始化脚本，共享文件不需要改动。

构建时可以选择完整项目、客户端、服务端或测试程序：

**Debug build presets**

- `local-msvc-debug`：全部目标。
- `local-msvc-debug-client`：客户端 `RemoteControlClient`。
- `local-msvc-debug-server`：服务端 `RemoteControlServer`。
- `local-msvc-debug-tests`：协议、客户端 worker、smoke、transport 生命周期、状态机和韧性
  测试程序。

**Release build presets**

- `local-msvc-release`：全部目标。
- `local-msvc-release-client`：客户端 `RemoteControlClient`。
- `local-msvc-release-server`：服务端 `RemoteControlServer`。
- `local-msvc-release-tests`：协议、客户端 worker、smoke、transport 生命周期、状态机和韧性
  测试程序。

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

# 只构建 Debug 测试程序（包括 smoke test 可执行文件）
cmake --build --preset local-msvc-debug-tests

# 构建 Release 全部目标
cmake --build --preset local-msvc-release

# 测试
ctest --preset local-msvc-debug
```

## Build.ps1

负责检查本机 preset、调用 CMake、同步 `compile_commands.json`、按需部署运行库和清理
构建目录。无参数调用时执行顶部示例中的 Debug 增量构建。

支持的参数：

- **`-Action`**：可选 `configure`、`build` 或 `clean`，默认为 `build`。
  `configure` 只生成构建系统；`build` 按需配置后编译；`clean` 删除对应构建目录。
- **`-Config`**：可选 `Debug` 或 `Release`，默认为 `Debug`；适用于所有 `Action`，用于选择
  配置或清理的构建类型。
- **`-Target`**：可选 `all`、`client`、`server` 或 `tests`，默认为 `all`；仅适用于
  `build`。它选择编译目标；`configure` 始终配置完整项目，`clean` 始终清理整个配置目录。
- **`-RefreshPresets`**：开关参数，默认关闭；适用于 `configure` 和 `build`。启用后重新检测
  Qt、Ninja、MSVC 和 SDK；`clean` 不使用该参数。
- **`-Deploy`**：开关参数，默认关闭；仅适用于 `build`。启用后强制重新部署 Qt 与 MSVC
  运行库。

常用示例：

```powershell
# 仅运行 CMake configure，并生成 compile_commands.json
.\scripts\Build.ps1 -Action configure

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

Debug 和 Release 的构建目录分别为 `build/msvc-debug` 和 `build/msvc-release`。

`build` 操作会在本地 preset 发生刷新或构建目录尚未配置时先执行 configure，然后并行
编译。首次构建、preset 刷新、显式指定 `-Deploy`，或探测用 Qt Core/Widgets DLL 不存在时，
脚本会调用 `windeployqt`；普通增量构建不会重复部署。该探测并不是完整的 Qt 依赖审计；如果
程序仍报告缺少 Qt DLL，应执行 `.\scripts\Build.ps1 -Deploy`。脚本会先检查当前
`windeployqt` 支持的参数，因此可同时适配 Qt 5.15 和 Qt 6。当前配置生成的
`compile_commands.json` 会同步到项目根目录供 `clangd` 使用。

`Build.ps1` 还会检查本地 preset、工具链路径和初始化脚本的 SHA-256 指纹；任一项失效时
自动重新生成 `CMakeUserPresets.json`。工具链安装位置变化后，也可以使用
`-RefreshPresets` 强制刷新。

## Run.ps1

负责运行已经构建好的程序，不会自动构建。必须通过 `-Target` 选择目标：

- `client`：运行远程控制客户端。
- `server`：运行远程控制服务端。
- `smoke`：运行端到端 smoke test，需要服务端已经运行。

以下示例只展示顶部日常流程之外的运行参数：

```powershell
# 无托盘运行服务端
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug -NoTray

# 连接指定地址和端口
.\scripts\Run.ps1 -Target client -ServerHost 192.0.2.10 -Port 9527
```

参数说明：

- **`-Target`**：必须指定，只能是 `client`、`server` 或 `smoke`。
- **`-BuildDir`**：适用于全部 target，默认自动搜索 `build/`。它限定程序查找目录；未指定且
  存在多个匹配程序时，脚本会要求显式指定。
- **`-ServerHost`**：适用于 `client` 和 `smoke`，默认为 `127.0.0.1`；用于指定服务端地址，
  对 `server` 不生效。
- **`-Port`**：适用于全部 target，默认为 `9527`，取值范围是 `1`～`65535`；`client` 和
  `smoke` 使用它连接服务端，`server` 使用它监听端口。
- **`-NoTray`**：仅适用于 `server`，默认关闭；启用后传递 `--no-tray`，对其他 target 不生效。
- **`-LockTestSeconds`**：仅适用于 `server`，默认为 `0`；仅大于 `0` 时运行定时模拟锁定测试，
  其他 target 不使用该参数。

`Run.ps1` 不提供当前用户登录启动项的安装、删除参数和 UAC 提权参数。这些维护操作
需要直接运行构建目录中的 `RemoteControlServer.exe`，具体参数见项目根目录
[README 的命令行配置](../README.md#命令行配置)。

## 测试

完整测试矩阵和各测试的覆盖范围统一记录在项目 [README 的测试章节](../README.md#测试)。
本文只说明脚本相关的运行方式和副作用边界。无系统副作用的测试已注册到
CTest，不需要人工启动服务端：

```powershell
ctest --test-dir .\build\msvc-debug --output-on-failure
```

端到端 smoke test 使用测试进程创建的本机临时路径，因此测试程序和服务端应运行在同一台
受控 Windows 主机上。需要先在另一个终端启动服务端：

```powershell
# 终端 1
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug

# 终端 2
.\scripts\Run.ps1 -Target smoke -BuildDir .\build\msvc-debug
```

smoke test 会调用服务端截图和鼠标移动路径，请求启动
`C:\Windows\System32\whoami.exe`，并在测试进程的临时目录中创建、下载和删除文件、目录及
junction。它只能连接专用的受控测试服务端。

## VS Code 和 Qt Creator

- VS Code 的构建和调试任务已经自动调用 `Build.ps1`，通常使用 `Ctrl+Shift+B` 或 `F5` 即可。
- Qt Creator 可直接使用 CMake target；需要自定义运行环境时，可调用 `Run.ps1`。详细配置参见
  项目根目录 [README 的 Qt Creator 小节](../README.md#qt-creator)。

## 查看命令帮助

三个可执行脚本均提供 PowerShell 原生帮助：

```powershell
Get-Help .\scripts\Build.ps1 -Full
Get-Help .\scripts\Run.ps1 -Full
Get-Help .\scripts\Setup-CMakeUserPresets.ps1 -Full
```
