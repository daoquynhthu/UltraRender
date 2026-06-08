#!/usr/bin/env pwsh

[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [string]$Target = "UltraRender",
    [string]$CudaArchitectures = "all-major",
    [string]$Generator = "",
    [switch]$NoClean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$StepName
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$StepName failed with exit code $LASTEXITCODE."
    }
}

function Remove-PathIfExists {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Import-BatchEnvironment {
    param([Parameter(Mandatory = $true)][string]$BatchFile)

    $batchCommand = 'call "{0}" && set' -f $BatchFile
    $envDump = & cmd /c $batchCommand

    foreach ($line in $envDump) {
        if ($line -match '^[A-Za-z0-9_]+=.*$') {
            $splitIndex = $line.IndexOf('=')
            if ($splitIndex -gt 0) {
                $name = $line.Substring(0, $splitIndex)
                $value = $line.Substring($splitIndex + 1)
                Set-Item -Path "Env:$name" -Value $value
            }
        }
    }
}

function Find-VcVarsBat {
    $candidatePaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )

    foreach ($candidate in $candidatePaths) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installRoot) {
            $fallback = Join-Path $installRoot "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path -LiteralPath $fallback) {
                return $fallback
            }
        }
    }

    throw "Could not locate vcvars64.bat. Install Visual Studio 2022 with the C++ build tools, or open a Developer Command Prompt."
}

function Initialize-WindowsToolchain {
    if (-not $IsWindows) {
        return
    }

    if (-not $env:VSCMD_VER) {
        $vcvars = Find-VcVarsBat
        Write-Host "[Build] Loading Visual Studio environment from: $vcvars"
        Import-BatchEnvironment -BatchFile $vcvars
    } else {
        Write-Host "[Build] Visual Studio environment already active."
    }
}

function Resolve-Nvcc {
    if (Get-Command nvcc -ErrorAction SilentlyContinue) {
        return (Get-Command nvcc).Path
    }

    $candidateFiles = @()

    if ($IsWindows) {
        $cudaRoots = @(
            "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA"
        )
        foreach ($root in $cudaRoots) {
            if (Test-Path -LiteralPath $root) {
                Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
                    ForEach-Object {
                        $candidateFiles += (Join-Path $_.FullName "bin\nvcc.exe")
                    }
            }
        }
    } else {
        $candidateFiles += "/usr/local/cuda/bin/nvcc"
        if (Test-Path -LiteralPath "/usr/local") {
            Get-ChildItem -LiteralPath "/usr/local" -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -like "cuda*" } |
                ForEach-Object {
                    $candidateFiles += (Join-Path $_.FullName "bin/nvcc")
                }
        }
    }

    foreach ($candidate in $candidateFiles) {
        if (Test-Path -LiteralPath $candidate) {
            $binDir = Split-Path -Parent $candidate
            if ($env:PATH -notlike "*$binDir*") {
                $env:PATH = "$binDir$([System.IO.Path]::PathSeparator)$env:PATH"
            }
            return $candidate
        }
    }

    throw "nvcc not found. Install the CUDA Toolkit and ensure its bin directory is on PATH."
}

function Get-DefaultGenerator {
    param([string]$RequestedGenerator)

    if ($RequestedGenerator) {
        return $RequestedGenerator
    }

    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        return "Ninja"
    }

    if ($IsWindows) {
        return "Visual Studio 17 2022"
    }

    return "Unix Makefiles"
}

function Get-ArtifactPath {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$ConfigName
    )

    $candidates = @(
        (Join-Path $BuildRoot "bin/UltraRender"),
        (Join-Path $BuildRoot "bin/UltraRender.exe"),
        (Join-Path $BuildRoot "$ConfigName/UltraRender"),
        (Join-Path $BuildRoot "$ConfigName/UltraRender.exe"),
        (Join-Path $BuildRoot "UltraRender"),
        (Join-Path $BuildRoot "UltraRender.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

$repoRoot = Split-Path -Parent $PSCommandPath
if (-not $repoRoot) {
    $repoRoot = (Get-Location).Path
}

Push-Location $repoRoot
try {
    Write-Host "[Build] Repository root: $repoRoot"

    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake was not found on PATH."
    }

    Initialize-WindowsToolchain
    $nvccPath = Resolve-Nvcc
    Write-Host "[Build] CUDA compiler: $nvccPath"

    $resolvedGenerator = Get-DefaultGenerator -RequestedGenerator $Generator
    Write-Host "[Build] Generator: $resolvedGenerator"
    Write-Host "[Build] CUDA architectures: $CudaArchitectures"

    if (-not $NoClean) {
        Write-Host "[Build] Cleaning previous build output..."
        Remove-PathIfExists -Path $BuildDir
        Remove-PathIfExists -Path (Join-Path $repoRoot "CMakeCache.txt")
        Remove-PathIfExists -Path (Join-Path $repoRoot "CMakeFiles")
    }

    $configureArgs = @(
        "-S", ".",
        "-B", $BuildDir,
        "-G", $resolvedGenerator,
        "-DCMAKE_CUDA_ARCHITECTURES=$CudaArchitectures"
    )

    if ($resolvedGenerator -eq "Ninja" -or $resolvedGenerator -eq "Unix Makefiles") {
        $configureArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
    } elseif ($resolvedGenerator -like "Visual Studio*") {
        $configureArgs += @("-A", "x64")
    }

    Write-Host "[Build] Configuring project..."
    Invoke-CheckedCommand -FilePath "cmake" -Arguments $configureArgs -StepName "CMake configure"

    $buildArgs = @(
        "--build", $BuildDir,
        "--target", $Target,
        "--parallel"
    )

    if ($resolvedGenerator -like "Visual Studio*") {
        $buildArgs += @("--config", $Configuration)
    }

    Write-Host "[Build] Building target: $Target"
    Invoke-CheckedCommand -FilePath "cmake" -Arguments $buildArgs -StepName "CMake build"

    $artifact = Get-ArtifactPath -BuildRoot $BuildDir -ConfigName $Configuration
    if ($artifact) {
        Write-Host "[Build] Build successful."
        Write-Host "[Build] Artifact: $artifact"
    } else {
        Write-Warning "[Build] Build finished, but the executable path was not found in the usual locations."
    }
}
finally {
    Pop-Location
}
