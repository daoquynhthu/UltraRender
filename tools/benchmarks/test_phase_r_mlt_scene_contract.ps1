param(
    [string]$BuildDir = "build_modular_x64"
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$exe = Join-Path $repo "$BuildDir\tests\gpu\gpu_phase_r_guiding_benchmark.exe"
$output = Join-Path $repo "output\benchmarks"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$baseline = Join-Path $output "phase_r_mlt_high_occlusion_contract.bin"
$rare = Join-Path $output "phase_r_mlt_high_occlusion_small_light_contract.bin"
& $exe high_occlusion 0 4 4 4 $baseline 200000 0
if ($LASTEXITCODE -ne 0) { throw "high-occlusion contract baseline failed" }
& $exe high_occlusion_small_light 0 4 4 4 $rare 200000 0
if ($LASTEXITCODE -ne 0) {
    throw "area-compensated high-occlusion small-light scene is unavailable"
}
$baselineHash = (Get-FileHash -LiteralPath $baseline -Algorithm SHA256).Hash
$rareHash = (Get-FileHash -LiteralPath $rare -Algorithm SHA256).Hash
if ($baselineHash -eq $rareHash) {
    throw "small-light workload does not change the rendered path distribution"
}
Write-Host "Phase R MLT area-compensated scene contract passed"
