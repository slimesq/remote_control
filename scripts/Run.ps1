<#
.SYNOPSIS
运行项目中的客户端、服务端或 smoke test。

.DESCRIPTION
自动设置 Qt Runtime，并从指定构建目录递归查找目标程序。

.PARAMETER Target
client：客户端；server：服务端；smoke：需要运行中服务端的端到端测试。

.PARAMETER BuildDir
可选的构建目录，适用于所有 target。未指定时在项目的 build 目录中查找；如果找到多个匹配程序，
脚本会要求显式指定构建目录。

.PARAMETER ServerHost
服务端地址，默认为 127.0.0.1。仅对 client 和 smoke 生效；server 始终监听自身配置的网络接口。

.PARAMETER Port
服务端端口，取值范围为 1 到 65535，默认为 9527。client 和 smoke 使用它连接服务端，
server 使用它设置监听端口。

.PARAMETER NoTray
仅对 server 生效。指定后向服务端传递 --no-tray，不创建系统托盘图标。

.PARAMETER LockTestSeconds
仅对 server 生效。大于 0 时向服务端传递 --lock-test，并在指定秒数后自动解除模拟锁定；
默认值 0 表示不运行锁定测试。

.EXAMPLE
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug

.EXAMPLE
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug -NoTray

.EXAMPLE
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug -ServerHost 192.168.1.20 -Port 9527

.EXAMPLE
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug -NoTray -LockTestSeconds 2

.EXAMPLE
.\scripts\Run.ps1 -Target smoke -BuildDir .\build\msvc-debug -ServerHost 127.0.0.1 -Port 9527
#>
param(
    [Parameter(Mandatory)]
    [ValidateSet("client", "server", "smoke")]
    [string]$Target,
    [string]$BuildDir = "",
    [string]$ServerHost = "127.0.0.1",
    [ValidateRange(1, 65535)]
    [int]$Port = 9527,
    [switch]$NoTray,
    [int]$LockTestSeconds = 0
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "internal\Common.ps1")

$workspace = Split-Path -Parent $PSScriptRoot

if ($Target -eq "client") {
    $executable = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlClient.exe" -BuildDir $BuildDir
    Enable-QtRuntimeForExecutable -ExecutablePath $executable
    & $executable --server-host $ServerHost --server-port $Port
    exit $LASTEXITCODE
}

if ($Target -eq "server") {
    $executable = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlServer.exe" -BuildDir $BuildDir
    Enable-QtRuntimeForExecutable -ExecutablePath $executable
    $arguments = @("--port", $Port.ToString())
    if ($NoTray) { $arguments += "--no-tray" }
    if ($LockTestSeconds -gt 0) { $arguments += "--lock-test", $LockTestSeconds.ToString() }
    & $executable @arguments
    exit $LASTEXITCODE
}

if ($Target -eq "smoke") {
    $executable = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlSmokeTests.exe" -BuildDir $BuildDir
    Enable-QtRuntimeForExecutable -ExecutablePath $executable
    & $executable $ServerHost $Port
    exit $LASTEXITCODE
}
