param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "docs/reports/phase_prv1r_quality_validation_v1.json",
    [string]$ArtifactDir = "build_modular_x64/artifacts/Release/evidence/prv1r_quality",
    [ValidateRange(128, 512)][int]$Samples = 128
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

function Invoke-Isolated([string]$Executable, [string[]]$Arguments, [string]$WorkingDirectory) {
    Push-Location $WorkingDirectory
    try {
        $output = & $Executable @Arguments 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        throw "Product quality command failed ($exitCode): $Executable`n$output"
    }
    return $output
}

function Convert-KeyValues([string]$Text) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Text, '(?m)^(?<key>[a-z_]+)=(?<value>.*)\r?$')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value.Trim()
    }
    return $values
}

function Invoke-QualityRun(
    [string]$Id,
    [string]$Transport,
    [string]$Scene,
    [int]$SampleCount,
    [string]$Runner,
    [string]$Runtime,
    [string]$Worker,
    [string]$Python,
    [string]$ArtifactDirectory,
    [string]$WorkingDirectory) {
    $prefix = Join-Path $ArtifactDirectory $Id
    $run = Convert-KeyValues (Invoke-Isolated $Runner @(
        $Transport, $Runtime, $Worker, $Scene, [string]$SampleCount, $prefix
    ) $WorkingDirectory)
    $png = "$prefix.png"
    $metricsPath = "$prefix.metrics.json"
    [void](Invoke-Isolated $Python @(
        (Join-Path $RepoRoot "scripts/validate_product_image.py"),
        "--low", $run.low_raw,
        "--mid", $run.mid_raw,
        "--final", $run.final_raw,
        "--png", $png,
        "--report", $metricsPath
    ) $WorkingDirectory)
    $image = Get-Content -Raw -LiteralPath $metricsPath | ConvertFrom-Json -Depth 100
    return [ordered]@{
        id = $Id
        transport = $Transport
        scene = [System.IO.Path]::GetRelativePath($RepoRoot, $Scene).Replace('\', '/')
        scene_sha256 = Get-Sha256 $Scene
        requested_samples = [uint64]$run.requested_samples
        accepted_samples = [uint64]$run.accepted_samples
        completed_samples = [uint64]$run.completed_samples
        low_samples = [uint64]$run.low_samples
        mid_samples = [uint64]$run.mid_samples
        resolution = $run.frame
        elapsed_ms = [uint64]$run.elapsed_ms
        identities = [ordered]@{
            build = $run.build_identity
            snapshot = $run.snapshot_identity
            objective = $run.objective_identity
            plan = $run.plan_identity
            frame_content = $run.frame_content_identity
        }
        device = [ordered]@{
            identity = $run.device_identity
            backend = [uint32]$run.backend
            provider = [uint32]$run.provider
            name = $run.device_name
            adapter_id = $run.adapter_id
            driver_identity = $run.driver_identity
            compiler_identity = $run.compiler_identity
            total_memory_bytes = [uint64]$run.total_memory_bytes
            available_memory_bytes = [uint64]$run.available_memory_bytes
            selected_memory_budget_bytes = [uint64]$run.selected_memory_budget_bytes
        }
        raw_authority = [ordered]@{
            format = $image.raw_authority.format
            orientation = $image.raw_authority.orientation
            color_space = $image.raw_authority.color_space
            low_sha256 = $image.raw_authority.low_sha256
            mid_sha256 = $image.raw_authority.mid_sha256
            final_sha256 = $image.raw_authority.final_sha256
        }
        derived_png = [ordered]@{
            file = "$Id.png"
            sha256 = $image.derived_view.sha256
            view_transform = $image.derived_view.view_transform
        }
        metrics = $image.metrics
        convergence = $image.convergence
    }
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$artifactFullPath = Resolve-RepositoryPath $ArtifactDir
$runtime = Join-Path $buildPath "artifacts/Release/bin/ultrarender_runtime_1.dll"
$worker = Join-Path $buildPath "artifacts/Release/bin/ultrarender_worker_1.exe"
$runner = Join-Path $buildPath "artifacts/Release/bin/product_scenario_runner.exe"
$python = (Get-Command python -ErrorAction Stop).Source
$isolation = Join-Path $buildPath "product_quality_isolated_cwd"
New-Item -ItemType Directory -Force -Path $artifactFullPath, $isolation | Out-Null

$scenes = @(
    Join-Path $RepoRoot "tests/assets/product_e2e/cornell_1280x720.urescene"
    Join-Path $RepoRoot "tests/assets/product_e2e/cornell_1920x1080.urescene"
)
foreach ($path in @($runtime, $worker, $runner, $python) + $scenes) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Product quality input is missing: $path"
    }
}

$runs = @(
    Invoke-QualityRun "cornell_1280x720_spp$Samples" "direct" $scenes[0] $Samples $runner $runtime $worker $python $artifactFullPath $isolation
    Invoke-QualityRun "cornell_1920x1080_spp$Samples" "worker" $scenes[1] $Samples $runner $runtime $worker $python $artifactFullPath $isolation
)
if ($runs[0].device.identity -ne $runs[1].device.identity) {
    throw "Product quality runs selected different execution devices"
}

$report = [ordered]@{
    schema = "ure.phase_prv1r.quality-validation/1.0"
    status = "AutomatedPassed"
    product_release_declared = $false
    evidence_tier = "ProductQuality"
    production_profile = "ProductJob 0.3 default automatic production profile"
    artifact_retention = "Authoritative raw PFM and derived PNG are retained in the configured local evidence directory; this report binds their hashes."
    source = [ordered]@{
        source_gltf = "scenes/cornell_box.gltf"
        source_gltf_sha256 = Get-Sha256 (Join-Path $RepoRoot "scenes/cornell_box.gltf")
        runtime_sha256 = Get-Sha256 $runtime
        worker_sha256 = Get-Sha256 $worker
        scenario_runner_sha256 = Get-Sha256 $runner
        image_validator_sha256 = Get-Sha256 (Join-Path $RepoRoot "scripts/validate_product_image.py")
    }
    runs = $runs
    visual_review = [ordered]@{
        required = $true
        record = "docs/reports/phase_prv1r_visual_review_v1.json"
    }
    semantic_digest = ""
}
$report.semantic_digest = Get-TextSha256 (
    $report | ConvertTo-Json -Depth 100 -Compress)
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
[System.IO.File]::WriteAllText(
    $reportFullPath, $json + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Output "Product quality evidence passed: 1280x720 and 1920x1080 at $Samples spp; visual review remains explicit"
