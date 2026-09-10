param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "docs/reports/phase_prv4_quality_validation_v1.json",
    [string]$ArtifactDir = "build_modular_x64/artifacts/Release/evidence/prv4_quality",
    [ValidateRange(500, 512)][int]$Samples = 500
)

$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-TextSha256([string]$Text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    return [Convert]::ToHexString(
        [System.Security.Cryptography.SHA256]::HashData($bytes)
    ).ToLowerInvariant()
}

function Convert-KeyValues([string]$Text) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Text, '(?m)^(?<key>[a-z_]+)=(?<value>.*)\r?$')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value.Trim()
    }
    return $values
}

function Find-Artifact([string]$Prefix, [string]$Extension) {
    $matches = @(Get-ChildItem -LiteralPath (Split-Path -Parent $Prefix) -File |
        Where-Object {
            $_.Name.StartsWith((Split-Path -Leaf $Prefix) + ".") -and
            $_.Extension -eq $Extension
        })
    if ($matches.Count -ne 1) {
        throw "Expected one $Extension artifact for $Prefix, found $($matches.Count)"
    }
    return $matches[0].FullName
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$artifactFullPath = Resolve-RepositoryPath $ArtifactDir
$runtime = Join-Path $buildPath "artifacts/Release/bin/ultrarender_runtime_1.dll"
$worker = Join-Path $buildPath "artifacts/Release/bin/ultrarender_worker_1.exe"
$runner = Join-Path $buildPath "artifacts/Release/bin/product_measurement_scenario_runner.exe"
$scene = Join-Path $RepoRoot "tests/assets/product_e2e/cornell_1280x720.urescene"
$validator = Join-Path $RepoRoot "scripts/validate_measurement_image.py"
$python = (Get-Command python -ErrorAction Stop).Source
$isolation = Join-Path $buildPath "prv4_quality_isolated_cwd"
$prefix = Join-Path $artifactFullPath "cornell-1280x720-spp$Samples-direct"
New-Item -ItemType Directory -Force -Path $artifactFullPath, $isolation | Out-Null

foreach ($path in @($runtime, $worker, $runner, $scene, $validator, $python)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PRV.4 quality input is missing: $path"
    }
}

Push-Location $isolation
try {
    $output = & $runner direct $runtime $worker $scene ([string]$Samples) $prefix 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($exitCode -ne 0) {
    throw "PRV.4 quality ProductJob failed ($exitCode)`n$output"
}
$run = Convert-KeyValues $output
$png = "$prefix.png"
$metricsPath = "$prefix.metrics.json"
& $python $validator --raw $run.beauty_raw --png $png --report $metricsPath
if ($LASTEXITCODE -ne 0) {
    throw "PRV.4 quality image validation failed"
}
$image = Get-Content -Raw -LiteralPath $metricsPath | ConvertFrom-Json -Depth 100
$productPrefix = "$prefix.product"
$exr = Find-Artifact $productPrefix ".exr"
$checkpoint = Find-Artifact $productPrefix ".v2"
$display = Find-Artifact $productPrefix ".ppm"
$manifest = "$productPrefix.manifest.json"
if ($run.frame -ne "1280x720" -or [uint64]$run.samples -ne $Samples -or
    [uint32]$run.planes -lt 20 -or [uint64]$run.partial_bytes -ne 4096 -or
    -not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "PRV.4 quality result does not satisfy the typed measurement contract"
}

$report = [ordered]@{
    schema = "ure.phase_prv4.quality-validation/1.0"
    status = "AutomatedPassed"
    visual_review_required = $true
    product_release_declared = $false
    evidence_tier = "ProductQuality"
    fixture = [ordered]@{
        source = "tests/assets/product_e2e/cornell_1280x720.urescene"
        sha256 = Get-Sha256 $scene
    }
    run = [ordered]@{
        transport = "Direct"
        resolution = $run.frame
        requested_samples = [uint64]$run.samples
        completed_samples = [uint64]$run.samples
        plane_count = [uint32]$run.planes
        partial_plane = [uint32]$run.partial_plane
        partial_bytes = [uint64]$run.partial_bytes
        elapsed_ms = [uint64]$run.elapsed_ms
        identities = [ordered]@{
            build = $run.build_identity
            snapshot = $run.snapshot_identity
            objective = $run.objective_identity
            plan = $run.plan_identity
            measurement = $run.measurement_identity
            manifest = $run.manifest_identity
        }
    }
    artifacts = [ordered]@{
        beauty_pfm_sha256 = Get-Sha256 $run.beauty_raw
        openexr_sha256 = Get-Sha256 $exr
        checkpoint_sha256 = Get-Sha256 $checkpoint
        reinhard_ppm_sha256 = Get-Sha256 $display
        manifest_sha256 = Get-Sha256 $manifest
        png_file = [System.IO.Path]::GetRelativePath($artifactFullPath, $png).Replace('\', '/')
        png_sha256 = Get-Sha256 $png
    }
    image = $image
    boundary = "720p/500 spp is milestone quality evidence, not a normal CTest or universal performance threshold"
    semantic_digest = ""
}
$report.semantic_digest = Get-TextSha256 ($report | ConvertTo-Json -Depth 100 -Compress)
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
[System.IO.File]::WriteAllText(
    $reportFullPath, $json + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Output "PRV.4 quality evidence passed: 1280x720 at $Samples spp; visual review remains explicit"
