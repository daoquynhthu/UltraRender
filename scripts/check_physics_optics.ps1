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

if ($RenderVisual) {
    $targets += "ure_cli"
}

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
    $scenePath = Join-Path $repo "scenes\textured_quad_validation.gltf"
    $outputPath = Join-Path $repo "output\physics_optics_visual_gltf.hdr"
    New-Item -ItemType Directory -Force -Path (Split-Path $outputPath) | Out-Null
    Run-Step "Render glTF optics visual smoke" {
        & (Join-Path $buildPath "apps\ure_cli\$Config\ure_cli.exe") render $scenePath --spp 1 --width 8 --height 8 --format hdr --output (Split-Path $outputPath -Leaf)
    }
    if (-not (Test-Path $outputPath)) {
        throw "glTF visual smoke did not produce $outputPath"
    }
}

Run-Step "git diff --check" {
    git -C $repo diff --check
}

Write-Host "Physics optics targeted gate passed."
