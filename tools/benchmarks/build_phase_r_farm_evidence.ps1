param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,
    [string]$OutputPath = "output\benchmarks\phase_r_farm_evidence.json"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$resolvedManifest = Resolve-Path -LiteralPath $ManifestPath
$manifest = Get-Content -Raw -LiteralPath $resolvedManifest | ConvertFrom-Json
if ($manifest.schema -ne "ure.phase_r.farm_manifest.v1") { throw "invalid Phase R farm manifest schema" }
if ([string]::IsNullOrWhiteSpace($manifest.run_id)) { throw "farm run_id is missing" }
if ([uint64]$manifest.expected_sample_count -lt 4096 -or
    @($manifest.shards).Count -lt 2 -or
    [int]$manifest.width -le 0 -or [int]$manifest.height -le 0 -or
    $manifest.executable_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw "farm manifest does not satisfy the long-run contract"
}
$resolvedExecutable = Resolve-Path -LiteralPath $manifest.executable
if ((Get-FileHash $resolvedExecutable -Algorithm SHA256).Hash -ne
    $manifest.executable_sha256) {
    throw "farm executable hash mismatch"
}

$shards = @()
foreach ($entry in $manifest.shards) {
    $artifactPath = if ([System.IO.Path]::IsPathRooted($entry.artifact)) {
        $entry.artifact
    } else {
        Join-Path (Split-Path $resolvedManifest) $entry.artifact
    }
    $artifact = Resolve-Path -LiteralPath $artifactPath
    $shards += [ordered]@{
        worker_id = [string]$entry.worker_id
        sample_begin = [uint64]$entry.sample_begin
        sample_end = [uint64]$entry.sample_end
        artifact = $artifact.Path
        artifact_sha256 = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
        elapsed_seconds = [double]$entry.elapsed_seconds
    }
}
$mergedPath = if ([System.IO.Path]::IsPathRooted($manifest.merged_artifact)) {
    $manifest.merged_artifact
} else {
    Join-Path (Split-Path $resolvedManifest) $manifest.merged_artifact
}
$merged = Resolve-Path -LiteralPath $mergedPath
$referencePath = if ([System.IO.Path]::IsPathRooted($manifest.reference_artifact)) {
    $manifest.reference_artifact
} else {
    Join-Path (Split-Path $resolvedManifest) $manifest.reference_artifact
}
$reference = Resolve-Path -LiteralPath $referencePath
if ([double]$manifest.merge_normalized_mse -lt 0.0 -or
    [double]$manifest.merge_normalized_mse -gt 1.0e-6) {
    throw "farm merged framebuffer validation failed"
}

$destination = if ([System.IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $RepoRoot $OutputPath }
New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
$report = [ordered]@{
    schema = "ure.phase_r.farm_evidence.v1"
    status = "collected"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    run_id = [string]$manifest.run_id
    expected_sample_count = [uint64]$manifest.expected_sample_count
    scene = [string]$manifest.scene
    mode = [int]$manifest.mode
    width = [int]$manifest.width
    height = [int]$manifest.height
    executable_sha256 = $manifest.executable_sha256.ToLowerInvariant()
    manifest_sha256 = (Get-FileHash -LiteralPath $resolvedManifest -Algorithm SHA256).Hash.ToLowerInvariant()
    merged_artifact = $merged.Path
    merged_artifact_sha256 = (Get-FileHash -LiteralPath $merged -Algorithm SHA256).Hash.ToLowerInvariant()
    reference_artifact = $reference.Path
    reference_artifact_sha256 = (Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash.ToLowerInvariant()
    merge_normalized_mse = [double]$manifest.merge_normalized_mse
    shards = $shards
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $destination -Encoding utf8
Write-Host "Wrote $destination"
