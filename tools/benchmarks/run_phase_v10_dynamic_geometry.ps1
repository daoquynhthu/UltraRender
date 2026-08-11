param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$OutputPath =
        "output/benchmarks/phase_v10_dynamic_geometry.json",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ArtifactBin = Join-Path $BuildPath "artifacts\$Config\bin"

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") `
        -BuildDir $BuildDir `
        -Config $Config `
        -SkipConfigure `
        -Targets "gpu_test_dynamic_geometry,test_dynamic_geometry"
    if ($LASTEXITCODE -ne 0) {
        throw "Phase V.10 dynamic geometry targets failed to build"
    }
}

$GpuTest = Join-Path $ArtifactBin "gpu_test_dynamic_geometry.exe"
$HostTest = Join-Path $ArtifactBin "test_dynamic_geometry.exe"
if (-not (Test-Path $GpuTest) -or
    -not (Test-Path $HostTest)) {
    throw "Phase V.10 dynamic geometry executables are missing"
}

$HostOutput = (& $HostTest 2>&1 |
    ForEach-Object { "$_" }) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Host dynamic geometry contract failed`n$HostOutput"
}
$GpuOutput = (& $GpuTest 2>&1 |
    ForEach-Object { "$_" }) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "GPU dynamic geometry contract failed`n$GpuOutput"
}

$Pattern = [regex]::new(
    'schema=ure\.phase_v\.dynamic_geometry\.v1 ' +
    'rigid_ms=([0-9.]+) deforming_ms=([0-9.]+) ' +
    'topology_ms=([0-9.]+) rigid_refit=(\d+) ' +
    'blas_rebuild=(\d+) topology_rebuild=(\d+) ' +
    'unsupported_refit=(\d+)')
$Match = $Pattern.Match($GpuOutput)
if (-not $Match.Success) {
    throw "Phase V.10 GPU output did not match the stable schema"
}

$RigidMs = [double]$Match.Groups[1].Value
$DeformingMs = [double]$Match.Groups[2].Value
$TopologyMs = [double]$Match.Groups[3].Value
$RigidRefit = [uint64]$Match.Groups[4].Value
$BlasRebuild = [uint64]$Match.Groups[5].Value
$TopologyRebuild = [uint64]$Match.Groups[6].Value
$UnsupportedRefit = [uint32]$Match.Groups[7].Value
if ($RigidMs -le 0.0 -or
    $DeformingMs -le 0.0 -or
    $TopologyMs -le 0.0 -or
    $RigidRefit -ne 1 -or
    $BlasRebuild -ne 2 -or
    $TopologyRebuild -ne 1 -or
    $UnsupportedRefit -ne 1) {
    throw "Phase V.10 dynamic geometry gate failed"
}

$Report = [ordered]@{
    schema = "ure.phase_v.dynamic_geometry.v1"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    build_dir = $BuildDir
    update_ms = [ordered]@{
        rigid = $RigidMs
        deforming = $DeformingMs
        topology_change = $TopologyMs
    }
    operations = [ordered]@{
        rigid_tlas_refit = $RigidRefit
        blas_rebuild = $BlasRebuild
        topology_rebuild = $TopologyRebuild
    }
    gates = [ordered]@{
        depth_aov_correctness = $true
        invalid_acceleration_zero = $true
        unsupported_refit_rejected = $true
    }
}

$ResolvedOutput = Join-Path $RepoRoot $OutputPath
$OutputDirectory = Split-Path -Parent $ResolvedOutput
New-Item -ItemType Directory -Force `
    -Path $OutputDirectory | Out-Null
$Report | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $ResolvedOutput -Encoding utf8
Write-Host "Phase V.10 dynamic geometry: $ResolvedOutput"
