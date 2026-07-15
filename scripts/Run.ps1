<#
.SYNOPSIS
运行项目中的客户端、服务端、完整本地环境或 smoke test。

.DESCRIPTION
自动设置 Qt Runtime，并从指定构建目录递归查找目标程序。

.PARAMETER Target
client：客户端；server：服务端；stack：服务端和客户端；smoke：协议测试。

.PARAMETER BuildDir
可选的构建目录。存在多个构建目录时建议显式指定。

.EXAMPLE
.\scripts\Run.ps1 -Target client -BuildDir .\build\vscode-debug

.EXAMPLE
.\scripts\Run.ps1 -Target stack -NoTray
#>
param(
    [Parameter(Mandatory)]
    [ValidateSet("client", "server", "stack", "smoke")]
    [string]$Target,
    [string]$BuildDir = "",
    [string]$ServerHost = "127.0.0.1",
    [int]$Port = 9527,
    [switch]$NoTray,
    [int]$LockTestSeconds = 0,
    [int]$StartupTimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "internal\Common.ps1")

function Test-ServerReady {
    param([string]$HostName, [int]$TargetPort)

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $result = $client.BeginConnect($HostName, $TargetPort, $null, $null)
        if (-not $result.AsyncWaitHandle.WaitOne(250)) {
            return $false
        }
        $client.EndConnect($result)
        return $true
    } catch {
        return $false
    } finally {
        $client.Dispose()
    }
}

$workspace = Split-Path -Parent $PSScriptRoot
Enable-QtRuntime

if ($Target -eq "client") {
    $executable = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlClient.exe" -BuildDir $BuildDir
    & $executable
    exit $LASTEXITCODE
}

if ($Target -eq "server") {
    $executable = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlServer.exe" -BuildDir $BuildDir
    $arguments = @("--port", $Port.ToString())
    if ($NoTray) { $arguments += "--no-tray" }
    if ($LockTestSeconds -gt 0) { $arguments += "--lock-test", $LockTestSeconds.ToString() }
    & $executable @arguments
    exit $LASTEXITCODE
}

if ($Target -eq "smoke") {
    $executable = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlSmokeTest.exe" -BuildDir $BuildDir
    & $executable $ServerHost $Port
    exit $LASTEXITCODE
}

$serverExe = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlServer.exe" -BuildDir $BuildDir
$clientExe = Resolve-RemoteExecutable -Workspace $workspace -FileName "RemoteControlClient.exe" -BuildDir $BuildDir
$serverWasStarted = $false

if (-not (Test-ServerReady -HostName $ServerHost -TargetPort $Port)) {
    $serverArguments = @("--port", $Port.ToString())
    if ($NoTray) { $serverArguments += "--no-tray" }
    $serverProcess = Start-Process -FilePath $serverExe -ArgumentList $serverArguments -WorkingDirectory $workspace -PassThru -WindowStyle Hidden
    $serverWasStarted = $true

    $deadline = (Get-Date).AddSeconds($StartupTimeoutSeconds)
    while ((Get-Date) -lt $deadline -and -not (Test-ServerReady -HostName $ServerHost -TargetPort $Port)) {
        if ($serverProcess.HasExited) {
            throw "The server exited before it started listening."
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not (Test-ServerReady -HostName $ServerHost -TargetPort $Port)) {
        throw "The server did not start listening on $ServerHost`:$Port."
    }
}

$clientArguments = @("--no-local-server", "--server-host", $ServerHost, "--server-port", $Port.ToString())
$clientProcess = Start-Process -FilePath $clientExe -ArgumentList $clientArguments -WorkingDirectory $workspace -PassThru
if ($serverWasStarted) { Write-Output "Started server: $($serverProcess.Id)" }
Write-Output "Started client: $($clientProcess.Id)"
