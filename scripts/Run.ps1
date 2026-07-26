<#
.SYNOPSIS
运行项目中的客户端、服务端或 smoke test。

.DESCRIPTION
自动设置 Qt Runtime，并从指定构建目录递归查找目标程序。

.PARAMETER Target
client：客户端；server：服务端；smoke：协议测试。

.PARAMETER BuildDir
可选的构建目录。存在多个构建目录时建议显式指定。

.EXAMPLE
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug

.EXAMPLE
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug -NoTray
#>
param(
    [Parameter(Mandatory)]
    [ValidateSet("client", "server", "smoke")]
    [string]$Target,
    [string]$BuildDir = "",
    [string]$ServerHost = "127.0.0.1",
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
    $executable = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlSmokeTest.exe" -BuildDir $BuildDir
    Enable-QtRuntimeForExecutable -ExecutablePath $executable
    & $executable $ServerHost $Port
    exit $LASTEXITCODE
}
