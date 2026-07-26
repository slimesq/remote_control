function Resolve-QtRoot {
    $configuredRoots = @(
        $env:QTDIR
        [Environment]::GetEnvironmentVariable("QTDIR", "User")
        [Environment]::GetEnvironmentVariable("QTDIR", "Machine")
    ) | Where-Object { $_ } | Select-Object -Unique

    foreach ($configuredRoot in $configuredRoots) {
        if (Test-Path -LiteralPath (Join-Path $configuredRoot "bin")) {
            return (Resolve-Path -LiteralPath $configuredRoot).Path
        }
    }

    $searchRoots = @("C:\Qt", "D:\Qt", "D:\wsqAPP\QT", "D:\ProgramStudy\QT")
    $kits = foreach ($root in $searchRoots) {
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -Filter qmake.exe -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Directory.Name -eq "bin" } |
                ForEach-Object { Split-Path -Parent $_.Directory.FullName }
        }
    }

    $kitCandidates = foreach ($kit in ($kits | Select-Object -Unique)) {
        $kitName = Split-Path -Leaf $kit
        if ($kitName -notmatch "^msvc") {
            continue
        }

        $versionText = Split-Path -Leaf (Split-Path -Parent $kit)
        $version = try { [version]$versionText } catch { [version]"0.0" }
        $compilerYear = if ($kitName -match "msvc(\d{4})") { [int]$Matches[1] } else { 0 }
        [pscustomobject]@{
            Path = $kit
            Version = $version
            CompilerYear = $compilerYear
        }
    }
    $selectedKit = $kitCandidates |
        Sort-Object @{ Expression = "Version"; Descending = $true },
                    @{ Expression = "CompilerYear"; Descending = $true },
                    @{ Expression = "Path"; Descending = $true } |
        Select-Object -First 1
    if ($selectedKit) {
        return $selectedKit.Path
    }

    throw "An MSVC Qt kit was not found. Set QTDIR to the MSVC Qt kit directory."
}

function Resolve-NinjaExecutable {
    param(
        [Parameter(Mandatory)]
        [string]$VisualStudioPath
    )

    if ($env:NINJA_EXE -and (Test-Path -LiteralPath $env:NINJA_EXE)) {
        return (Resolve-Path -LiteralPath $env:NINJA_EXE).Path
    }

    $command = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $visualStudioNinja = Join-Path $VisualStudioPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if (Test-Path -LiteralPath $visualStudioNinja) {
        return (Resolve-Path -LiteralPath $visualStudioNinja).Path
    }

    throw "Ninja was not found. Install Ninja or set NINJA_EXE."
}

function Resolve-RemoteExecutable {
    param(
        [Parameter(Mandatory)]
        [string]$Workspace,
        [Parameter(Mandatory)]
        [string]$FileName,
        [string]$BuildDir = ""
    )

    $roots = if ($BuildDir) { @($BuildDir) } else { @(Join-Path $Workspace "build") }
    $matches = foreach ($root in $roots) {
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -Filter $FileName -Recurse -File -ErrorAction SilentlyContinue
        }
    }
    $matches = @($matches | Sort-Object LastWriteTime -Descending)

    if ($matches.Count -eq 0) {
        throw "Executable not found: $FileName. Build the project first."
    }
    if ($matches.Count -gt 1 -and -not $BuildDir) {
        $locations = ($matches | Select-Object -ExpandProperty FullName) -join [Environment]::NewLine
        throw "Multiple builds contain $FileName. Pass -BuildDir explicitly:`n$locations"
    }

    return $matches[0].FullName
}

function Enable-QtRuntime {
    $qtRoot = Resolve-QtRoot
    $env:PATH = "$(Join-Path $qtRoot 'bin');$env:PATH"
}

function Enable-QtRuntimeForExecutable {
    param(
        [Parameter(Mandatory)]
        [string]$ExecutablePath
    )

    $executableDirectory = Split-Path -Parent $ExecutablePath
    $deployedQtCore = Get-ChildItem -LiteralPath $executableDirectory -Filter "Qt*Core*.dll" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $deployedQtCore) {
        Enable-QtRuntime
    }
}
