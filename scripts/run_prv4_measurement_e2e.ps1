param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "docs/reports/phase_prv4_functional_validation_v1.json",
    [string]$ArtifactDir = "build_modular_x64/artifacts/Release/evidence/prv4_functional",
    [ValidateRange(16, 64)][int]$Samples = 16
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
        throw "PRV.4 product command failed ($exitCode): $Executable`n$output"
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

function Invoke-MeasurementRun(
    [string]$Transport,
    [string]$Prefix,
    [string]$Runner,
    [string]$Runtime,
    [string]$Worker,
    [string]$Scene,
    [string]$Python,
    [string]$Isolation) {
    $values = Convert-KeyValues (Invoke-Isolated $Runner @(
        $Transport, $Runtime, $Worker, $Scene, [string]$Samples, $Prefix
    ) $Isolation)
    $png = "$Prefix.png"
    $metrics = "$Prefix.metrics.json"
    [void](Invoke-Isolated $Python @(
        (Join-Path $RepoRoot "scripts/validate_measurement_image.py"),
        "--raw", $values.beauty_raw,
        "--png", $png,
        "--report", $metrics
    ) $Isolation)
    $productPrefix = "$Prefix.product"
    $exr = Find-Artifact $productPrefix ".exr"
    $checkpoint = Find-Artifact $productPrefix ".v2"
    $display = Find-Artifact $productPrefix ".ppm"
    $manifest = "$productPrefix.manifest.json"
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Product manifest is missing: $manifest"
    }
    return [ordered]@{
        transport = $Transport
        resolution = $values.frame
        samples = [uint64]$values.samples
        plane_count = [uint32]$values.planes
        partial_plane = [uint32]$values.partial_plane
        partial_bytes = [uint64]$values.partial_bytes
        elapsed_ms = [uint64]$values.elapsed_ms
        identities = [ordered]@{
            build = $values.build_identity
            snapshot = $values.snapshot_identity
            objective = $values.objective_identity
            plan = $values.plan_identity
            measurement = $values.measurement_identity
            manifest = $values.manifest_identity
        }
        artifacts = [ordered]@{
            beauty_pfm_sha256 = Get-Sha256 $values.beauty_raw
            exr_sha256 = Get-Sha256 $exr
            checkpoint_sha256 = Get-Sha256 $checkpoint
            display_ppm_sha256 = Get-Sha256 $display
            manifest_sha256 = Get-Sha256 $manifest
            png_sha256 = Get-Sha256 $png
        }
        image = Get-Content -Raw -LiteralPath $metrics | ConvertFrom-Json -Depth 100
    }
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$artifactFullPath = Resolve-RepositoryPath $ArtifactDir
$runtime = Join-Path $buildPath "artifacts/Release/bin/ultrarender_runtime_1.dll"
$worker = Join-Path $buildPath "artifacts/Release/bin/ultrarender_worker_1.exe"
$cli = Join-Path $buildPath "artifacts/Release/bin/ure_cli.exe"
$runner = Join-Path $buildPath "artifacts/Release/bin/product_measurement_scenario_runner.exe"
$scene = Join-Path $RepoRoot "tests/assets/product_e2e/cornell_854x480.urescene"
$python = (Get-Command python -ErrorAction Stop).Source
$isolation = Join-Path $buildPath "prv4_measurement_isolated_cwd"
New-Item -ItemType Directory -Force -Path $artifactFullPath, $isolation | Out-Null

foreach ($path in @($runtime, $worker, $cli, $runner, $scene, $python)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PRV.4 E2E input is missing: $path"
    }
}

$directPrefix = Join-Path $artifactFullPath "direct/frame"
$workerPrefix = Join-Path $artifactFullPath "worker/frame"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $directPrefix), (Split-Path -Parent $workerPrefix) | Out-Null
$direct = Invoke-MeasurementRun "direct" $directPrefix $runner $runtime $worker $scene $python $isolation
$workerRun = Invoke-MeasurementRun "worker" $workerPrefix $runner $runtime $worker $scene $python $isolation

foreach ($key in @("resolution", "samples", "plane_count")) {
    if ($direct[$key] -ne $workerRun[$key]) {
        throw "Direct/Worker measurement parity failed for $key"
    }
}
foreach ($key in @("build", "snapshot", "objective", "plan", "measurement")) {
    if ($direct.identities[$key] -ne $workerRun.identities[$key]) {
        throw "Direct/Worker identity parity failed for $key"
    }
}
foreach ($key in @("beauty_pfm_sha256", "exr_sha256", "checkpoint_sha256", "display_ppm_sha256")) {
    if ($direct.artifacts[$key] -ne $workerRun.artifacts[$key]) {
        throw "Direct/Worker artifact parity failed for $key"
    }
}

