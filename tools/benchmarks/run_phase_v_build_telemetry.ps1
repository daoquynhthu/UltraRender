param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$OutputPath = "output/benchmarks/phase_v_build_telemetry.json",
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
        -Targets "gpu_test_acceleration_contract,test_session"
    if ($LASTEXITCODE -ne 0) {
        throw "Phase V telemetry targets failed to build"
    }
}

$GpuTest = Join-Path $ArtifactBin "gpu_test_acceleration_contract.exe"
$SessionTest = Join-Path $ArtifactBin "test_session.exe"
if (-not (Test-Path $GpuTest) -or -not (Test-Path $SessionTest)) {
    throw "Phase V telemetry executables are missing"
}

$GpuOutput = (& $GpuTest 2>&1 | ForEach-Object { "$_" }) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "GPU acceleration contract failed`n$GpuOutput"
}
$SessionOutput = (& $SessionTest 2>&1 | ForEach-Object { "$_" }) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Session acceleration contract failed`n$SessionOutput"
}

$LargePattern = [regex]::new(
    'V\.4 large mesh: triangles=(\d+) rays=(\d+) ' +
    'build_ms=\[([0-9.]+),([0-9.]+),([0-9.]+)\] ' +
    'trace_ms=\[([0-9.]+),([0-9.]+),([0-9.]+)\] ' +
    'node_bytes=\[(\d+),(\d+),(\d+)\] ' +
    'node_visits=\[(\d+),(\d+),(\d+)\] ' +
    'triangle_tests=\[(\d+),(\d+),(\d+)\] ' +
    'sbvh_stress_splits=(\d+) vram_bytes=(\d+)')
$AsyncPattern = [regex]::new(
    'V\.5 async build: build_ms=([0-9.]+) upload_ms=([0-9.]+) temporary_bytes=(\d+) uncompacted_bytes=(\d+) compacted_bytes=(\d+) upload_bytes=(\d+) concurrency=(\d+)')
$Large = $LargePattern.Match($GpuOutput)
$Async = $AsyncPattern.Match($SessionOutput)
if (-not $Large.Success -or -not $Async.Success) {
    throw "Phase V telemetry output did not match the stable schema"
}

$RayCount = [uint64]$Large.Groups[2].Value
$TraceMs = @(
    [double]$Large.Groups[6].Value,
    [double]$Large.Groups[7].Value,
    [double]$Large.Groups[8].Value)
$TraceMraysPerSecond = @(
    $TraceMs | ForEach-Object {
        if ($_ -le 0.0) {
            throw "Phase V telemetry reported non-positive trace time"
        }
        [Math]::Round($RayCount / $_ / 1000.0, 6)
    })
$NodeVisits = @(
    [uint64]$Large.Groups[12].Value,
    [uint64]$Large.Groups[13].Value,
    [uint64]$Large.Groups[14].Value)
$TriangleTests = @(
    [uint64]$Large.Groups[15].Value,
    [uint64]$Large.Groups[16].Value,
    [uint64]$Large.Groups[17].Value)
if ($RayCount -eq 0 -or
    $NodeVisits[1] -ge $NodeVisits[0] -or
    $NodeVisits[2] -ge $NodeVisits[0] -or
    [uint64]$Large.Groups[18].Value -eq 0 -or
    [uint64]$Large.Groups[19].Value -eq 0) {
    throw "Phase V dense geometry telemetry gate failed"
}

$Report = [ordered]@{
    schema = "ure.phase_v.build_telemetry.v1"
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString("o")
    build_dir = $BuildDir
    large_mesh = [ordered]@{
        triangle_count = [uint64]$Large.Groups[1].Value
        ray_count = $RayCount
        build_ms = @(
            [double]$Large.Groups[3].Value,
            [double]$Large.Groups[4].Value,
            [double]$Large.Groups[5].Value)
        trace_ms = $TraceMs
        trace_mrays_per_second = $TraceMraysPerSecond
        compact_bytes = @(
            [uint64]$Large.Groups[9].Value,
            [uint64]$Large.Groups[10].Value,
            [uint64]$Large.Groups[11].Value)
        node_visits = $NodeVisits
        triangle_tests = $TriangleTests
        spatial_split_count =
            [uint64]$Large.Groups[18].Value
        benchmark_vram_bytes =
            [uint64]$Large.Groups[19].Value
    }
    async_pipeline = [ordered]@{
        build_wall_ms = [double]$Async.Groups[1].Value
        upload_ms = [double]$Async.Groups[2].Value
        temporary_bytes_peak = [uint64]$Async.Groups[3].Value
        uncompacted_bytes = [uint64]$Async.Groups[4].Value
        compacted_bytes = [uint64]$Async.Groups[5].Value
        upload_bytes = [uint64]$Async.Groups[6].Value
        build_peak_concurrency = [uint32]$Async.Groups[7].Value
    }
    gates = [ordered]@{
        reference_hit_parity = $true
        traversal_failure_is_loud = $true
        scratch_budget_rejection = $true
    }
}

$ResolvedOutput = Join-Path $RepoRoot $OutputPath
$OutputDirectory = Split-Path -Parent $ResolvedOutput
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$Report | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $ResolvedOutput -Encoding utf8
Write-Host "Phase V build telemetry: $ResolvedOutput"
