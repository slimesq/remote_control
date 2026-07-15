function Resolve-QtRoot {
    if ($env:QTDIR) {
        if (Test-Path -LiteralPath (Join-Path $env:QTDIR "bin")) {
            return (Resolve-Path -LiteralPath $env:QTDIR).Path
        }
        throw "QTDIR does not point to a valid Qt kit: $env:QTDIR"
    }

    $searchRoots = @("C:\Qt", "D:\Qt", "D:\wsqAPP\QT", "D:\ProgramStudy\QT")
    $kits = foreach ($root in $searchRoots) {
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -Filter qmake.exe -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Directory.Name -eq "bin" } |
                ForEach-Object { Split-Path -Parent $_.Directory.FullName }
        }
    }
    $kit = $kits | Where-Object { (Split-Path -Leaf $_) -match "^(msvc|mingw)" } | Sort-Object -Descending | Select-Object -First 1
    if ($kit) {
        return $kit
    }

    throw "Qt was not found. Set QTDIR to the Qt kit directory."
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
