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
if ([uint64]$manifest.expected_sample_count -eq 0) { throw "expected_sample_count must be positive" }

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
    }
}

$destination = if ([System.IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $RepoRoot $OutputPath }
New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
$report = [ordered]@{
    schema = "ure.phase_r.farm_evidence.v1"
    status = "collected"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    run_id = [string]$manifest.run_id
    expected_sample_count = [uint64]$manifest.expected_sample_count
    manifest_sha256 = (Get-FileHash -LiteralPath $resolvedManifest -Algorithm SHA256).Hash.ToLowerInvariant()
    shards = $shards
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $destination -Encoding utf8
Write-Host "Wrote $destination"
