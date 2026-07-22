param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$Scene = "cornell",
    [int]$Mode = 0,
    [int]$Width = 16,
    [int]$Height = 16,
    [int]$TotalSpp = 4096,
    [int]$ShardCount = 2,
    [string]$OutputPath = "output\benchmarks\phase_r_farm_evidence.json",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$ExePath = Join-Path $RepoRoot "$BuildDir\tests\gpu\gpu_phase_r_guiding_benchmark.exe"
$ResultDir = Join-Path $RepoRoot "output\benchmarks\phase_r_farm"
$ManifestPath = Join-Path $ResultDir "manifest.json"
$MergedPath = Join-Path $ResultDir "merged.bin"
$ReferencePath = Join-Path $ResultDir "reference.bin"
if ($Width -le 0 -or $Height -le 0 -or $TotalSpp -lt 4096 -or
    $ShardCount -lt 2 -or $ShardCount -gt $TotalSpp) {
    throw "farm dimensions, >=4096 SPP, and at least two shards are required"
}
if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir `
        -Config $Config -SkipConfigure -Targets gpu_phase_r_guiding_benchmark
    if ($LASTEXITCODE -ne 0) { throw "farm benchmark build failed" }
}
if (-not (Test-Path $ExePath)) { throw "farm benchmark executable is missing" }
New-Item -ItemType Directory -Force $ResultDir | Out-Null

$merged = New-Object 'double[]' ($Width * $Height * 3)
$shards = @()
$cursor = 0
for ($shardIndex = 0; $shardIndex -lt $ShardCount; ++$shardIndex) {
    $remaining = $TotalSpp - $cursor
    $remainingShards = $ShardCount - $shardIndex
    $count = [int][Math]::Floor($remaining / $remainingShards)
    $end = $cursor + $count
    $artifact = Join-Path $ResultDir "shard_${cursor}_${end}.bin"
    $elapsed = Measure-Command {
        & $ExePath $Scene $Mode $Width $Height $count $artifact $cursor
        if ($LASTEXITCODE -ne 0) { throw "farm shard $shardIndex failed" }
    }
    $bytes = [IO.File]::ReadAllBytes($artifact)
    if ([BitConverter]::ToInt32($bytes, 0) -ne $Width -or
        [BitConverter]::ToInt32($bytes, 4) -ne $Height -or
        $bytes.Length -ne 8 + $merged.Length * 4) {
        throw "farm shard $shardIndex has an invalid framebuffer payload"
    }
    for ($valueIndex = 0; $valueIndex -lt $merged.Length; ++$valueIndex) {
        $value = [BitConverter]::ToSingle($bytes, 8 + 4 * $valueIndex)
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) {
            throw "farm shard contains a non-finite value"
        }
        $merged[$valueIndex] += $value * $count / $TotalSpp
    }
    $shards += [ordered]@{
        worker_id = "local-gpu-$shardIndex"
        sample_begin = $cursor
        sample_end = $end
        artifact = [IO.Path]::GetRelativePath($ResultDir, $artifact)
        elapsed_seconds = [Math]::Round($elapsed.TotalSeconds, 6)
    }
    $cursor = $end
}
if ($cursor -ne $TotalSpp) { throw "farm shard partition did not cover the sample range" }
$stream = [IO.File]::Open($MergedPath, [IO.FileMode]::Create)
$writer = [IO.BinaryWriter]::new($stream)
try {
    $writer.Write($Width)
    $writer.Write($Height)
    foreach ($value in $merged) { $writer.Write([single]$value) }
} finally {
    $writer.Dispose()
    $stream.Dispose()
}
& $ExePath $Scene $Mode $Width $Height $TotalSpp $ReferencePath 0
if ($LASTEXITCODE -ne 0) { throw "farm merge reference render failed" }
$referenceBytes = [IO.File]::ReadAllBytes($ReferencePath)
$errorSquared = 0.0
$referenceSquared = 0.0
for ($valueIndex = 0; $valueIndex -lt $merged.Length; ++$valueIndex) {
    $reference = [BitConverter]::ToSingle(
        $referenceBytes, 8 + 4 * $valueIndex)
    $delta = $merged[$valueIndex] - $reference
    $errorSquared += $delta * $delta
    $referenceSquared += $reference * $reference
}
$mergeNormalizedMse = $errorSquared / [Math]::Max($referenceSquared, 1.0e-20)
if ($mergeNormalizedMse -gt 1.0e-6) {
    throw "farm merged framebuffer differs from the direct range: $mergeNormalizedMse"
}
$manifest = [ordered]@{
    schema = "ure.phase_r.farm_manifest.v1"
    run_id = "phase-r-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))"
    executable = $ExePath
    executable_sha256 = (Get-FileHash $ExePath -Algorithm SHA256).Hash.ToLowerInvariant()
    scene = $Scene
    mode = $Mode
    width = $Width
    height = $Height
    expected_sample_count = $TotalSpp
    merged_artifact = [IO.Path]::GetRelativePath($ResultDir, $MergedPath)
    reference_artifact = [IO.Path]::GetRelativePath($ResultDir, $ReferencePath)
    merge_normalized_mse = $mergeNormalizedMse
    shards = $shards
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ManifestPath -Encoding utf8
& (Join-Path $PSScriptRoot "build_phase_r_farm_evidence.ps1") `
    -ManifestPath $ManifestPath -OutputPath $OutputPath
if ($LASTEXITCODE -ne 0) { throw "farm evidence build failed" }
Write-Host "Phase R farm long-run passed: $OutputPath"
