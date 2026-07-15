<#
.SYNOPSIS
配置、构建或清理项目。

.DESCRIPTION
自动查找 Qt、MSVC、CMake 和 Ninja，使用 build/vscode-<config> 作为构建目录。
构建完成后自动执行 windeployqt，并生成供 clangd 使用的 compile_commands.json。

.PARAMETER Action
configure：仅生成构建系统；build：配置并构建；clean：删除对应构建目录。

.PARAMETER Config
选择 Debug 或 Release，默认为 Debug。

.EXAMPLE
.\scripts\Build.ps1

.EXAMPLE
.\scripts\Build.ps1 -Action clean -Config Debug
#>
param(
    [ValidateSet("configure", "build", "clean")]
    [string]$Action = "build",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$workspace = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $workspace ("build\vscode-" + $Config.ToLowerInvariant())
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
. (Join-Path $PSScriptRoot "internal\Common.ps1")
$qtRoot = Resolve-QtRoot

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe not found. Please install Visual Studio Build Tools with C++ support."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "MSVC Build Tools not found."
}

$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
$ninja = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source

if ($Action -eq "clean") {
    if (Test-Path -LiteralPath $buildDir) {
        Remove-Item -LiteralPath $buildDir -Recurse -Force
    }
    exit 0
}

$configureArgs = 'call "{0}" -arch=amd64 && "{1}" -S "{2}" -B "{3}" -G Ninja -DCMAKE_BUILD_TYPE={4} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_PREFIX_PATH="{5}" -DCMAKE_MAKE_PROGRAM="{6}"' -f $devCmd, $cmake, $workspace, $buildDir, $Config, $qtRoot, $ninja
& cmd.exe /d /s /c $configureArgs
if ($LASTEXITCODE -ne 0 -or $Action -eq "configure") {
    exit $LASTEXITCODE
}

$buildArgs = 'call "{0}" -arch=amd64 && "{1}" --build "{2}" --parallel' -f $devCmd, $cmake, $buildDir
& cmd.exe /d /s /c $buildArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$deployTool = Join-Path $qtRoot "bin\windeployqt.exe"
$deployTargets = @("RemoteControlClient.exe", "RemoteControlServer.exe", "RemoteControlSmokeTest.exe", "RemoteControlProtocolTests.exe")
foreach ($executable in Get-ChildItem -LiteralPath $buildDir -Filter "*.exe" -Recurse -File | Where-Object Name -In $deployTargets) {
    & $deployTool --no-translations --no-compiler-runtime $executable.FullName
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
