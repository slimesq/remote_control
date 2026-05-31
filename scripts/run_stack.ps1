param(
    [string]$Config = "Debug",
    [string]$BuildDir = "",
    [string]$ServerHost = "127.0.0.1",
    [int]$Port = 9527,
    [switch]$NoTray,
    [int]$StartupTimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"

function Resolve-QtBin {
    $candidates = @()

    if ($env:QTDIR) {
        $candidates += (Join-Path $env:QTDIR "bin")
    }

    $candidates += @(
        "D:\ProgramStudy\QT\6.7.3\msvc2022_64\bin"
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    throw "Qt bin directory was not found. Set QTDIR or update the script."
}

function Resolve-ExecutablePath {
    param(
        [string]$Workspace,
        [string]$Configuration,
        [string]$FileName,
        [string]$PreferredBuildDir
    )

    $normalizedConfig = $Configuration.ToLowerInvariant()
    $candidates = @()

    if ($PreferredBuildDir) {
        $candidates += (Join-Path $PreferredBuildDir $FileName)
    }

    $candidates += @(
        (Join-Path $Workspace "build\Desktop_Qt_6_7_3_MSVC2022_64bit-$Configuration\$FileName"),
        (Join-Path $Workspace "build\qtcreator-$normalizedConfig\$FileName"),
        (Join-Path $Workspace "build\$Configuration\$FileName")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $buildRoots = @(
        (Join-Path $Workspace "build"),
        (Join-Path $Workspace "build-*")
    )

    foreach ($root in $buildRoots) {
        $matches = Get-ChildItem -Path $root -Filter $FileName -Recurse -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending
        if ($matches.Count -eq 1) {
            return $matches[0].FullName
        }
        if ($matches.Count -gt 1) {
            $locations = ($matches | Select-Object -ExpandProperty FullName) -join [Environment]::NewLine
            throw "Multiple matching executables were found for $FileName. Pass -BuildDir to choose one:`n$locations"
        }
    }

    throw "Executable not found: $FileName"
}

function Test-ServerReady {
    param(
        [string]$TargetHost,
        [int]$TargetPort
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $asyncResult = $client.BeginConnect($TargetHost, $TargetPort, $null, $null)
        if (-not $asyncResult.AsyncWaitHandle.WaitOne(250)) {
            return $false
        }
        $client.EndConnect($asyncResult)
        return $true
    } catch {
        return $false
    } finally {
        $client.Dispose()
    }
}

$workspace = Split-Path -Parent $PSScriptRoot
$qtBin = Resolve-QtBin
$preferredBuildDir = if ($BuildDir) { $BuildDir } elseif ($env:QT_CREATOR_BUILD_DIR) { $env:QT_CREATOR_BUILD_DIR } else { "" }
$serverExe = Resolve-ExecutablePath -Workspace $workspace -Configuration $Config -FileName "remote_server_qt.exe" -PreferredBuildDir $preferredBuildDir
$clientExe = Resolve-ExecutablePath -Workspace $workspace -Configuration $Config -FileName "remote_client_qt.exe" -PreferredBuildDir $preferredBuildDir

$env:PATH = "$qtBin;$env:PATH"

$serverWasStarted = $false
if (-not (Test-ServerReady -TargetHost $ServerHost -TargetPort $Port)) {
    $serverArguments = @("--port", $Port.ToString())
    if ($NoTray) {
        $serverArguments += "--no-tray"
    }

    $serverProcess = Start-Process -FilePath $serverExe `
        -ArgumentList $serverArguments `
        -WorkingDirectory $workspace `
        -PassThru
    $serverWasStarted = $true

    $deadline = (Get-Date).AddSeconds($StartupTimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($serverProcess.HasExited) {
            throw "The server exited before it started listening."
        }

        if (Test-ServerReady -TargetHost $ServerHost -TargetPort $Port) {
            break
        }

        Start-Sleep -Milliseconds 250
    }

    if (-not (Test-ServerReady -TargetHost $ServerHost -TargetPort $Port)) {
        throw "The server did not start listening on $ServerHost`:$Port within $StartupTimeoutSeconds seconds."
    }
}

$clientProcess = Start-Process -FilePath $clientExe `
    -ArgumentList @(
        "--no-local-server",
        "--server-host", $ServerHost,
        "--server-port", $Port.ToString()
    ) `
    -WorkingDirectory $workspace `
    -PassThru

if ($serverWasStarted) {
    Write-Output "Started server: $($serverProcess.Id)"
} else {
    Write-Output "Server already listening on $ServerHost`:$Port"
}
Write-Output "Started client: $($clientProcess.Id)"
