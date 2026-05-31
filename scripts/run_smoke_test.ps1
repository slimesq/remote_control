param(
    [string]$Config = "Debug",
    [string]$BuildDir = "",
    [string]$ServerHost = "127.0.0.1",
    [int]$Port = 9527
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

$workspace = Split-Path -Parent $PSScriptRoot
$qtBin = Resolve-QtBin
$preferredBuildDir = if ($BuildDir) { $BuildDir } elseif ($env:QT_CREATOR_BUILD_DIR) { $env:QT_CREATOR_BUILD_DIR } else { "" }
$exe = Resolve-ExecutablePath -Workspace $workspace -Configuration $Config -FileName "remote_smoke_test.exe" -PreferredBuildDir $preferredBuildDir

$env:PATH = "$qtBin;$env:PATH"
& $exe $ServerHost $Port
