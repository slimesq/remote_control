<#
.SYNOPSIS
配置、构建或清理项目。

.DESCRIPTION
自动查找 Qt、MSVC、CMake 和 Ninja，并调用 CMakePresets.json 中的 MSVC 构建预设。
构建完成后按需执行 windeployqt，并生成供 clangd 使用的 compile_commands.json。

.PARAMETER Action
configure：仅生成构建系统；build：在需要时先配置再构建；clean：删除对应构建目录。

.PARAMETER Config
选择 Debug 或 Release，默认为 Debug。

.PARAMETER Target
选择构建全部目标、客户端、服务端或测试程序，默认为 all。

.PARAMETER RefreshPresets
强制重新检测本机工具链并生成 CMakeUserPresets.json。

.PARAMETER Deploy
即使运行库已经存在，也强制重新执行 windeployqt。

.EXAMPLE
.\scripts\Build.ps1

.EXAMPLE
.\scripts\Build.ps1 -Target client

.EXAMPLE
.\scripts\Build.ps1 -Action clean -Config Debug
#>
param(
    [ValidateSet("configure", "build", "clean")]
    [string]$Action = "build",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [ValidateSet("all", "client", "server", "tests")]
    [string]$Target = "all",
    [switch]$RefreshPresets,
    [switch]$Deploy
)

$ErrorActionPreference = "Stop"

$workspace = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $workspace ("build\msvc-" + $Config.ToLowerInvariant())
$configurePreset = "local-msvc-" + $Config.ToLowerInvariant()
$buildPreset = $configurePreset
if ($Target -ne "all") {
    $buildPreset += "-$Target"
}
$userPresetsPath = Join-Path $workspace "CMakeUserPresets.json"
$cachePath = Join-Path $buildDir "CMakeCache.txt"
$presetGeneratorPath = Join-Path $PSScriptRoot "Setup-CMakeUserPresets.ps1"
$expectedPresetGeneratorFingerprint =
    (Get-FileHash -LiteralPath $presetGeneratorPath -Algorithm SHA256).Hash

if ($Action -eq "clean") {
    if (Test-Path -LiteralPath $buildDir) {
        Remove-Item -LiteralPath $buildDir -Recurse -Force
    }
    exit 0
}

function Test-LocalPresets {
    if (-not (Test-Path -LiteralPath $userPresetsPath)) {
        return $false
    }

    try {
        $document = Get-Content -LiteralPath $userPresetsPath -Raw -Encoding utf8 | ConvertFrom-Json
    } catch {
        return $false
    }

    $presetGeneratorFingerprint = $document.vendor."remote-control".generatorFingerprint
    if ($presetGeneratorFingerprint -ne $expectedPresetGeneratorFingerprint) {
        return $false
    }

    $environmentPreset = $document.configurePresets | Where-Object Name -eq "local-msvc-environment"
    $buildPresetExists = $document.buildPresets | Where-Object Name -eq $buildPreset
    if (-not $environmentPreset -or -not $buildPresetExists) {
        return $false
    }

    $environment = $environmentPreset.environment
    if (-not $environment.QTDIR -or -not $environment.NINJA_EXE -or
        -not $environment.VCToolsInstallDir -or -not $environment.WindowsSdkDir) {
        return $false
    }

    return (Test-Path -LiteralPath (Join-Path $environment.QTDIR "bin")) -and
        (Test-Path -LiteralPath $environment.NINJA_EXE) -and
        (Test-Path -LiteralPath $environment.VCToolsInstallDir) -and
        (Test-Path -LiteralPath $environment.WindowsSdkDir)
}

$presetsRefreshed = $RefreshPresets -or -not (Test-LocalPresets)
if ($presetsRefreshed) {
    & $presetGeneratorPath
}

$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source

function Sync-CompileCommands {
    $compileCommands = Join-Path $buildDir "compile_commands.json"
    if (Test-Path -LiteralPath $compileCommands) {
        Copy-Item -LiteralPath $compileCommands -Destination (Join-Path $workspace "compile_commands.json") -Force
    }
}

$shouldConfigure = $Action -eq "configure" -or $presetsRefreshed -or -not (Test-Path -LiteralPath $cachePath)
if ($shouldConfigure) {
    & $cmake --preset $configurePreset
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Sync-CompileCommands
}

if ($Action -eq "configure") {
    exit 0
}

& $cmake --build --preset $buildPreset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Sync-CompileCommands

$binDir = Join-Path $buildDir "bin"
$debugSuffix = if ($Config -eq "Debug") { "d" } else { "" }
$requiredQtModule = if ($Target -eq "tests") { "Core" } else { "Widgets" }
$requiredQtRuntime = Get-ChildItem -LiteralPath $binDir -Filter "Qt*$requiredQtModule$debugSuffix.dll" -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
$shouldDeploy = $Deploy -or $presetsRefreshed -or -not $requiredQtRuntime
if ($shouldDeploy) {
    if (-not $presetsRefreshed) {
        & (Join-Path $PSScriptRoot "Setup-CMakeUserPresets.ps1")
    }

    . (Join-Path $PSScriptRoot "internal\Common.ps1")
    $qtRoot = Resolve-QtRoot
    $deployTool = Join-Path $qtRoot "bin\windeployqt.exe"
    if (-not (Test-Path -LiteralPath $deployTool)) {
        throw "windeployqt.exe was not found: $deployTool"
    }

    $deployTargets = @(
        "RemoteControlClient.exe"
        "RemoteControlServer.exe"
        "RemoteControlSmokeTests.exe"
        "RemoteControlProtocolTests.exe"
        "RemoteControlClientWorkerLifecycleTests.exe"
        "RemoteControlTransportLifecycleTests.exe"
        "RemoteControlConnectionStateTests.exe"
        "RemoteControlTransportResilienceTests.exe"
    )
    $executables = @(Get-ChildItem -LiteralPath $buildDir -Filter "*.exe" -Recurse -File | Where-Object Name -In $deployTargets)
    $deployArguments = @("--no-translations", "--compiler-runtime")
    $deployHelp = (& $deployTool --help 2>&1 | Out-String)
    if ($deployHelp -match "--no-system-dxc-compiler") {
        $deployArguments += "--no-system-dxc-compiler"
    }
    $deployArguments += @($executables | Select-Object -ExpandProperty FullName)
    if ($executables.Count -gt 0) {
        & $deployTool @deployArguments
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}
