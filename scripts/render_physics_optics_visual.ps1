param(
    [string]$BuildDir = "build_modular",
    [string]$Config = "RelWithDebInfo",
    [string]$Scene = "scenes\physics_optics_visual.scene",
    [int]$Spp = 64,
    [int]$Width = 640,
    [int]$Height = 360,
    [string]$Output = "physics_optics_visual.bmp"
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $repo $BuildDir
$cliPath = Join-Path $buildPath "apps\ure_cli\$Config\ure_cli.exe"
$outputPath = Join-Path $repo "output\$Output"

function Run-Step {
    param(
        [string]$Name,
        [scriptblock]$Body
    )
    Write-Host "==> $Name"
    & $Body
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Run-Step "Build ure_cli" {
    cmake --build $buildPath --config $Config --target ure_cli -- /m:1
}

Push-Location $repo
try {
    Run-Step "Render $Scene" {
        & $cliPath -q render $Scene --spp $Spp --width $Width --height $Height -o $Output
    }
} finally {
    Pop-Location
}

if (!(Test-Path $outputPath)) {
    throw "Expected render output was not created: $outputPath"
}

$length = (Get-Item $outputPath).Length
if ($length -le 1024) {
    throw "Render output is unexpectedly small: $outputPath ($length bytes)"
}

Write-Host "Visual render written to $outputPath"
