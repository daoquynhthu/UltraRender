param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$Generator = "Ninja",
    [Alias("Target")][string[]]$Targets = @(),
    [switch]$Clean,
    [switch]$CleanOnly,
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [switch]$RemoveAfterBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir

function Write-Info {
    Write-Host "[build_x64] $($args -join ' ')"
}

function Normalize-Targets {
    param([string[]]$RawTargets)

    $items = @()
    foreach ($target in $RawTargets) {
        if ([string]::IsNullOrWhiteSpace($target)) { continue }
        $items += ($target -split "[,;]" |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { $_.Trim() })
    }
    return $items
}

function Get-CommandPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Find-VsWhere {
    $vswhere = Get-CommandPath "vswhere.exe"
    if ($vswhere) { return $vswhere }
    $candidate = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $candidate) { return $candidate }
    return $null
}

function Find-VsInstallRoot {
    $vswhere = Find-VsWhere
    if ($vswhere) {
        $path = & "$vswhere" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($path)) {
            return $path.Trim()
        }
    }

    $fallback = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    if (Test-Path $fallback) { return $fallback }
    return $null
}

function Find-VsDevCmd {
    $root = Find-VsInstallRoot
    if ($root) {
        $candidate = Join-Path $root "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Find-VsCMake {
    $root = Find-VsInstallRoot
    if ($root) {
        $candidate = Join-Path $root "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    return Get-CommandPath "cmake.exe"
}

function Find-VsNinja {
    $root = Find-VsInstallRoot
    if ($root) {
        $candidate = Join-Path $root "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    return Get-CommandPath "ninja.exe"
}

function Assert-Binary {
    param(
        [string]$Name,
        [string]$Path
    )
    if (-not $Path) {
        throw "Required binary '$Name' was not found in the environment."
    }
}

function Remove-BuildDirectory {
    if (Test-Path $BuildPath) {
        Write-Info "Removing build directory: $BuildPath"
        Remove-Item $BuildPath -Recurse -Force
    } else {
        Write-Info "Build directory does not exist: $BuildPath"
    }
}

function Invoke-VsBuildCommand {
    param([string]$Command)

    $vsDevCmd = Find-VsDevCmd
    if ($vsDevCmd) {
        Write-Info "Using Visual Studio developer environment: $vsDevCmd"
        $cmdLine = "chcp 65001 >NUL && set VSLANG=1033 && call `"$vsDevCmd`" -arch=amd64 -host_arch=amd64 && $Command"
        & cmd.exe /c $cmdLine
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code $LASTEXITCODE"
        }
        return
    }

    Write-Info "Visual Studio developer environment not found; falling back to current PATH."
    & cmd.exe /c $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

function Configure-Project {
    $cmake = Find-VsCMake
    $ninja = Find-VsNinja
    Assert-Binary "cmake" $cmake
    Assert-Binary "ninja" $ninja

    Write-Info "Using cmake: $cmake"
    Write-Info "Using ninja: $ninja"

    if ($Clean -and (Test-Path $BuildPath)) {
        Remove-BuildDirectory
    }

    if (-not (Test-Path $BuildPath)) {
        New-Item -ItemType Directory -Path $BuildPath | Out-Null
    }

    $cmakeArgs = @(
        "-S", "`"$RepoRoot`"",
        "-B", "`"$BuildPath`"",
        "-G", "`"$Generator`"",
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DCMAKE_MAKE_PROGRAM=`"$(Find-VsNinja)`""
    )

    $configureCommand = "`"$(Find-VsCMake)`" $($cmakeArgs -join ' ')"
    Write-Info "Configuring project"
    Invoke-VsBuildCommand $configureCommand
}

function Build-Project {
    $cmake = Find-VsCMake
    $buildCommand = "`"$cmake`" --build `"$BuildPath`" --config $Config"
    if ($Targets.Count -gt 0) {
        $buildCommand += " --target $($Targets -join ' ')"
        Write-Info "Building target(s): $($Targets -join ', ')"
    } else {
        Write-Info "Building default target (all)"
    }
    $buildCommand += " --parallel"
    Invoke-VsBuildCommand $buildCommand
}

$Targets = @(Normalize-Targets $Targets)

Write-Info "Repo root: $RepoRoot"
Write-Info "Build dir: $BuildPath"
Write-Info "Configuration: $Config"
Write-Info "Generator: $Generator"
Write-Info "Targets: $($Targets -join ', ')"
Write-Info "Clean: $Clean"
Write-Info "SkipConfigure: $SkipConfigure"
Write-Info "SkipBuild: $SkipBuild"
Write-Info "CleanOnly: $CleanOnly"
Write-Info "RemoveAfterBuild: $RemoveAfterBuild"

if ($CleanOnly) {
    Remove-BuildDirectory
    Write-Info "Clean-only complete."
    return
}

try {
    if (-not $SkipConfigure) {
        Configure-Project
    } else {
        Write-Info "Skipping configuration step."
    }

    if (-not $SkipBuild) {
        Build-Project
    } else {
        Write-Info "Skipping build step."
    }

    if ($RemoveAfterBuild) {
        Remove-BuildDirectory
        Write-Info "Build directory removed after successful build."
    }

    Write-Info "Build completed successfully."
} catch {
    Write-Host "[build_x64] ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
