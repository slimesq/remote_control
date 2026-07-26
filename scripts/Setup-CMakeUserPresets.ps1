<#
.SYNOPSIS
为当前计算机生成 CMakeUserPresets.json。

.DESCRIPTION
自动查找 Qt、Ninja、Visual Studio 和 Windows SDK，并从 VsDevCmd.bat
提取 MSVC x64 编译环境。生成的 CMakeUserPresets.json 只供当前计算机
使用，已被 Git 忽略。

.EXAMPLE
.\scripts\Setup-CMakeUserPresets.ps1
#>

$ErrorActionPreference = "Stop"

$workspace = Split-Path -Parent $PSScriptRoot
$outputPath = Join-Path $workspace "CMakeUserPresets.json"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

. (Join-Path $PSScriptRoot "internal\Common.ps1")

function Get-VisualStudioEnvironment {
    param(
        [Parameter(Mandatory)]
        [string]$DeveloperCommandPath
    )

    $commandLine = 'call "{0}" -arch=x64 -host_arch=x64 >nul && set' -f $DeveloperCommandPath
    $lines = & cmd.exe /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize the Visual Studio developer environment."
    }

    $environment = @{}
    foreach ($line in $lines) {
        $separator = $line.IndexOf("=")
        if ($separator -le 0) {
            continue
        }

        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        $environment[$name] = $value
    }

    foreach ($requiredName in @("Path", "INCLUDE", "LIB", "LIBPATH")) {
        if (-not $environment[$requiredName]) {
            throw "Visual Studio environment variable is missing: $requiredName"
        }
    }

    return $environment
}

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe was not found. Install Visual Studio Build Tools with C++ support."
}

$visualStudioPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioPath) {
    throw "Visual Studio with the MSVC C++ toolchain was not found."
}

$developerCommandPath = Join-Path $visualStudioPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path -LiteralPath $developerCommandPath)) {
    throw "VsDevCmd.bat was not found: $developerCommandPath"
}

$qtRoot = Resolve-QtRoot
$ninjaExecutable = Resolve-NinjaExecutable -VisualStudioPath $visualStudioPath
$ninjaDirectory = Split-Path -Parent $ninjaExecutable
$visualStudioEnvironment = Get-VisualStudioEnvironment -DeveloperCommandPath $developerCommandPath

