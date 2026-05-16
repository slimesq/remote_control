param(
    [string]$Config = "Debug",
    [switch]$NoTray,
    [int]$LockTestSeconds = 0
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
        [string]$FileName
    )

    $normalizedConfig = $Configuration.ToLowerInvariant()
    $candidates = @(
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
        if ($matches) {
            return $matches[0].FullName
        }
    }

    throw "Executable not found: $FileName"
}

$workspace = Split-Path -Parent $PSScriptRoot
$qtBin = Resolve-QtBin
$exe = Resolve-ExecutablePath -Workspace $workspace -Configuration $Config -FileName "remote_server_qt.exe"

$env:PATH = "$qtBin;$env:PATH"

$arguments = @()
if ($NoTray) {
    $arguments += "--no-tray"
}
if ($LockTestSeconds -gt 0) {
    $arguments += "--lock-test"
    $arguments += $LockTestSeconds.ToString()
}

& $exe @arguments
