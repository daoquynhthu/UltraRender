param(
    [string]$BuildDir = "build_modular",
    [string]$Config = "RelWithDebInfo",
    [switch]$RenderVisual
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $repo $BuildDir

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

$targets = @(
    "gpu_test_math",
    "gpu_test_spectral_soa",
    "gpu_test_render"
)

foreach ($target in $targets) {
    Run-Step "Build $target" {
        cmake --build $buildPath --config $Config --target $target -- /m:1
    }
}

$executables = @(
    "tests\gpu\$Config\gpu_test_math.exe",
    "tests\gpu\$Config\gpu_test_spectral_soa.exe",
    "tests\gpu\$Config\gpu_test_render.exe"
)

foreach ($exe in $executables) {
    $exePath = Join-Path $buildPath $exe
    Run-Step "Run $exe" {
        & $exePath
    }
}

if ($RenderVisual) {
    Run-Step "Render physics optics visual scene" {
        & (Join-Path $PSScriptRoot "render_physics_optics_visual.ps1") -BuildDir $BuildDir -Config $Config
    }
}

Run-Step "git diff --check" {
    git -C $repo diff --check
}

Write-Host "Physics optics targeted gate passed."