$parentPathEntries = @{}
foreach ($entry in ($env:PATH -split ";")) {
    $key = $entry.Trim().TrimEnd("\").ToLowerInvariant()
    if ($key) {
        $parentPathEntries[$key] = $true
    }
}

$localPathEntries = @()
$localPathEntriesSeen = @{}
foreach ($entry in @($ninjaDirectory) + @($visualStudioEnvironment["Path"] -split ";")) {
    $trimmedEntry = $entry.Trim()
    $key = $trimmedEntry.TrimEnd("\").ToLowerInvariant()
    if (-not $key -or $localPathEntriesSeen[$key]) {
        continue
    }
    if ($key -ne $ninjaDirectory.TrimEnd("\").ToLowerInvariant() -and $parentPathEntries[$key]) {
        continue
    }

    $localPathEntriesSeen[$key] = $true
    $localPathEntries += $trimmedEntry
}
$localPath = (@($localPathEntries) + @('$penv{PATH}')) -join ";"

$localEnvironment = [ordered]@{
    QTDIR = $qtRoot
    NINJA_EXE = $ninjaExecutable
    PATH = $localPath
    INCLUDE = $visualStudioEnvironment["INCLUDE"]
    LIB = $visualStudioEnvironment["LIB"]
    LIBPATH = $visualStudioEnvironment["LIBPATH"]
    VSLANG = "1033"
}

foreach ($name in @("VCINSTALLDIR", "VCToolsInstallDir", "VSINSTALLDIR", "WindowsSdkDir", "WindowsSDKVersion")) {
    if ($visualStudioEnvironment[$name]) {
        $localEnvironment[$name] = $visualStudioEnvironment[$name]
    }
}

$presetDocument = [ordered]@{
    version = 6
    include = @("CMakePresets.json")
    configurePresets = @(
        [ordered]@{
            name = "local-msvc-environment"
            hidden = $true
            environment = $localEnvironment
        },
        [ordered]@{
            name = "local-msvc-debug"
            displayName = "Local MSVC Debug (Ninja)"
            inherits = @("msvc-debug", "local-msvc-environment")
        },
        [ordered]@{
            name = "local-msvc-release"
            displayName = "Local MSVC Release (Ninja)"
            inherits = @("msvc-release", "local-msvc-environment")
        }
    )
    buildPresets = @(
        [ordered]@{
            name = "local-msvc-debug"
            displayName = "Build Local MSVC Debug"
            inherits = "msvc-debug"
            configurePreset = "local-msvc-debug"
        },
        [ordered]@{
            name = "local-msvc-debug-client"
            displayName = "Build Local Client (Debug)"
            inherits = "local-msvc-debug"
            targets = @("RemoteControlClient")
        },
        [ordered]@{
            name = "local-msvc-debug-server"
            displayName = "Build Local Server (Debug)"
            inherits = "local-msvc-debug"
            targets = @("RemoteControlServer")
        },
        [ordered]@{
            name = "local-msvc-debug-tests"
            displayName = "Build Local Tests (Debug)"
            inherits = "local-msvc-debug"
            targets = @("RemoteControlProtocolTests", "RemoteControlSmokeTest")
        },
        [ordered]@{
            name = "local-msvc-release"
            displayName = "Build Local MSVC Release"
            inherits = "msvc-release"
            configurePreset = "local-msvc-release"
        },
        [ordered]@{
            name = "local-msvc-release-client"
            displayName = "Build Local Client (Release)"
            inherits = "local-msvc-release"
            targets = @("RemoteControlClient")
        },
        [ordered]@{
            name = "local-msvc-release-server"
            displayName = "Build Local Server (Release)"
            inherits = "local-msvc-release"
            targets = @("RemoteControlServer")
        },
        [ordered]@{
            name = "local-msvc-release-tests"
            displayName = "Build Local Tests (Release)"
            inherits = "local-msvc-release"
            targets = @("RemoteControlProtocolTests", "RemoteControlSmokeTest")
        }
    )
    testPresets = @(
        [ordered]@{
            name = "local-msvc-debug"
            displayName = "Test Local MSVC Debug"
            inherits = "msvc-debug"
            configurePreset = "local-msvc-debug"
        },
        [ordered]@{
            name = "local-msvc-release"
            displayName = "Test Local MSVC Release"
            inherits = "msvc-release"
            configurePreset = "local-msvc-release"
        }
    )
    workflowPresets = @(
        [ordered]@{
            name = "local-msvc-debug"
            displayName = "Configure, Build and Test Local Debug"
            steps = @(
                [ordered]@{ type = "configure"; name = "local-msvc-debug" },
                [ordered]@{ type = "build"; name = "local-msvc-debug" },
                [ordered]@{ type = "test"; name = "local-msvc-debug" }
            )
        },
        [ordered]@{
            name = "local-msvc-release"
            displayName = "Configure, Build and Test Local Release"
            steps = @(
                [ordered]@{ type = "configure"; name = "local-msvc-release" },
                [ordered]@{ type = "build"; name = "local-msvc-release" },
                [ordered]@{ type = "test"; name = "local-msvc-release" }
            )
        }
    )
}

$json = $presetDocument | ConvertTo-Json -Depth 20
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
$jsonContent = "$json`r`n"
$presetChanged = -not (Test-Path -LiteralPath $outputPath) -or
    [System.IO.File]::ReadAllText($outputPath) -cne $jsonContent
if ($presetChanged) {
    [System.IO.File]::WriteAllText($outputPath, $jsonContent, $utf8WithoutBom)
}

$env:QTDIR = $qtRoot
$env:NINJA_EXE = $ninjaExecutable
foreach ($name in @("INCLUDE", "LIB", "LIBPATH", "VCINSTALLDIR", "VCToolsInstallDir", "VSINSTALLDIR", "WindowsSdkDir", "WindowsSDKVersion", "VSLANG")) {
    if ($localEnvironment[$name]) {
        Set-Item -Path "Env:$name" -Value $localEnvironment[$name]
    }
}

$presetStatus = if ($presetChanged) { "Generated" } else { "Unchanged" }
Write-Host "${presetStatus}: $outputPath"
Write-Host "Qt:        $qtRoot"
Write-Host "Ninja:     $ninjaExecutable"
Write-Host "MSVC:      $($visualStudioEnvironment['VCToolsInstallDir'])"
Write-Host "SDK:       $($visualStudioEnvironment['WindowsSDKVersion'])"
