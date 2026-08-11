param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$exe = Join-Path $repo "$BuildDir\artifacts\$Config\bin\gpu_phase_r_guiding_benchmark.exe"
$output = Join-Path $repo "output\benchmarks"
New-Item -ItemType Directory -Path $output -Force | Out-Null

function Invoke-MltReplicate {
    param([int]$IdentityOffset, [string]$Name)
    $path = Join-Path $output $Name
    & $exe sds 4 4 4 8 $path 0 $IdentityOffset
    if ($LASTEXITCODE -ne 0) {
        throw "MLT replicate identity offset was rejected: $IdentityOffset"
    }
    $telemetry = @{}
    foreach ($line in Get-Content -LiteralPath ($path + ".telemetry")) {
        $parts = $line -split "=", 2
        if ($parts.Count -eq 2) { $telemetry[$parts[0]] = $parts[1] }
    }
    [ordered]@{
        hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        bootstrap_batches = [int]$telemetry.mlt_bootstrap_batches
    }
}

$first = Invoke-MltReplicate 0 "phase_r_mlt_contract_first.bin"
$second = Invoke-MltReplicate 16 "phase_r_mlt_contract_second.bin"
$repeat = Invoke-MltReplicate 0 "phase_r_mlt_contract_repeat.bin"
if ($first.hash -ne $repeat.hash) {
    throw "equal MLT replicate identities are not deterministic"
}
if ($first.hash -eq $second.hash) {
    throw "distinct MLT replicate identities produced the same sample stream"
}
if ($first.bootstrap_batches -le 0 -or $first.bootstrap_batches -gt 4) {
    throw "MLT bootstrap was not evaluated in bounded queue-sized batches"
}
Write-Host "Phase R MLT replicate identity contract passed"