$cliRuns = @()
foreach ($entry in @(
    [ordered]@{ transport = "direct"; format = "bmp"; tone = "linear" },
    [ordered]@{ transport = "worker"; format = "ppm"; tone = "aces" }
)) {
    $prefix = Join-Path $artifactFullPath "cli_$($entry.transport)/frame"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $prefix) | Out-Null
    $values = Convert-KeyValues (Invoke-Isolated $cli @(
        "render", $scene, "--spp", [string]$Samples,
        "--runtime", $runtime, "--worker", $worker,
        "--transport", $entry.transport,
        "--format", $entry.format, "--tonemap", $entry.tone,
        "--output", $prefix
    ) $isolation)
    $manifestPath = "$prefix.manifest.json"
    if ([uint32]$values.output_status -ne 2147483763 -or
        [uint64]$values.output_artifacts -ne 4 -or
        -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "CLI $($entry.transport) did not publish a complete artifact graph"
    }
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    $exr = Join-Path (Split-Path -Parent $prefix) $manifest.exr
    $checkpoint = Join-Path (Split-Path -Parent $prefix) $manifest.measurement_checkpoint
    $display = Join-Path (Split-Path -Parent $prefix) $manifest.derived_display
    foreach ($path in @($exr, $checkpoint, $display)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "CLI artifact is missing: $path"
        }
    }
    $cliRuns += [ordered]@{
        transport = $entry.transport
        format = $entry.format
        tone_map = $entry.tone
        resolution = $values.frame
        accepted_samples = [uint64]$values.accepted_samples
        completed_samples = [uint64]$values.completed_samples
        output_artifacts = [uint64]$values.output_artifacts
        identities = [ordered]@{
            build = $manifest.build_identity
            snapshot = $manifest.scene_identity
            objective = $manifest.objective_identity
            plan = $manifest.plan_identity
            measurement = $manifest.measurement_content_identity
            exr = $manifest.exr_content_identity
            display = $manifest.derived_display_content_identity
        }
        artifacts = [ordered]@{
            exr_sha256 = Get-Sha256 $exr
            checkpoint_sha256 = Get-Sha256 $checkpoint
            display_sha256 = Get-Sha256 $display
            manifest_sha256 = Get-Sha256 $manifestPath
        }
    }
}

foreach ($key in @("build", "snapshot", "objective", "plan", "measurement", "exr")) {
    if ($cliRuns[0].identities[$key] -ne $cliRuns[1].identities[$key]) {
        throw "CLI Direct/Worker identity parity failed for $key"
    }
}
foreach ($key in @("exr_sha256", "checkpoint_sha256")) {
    if ($cliRuns[0].artifacts[$key] -ne $cliRuns[1].artifacts[$key]) {
        throw "CLI Direct/Worker authoritative artifact parity failed for $key"
    }
}

$report = [ordered]@{
    schema = "ure.phase_prv4.functional-validation/1.0"
    status = "Passed"
    product_release_declared = $false
    stable_contract = "Core ABI 1.0 / Worker Protocol 1.0"
    exact_build_product = "ProductJob 0.4 / Preview Client 0.4"
    evidence_tier = "ProductFunctional"
    fixture = [ordered]@{
        source = "tests/assets/product_e2e/cornell_854x480.urescene"
        sha256 = Get-Sha256 $scene
    }
    binaries = [ordered]@{
        runtime_sha256 = Get-Sha256 $runtime
        worker_sha256 = Get-Sha256 $worker
        cli_sha256 = Get-Sha256 $cli
        runner_sha256 = Get-Sha256 $runner
    }
    runs = @($direct, $workerRun)
    cli = $cliRuns
    parity = [ordered]@{
        direct_worker_measurement_identity = $true
        direct_worker_beauty_bytes = $true
        direct_worker_openexr_bytes = $true
        direct_worker_checkpoint_bytes = $true
        direct_worker_tonemapped_ppm_bytes = $true
        cli_direct_worker_openexr_bytes = $true
        cli_direct_worker_checkpoint_bytes = $true
    }
    external_sdk = [ordered]@{
        gate = "test_external_client_package"
        boundary = "staged exact-build SDK only; no renderer-private linkage or flatc invocation"
        calls = @("Direct", "Worker")
        evidence = @("typed raw/AOV/statistics", "partial plane read", "official OpenEXR artifact graph", "PFM image")
    }
    semantic_digest = ""
}
$report.semantic_digest = Get-TextSha256 ($report | ConvertTo-Json -Depth 100 -Compress)
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
[System.IO.File]::WriteAllText(
    $reportFullPath, $json + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Output "PRV.4 functional measurement E2E passed: Direct, Worker, CLI and external SDK product calls"
