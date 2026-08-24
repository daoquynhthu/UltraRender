param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "build_modular_x64/artifacts/Release/evidence/prv2_package/report.json",
    [string]$ArtifactDir = "build_modular_x64/artifacts/Release/evidence/prv2_package",
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

function Invoke-Isolated(
    [string]$Executable,
    [string[]]$Arguments,
    [string]$WorkingDirectory) {
    Push-Location $WorkingDirectory
    try {
        $text = & $Executable @Arguments 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        throw "Product package E2E command failed ($exitCode): $Executable $($Arguments -join ' ')`n$text"
    }
    return $text
}

function Convert-KeyValues([string]$Text) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Text, '(?m)^(?<key>[a-z_]+)=(?<value>.*)\r?$')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value.Trim()
    }
    return $values
}

function Invoke-SceneTool(
    [string]$Cli,
    [string]$Runtime,
    [string]$Worker,
    [string]$Transport,
    [string[]]$Arguments,
    [string]$WorkingDirectory) {
    $text = Invoke-Isolated $Cli ($Arguments + @(
        "--transport", $Transport,
        "--runtime", $Runtime,
        "--worker", $Worker)) $WorkingDirectory
    return $text | ConvertFrom-Json -Depth 100
}

function Assert-JsonParity([object]$Direct, [object]$WorkerResult, [string]$Name) {
    $left = $Direct | ConvertTo-Json -Depth 100 -Compress
    $right = $WorkerResult | ConvertTo-Json -Depth 100 -Compress
    if ($left -ne $right -or -not $Direct.ok) {
        throw "Product package scene-tool parity failed: $Name"
    }
}

function Invoke-ImageValidation(
    [string]$Python,
    [System.Collections.IDictionary]$Run,
    [string]$Prefix,
    [string]$ArtifactDirectory,
    [string]$WorkingDirectory) {
    $png = Join-Path $ArtifactDirectory "$Prefix.png"
    $metrics = Join-Path $ArtifactDirectory "$Prefix.metrics.json"
    [void](Invoke-Isolated $Python @(
        (Join-Path $RepoRoot "scripts/validate_product_image.py"),
        "--low", $Run.low_raw,
        "--mid", $Run.mid_raw,
        "--final", $Run.final_raw,
        "--png", $png,
        "--report", $metrics) $WorkingDirectory)
    return Get-Content -Raw -LiteralPath $metrics | ConvertFrom-Json -Depth 100
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$artifactFullPath = Resolve-RepositoryPath $ArtifactDir
$runtime = Join-Path $buildPath "artifacts/Release/bin/ultrarender_runtime_1.dll"
$worker = Join-Path $buildPath "artifacts/Release/bin/ultrarender_worker_1.exe"
$cli = Join-Path $buildPath "artifacts/Release/bin/ure_cli.exe"
$runner = Join-Path $buildPath "artifacts/Release/bin/product_scenario_runner.exe"
$python = (Get-Command python -ErrorAction Stop).Source
$fixture = Join-Path $RepoRoot "tests/assets/native_scene/q4_procedural_scene"
$work = Join-Path $buildPath "prv2_package_e2e_work"
$author = Join-Path $work "author"
$isolation = Join-Path $work "isolated"
$directPackage = Join-Path $artifactFullPath "procedural_direct.urepkg"
$workerPackage = Join-Path $artifactFullPath "procedural_worker.urepkg"

foreach ($path in @($runtime, $worker, $cli, $runner, $python)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Product package E2E input is missing: $path"
    }
}
if (-not (Test-Path -LiteralPath $fixture -PathType Container)) {
    throw "Product package E2E fixture is missing: $fixture"
}

if (Test-Path -LiteralPath $work) {
    $resolvedWork = [System.IO.Path]::GetFullPath($work)
    if (-not $resolvedWork.StartsWith($buildPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Product package E2E work directory escaped the build tree"
    }
    Remove-Item -LiteralPath $resolvedWork -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $work, $isolation, $artifactFullPath | Out-Null
Copy-Item -LiteralPath $fixture -Destination $author -Recurse
$authorScene = Join-Path $author "procedural_scene.ure"
$document = Get-Content -Raw -LiteralPath $authorScene | ConvertFrom-Json -Depth 100
$document.scene_ir.width = 854
$document.scene_ir.height = 480
$document.scene_ir.spp = 0
$document.scene_ir.camera.position = @(3.0, 3.0, 4.0)
$document.scene_ir.camera.look_at = @(0.3, 0.4, 0.3)
$document.scene_ir.camera.focus_dist = 5.0
$document.scene_ir.camera.fov = 55.0
[System.IO.File]::WriteAllText(
    $authorScene,
    ($document | ConvertTo-Json -Depth 100) + "`n",
    [System.Text.UTF8Encoding]::new($false))

$packDirect = Invoke-SceneTool $cli $runtime $worker "direct" @(
    "pack", $authorScene, "-o", $directPackage) $isolation
$packWorker = Invoke-SceneTool $cli $runtime $worker "worker" @(
    "pack", $authorScene, "-o", $workerPackage) $isolation
Assert-JsonParity $packDirect $packWorker "pack"
if ((Get-Sha256 $directPackage) -ne (Get-Sha256 $workerPackage)) {
    throw "Direct and Worker package bytes differ"
}
Remove-Item -LiteralPath $author -Recurse -Force

$validateDirect = Invoke-SceneTool $cli $runtime $worker "direct" @(
    "validate", $directPackage) $isolation
$validateWorker = Invoke-SceneTool $cli $runtime $worker "worker" @(
    "validate", $directPackage) $isolation
Assert-JsonParity $validateDirect $validateWorker "validate after source deletion"
$realizeDirect = Invoke-SceneTool $cli $runtime $worker "direct" @(
    "realize", $directPackage) $isolation
$realizeWorker = Invoke-SceneTool $cli $runtime $worker "worker" @(
    "realize", $directPackage) $isolation
Assert-JsonParity $realizeDirect $realizeWorker "realize after source deletion"
if (($realizeDirect.feature_dispositions | Where-Object {
        $_.id -eq "ure.scene.procedural" -and $_.disposition -eq "Executed"
    }).Count -ne 1) {
    throw "Procedural package was not executed by the ProductSnapshot realizer"
}
$expectedOutputBytes = [uint64](854 * 480 * 3 * 4)
if ([uint64]$realizeDirect.budget.output_bytes -ne $expectedOutputBytes) {
    throw "Scene realization did not account for the complete output-memory budget"
}

$direct = Convert-KeyValues (Invoke-Isolated $runner @(
    "direct", $runtime, $worker, $directPackage, [string]$Samples,
    (Join-Path $artifactFullPath "direct")) $isolation)
$workerRun = Convert-KeyValues (Invoke-Isolated $runner @(
    "worker", $runtime, $worker, $directPackage, [string]$Samples,
    (Join-Path $artifactFullPath "worker")) $isolation)
$directImage = Invoke-ImageValidation $python $direct "direct" $artifactFullPath $isolation
$workerImage = Invoke-ImageValidation $python $workerRun "worker" $artifactFullPath $isolation
if ([double]$directImage.metrics.luminance_standard_deviation -lt 0.05 -or
    [double]$directImage.metrics.mean_spatial_gradient -lt 0.0005) {
    throw "Procedural package artifact lacks visible geometry and spatial lighting structure"
}

$cliCommon = @(
    "render", $directPackage, "--spp", [string]$Samples,
    "--runtime", $runtime, "--worker", $worker)
$cliWorker = Convert-KeyValues (Invoke-Isolated $cli $cliCommon $isolation)
$cliDirect = Convert-KeyValues (Invoke-Isolated $cli (
    $cliCommon + @("--transport", "direct")) $isolation)

foreach ($key in @(
    "accepted_samples", "completed_samples", "frame", "build_identity",
    "snapshot_identity", "objective_identity", "plan_identity",
    "frame_content_identity")) {
    if ([string]::IsNullOrWhiteSpace([string]$direct[$key]) -or
        $direct[$key] -ne $workerRun[$key] -or
        $direct[$key] -ne $cliDirect[$key] -or
        $direct[$key] -ne $cliWorker[$key]) {
        throw "Product package E2E parity failed for $key"
    }
}
if ($direct.frame -ne "854x480") {
    throw "Product package functional evidence is not 854x480"
}
if ($direct.snapshot_identity -ne $realizeDirect.snapshot_identity) {
    throw "Scene-tool realization and ProductJob used different ProductSnapshot identities"
}
if ($directImage.raw_authority.final_sha256 -ne $workerImage.raw_authority.final_sha256) {
    throw "Direct and Worker authoritative package render artifacts differ"
}

$report = [ordered]@{
    schema = "ure.preview.prv2-package-e2e-evidence/1.0"
    status = "Passed"
    tier = "ProductFunctional"
    product_release_declared = $false
    product_path = "ure_client -> Direct/Worker -> runtime Product extension -> ure_product -> ProductSnapshot -> immutable Frame/artifact"
    production_profile = "ProductJob 0.3 default automatic production profile"
    fixture = [ordered]@{
        id = "q4_procedural_854x480_package"
        source = "tests/assets/native_scene/q4_procedural_scene"
        source_scene_sha256 = Get-Sha256 (Join-Path $fixture "procedural_scene.ure")
        derived_resolution = "854x480"
        package_sha256 = Get-Sha256 $directPackage
        author_source_deleted_before_validation = $true
    }
    binaries = [ordered]@{
        runtime_sha256 = Get-Sha256 $runtime
        worker_sha256 = Get-Sha256 $worker
        cli_sha256 = Get-Sha256 $cli
        scenario_runner_sha256 = Get-Sha256 $runner
    }
    scene_tool = [ordered]@{
        direct_worker_pack_byte_parity = $true
        validate_parity_after_source_deletion = $true
        realize_parity_after_source_deletion = $true
        snapshot_identity = $realizeDirect.snapshot_identity
        semantic_identity = $realizeDirect.semantic_identity
        resource_count = $realizeDirect.inventory.resources
        budget = $realizeDirect.budget
        dispositions = $realizeDirect.feature_dispositions
    }
    runs = @(
        [ordered]@{
            transport = "Direct"
            samples = [uint64]$direct.completed_samples
            resolution = $direct.frame
            elapsed_ms = [uint64]$direct.elapsed_ms
            build_identity = $direct.build_identity
            snapshot_identity = $direct.snapshot_identity
            objective_identity = $direct.objective_identity
            plan_identity = $direct.plan_identity
            frame_content_identity = $direct.frame_content_identity
            device = [ordered]@{
                identity = $direct.device_identity
                backend = [uint64]$direct.backend
                provider = [uint64]$direct.provider
                name = $direct.device_name
                adapter_id = $direct.adapter_id
                driver_identity = $direct.driver_identity
                compiler_identity = $direct.compiler_identity
                total_memory_bytes = [uint64]$direct.total_memory_bytes
                available_memory_bytes = [uint64]$direct.available_memory_bytes
                selected_memory_budget_bytes = [uint64]$direct.selected_memory_budget_bytes
            }
            raw_sha256 = $directImage.raw_authority.final_sha256
            png_sha256 = $directImage.derived_view.sha256
            metrics = $directImage.metrics
            convergence = $directImage.convergence
        },
        [ordered]@{
            transport = "Worker"
            samples = [uint64]$workerRun.completed_samples
            resolution = $workerRun.frame
            elapsed_ms = [uint64]$workerRun.elapsed_ms
            build_identity = $workerRun.build_identity
            snapshot_identity = $workerRun.snapshot_identity
            objective_identity = $workerRun.objective_identity
            plan_identity = $workerRun.plan_identity
            frame_content_identity = $workerRun.frame_content_identity
            device = [ordered]@{
                identity = $workerRun.device_identity
                backend = [uint64]$workerRun.backend
                provider = [uint64]$workerRun.provider
                name = $workerRun.device_name
                adapter_id = $workerRun.adapter_id
                driver_identity = $workerRun.driver_identity
                compiler_identity = $workerRun.compiler_identity
                total_memory_bytes = [uint64]$workerRun.total_memory_bytes
                available_memory_bytes = [uint64]$workerRun.available_memory_bytes
                selected_memory_budget_bytes = [uint64]$workerRun.selected_memory_budget_bytes
            }
            raw_sha256 = $workerImage.raw_authority.final_sha256
            png_sha256 = $workerImage.derived_view.sha256
            metrics = $workerImage.metrics
            convergence = $workerImage.convergence
        }
    )
    cli = @(
        [ordered]@{ transport = "Direct"; frame = $cliDirect.frame; completed_samples = [uint64]$cliDirect.completed_samples },
        [ordered]@{ transport = "Worker"; frame = $cliWorker.frame; completed_samples = [uint64]$cliWorker.completed_samples }
    )
    parity = [ordered]@{
        same_scene_tool_product_snapshot = $true
        same_build_identity = ($direct.build_identity -eq $workerRun.build_identity)
        same_plan_identity = ($direct.plan_identity -eq $workerRun.plan_identity)
        same_frame_content_identity = ($direct.frame_content_identity -eq $workerRun.frame_content_identity)
        same_device_identity = ($direct.device_identity -eq $workerRun.device_identity)
        byte_identical_raw_secondary_check = $true
    }
}
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
[System.IO.File]::WriteAllText(
    $reportFullPath, $json + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Output "PRV.2 package ProductE2E passed: self-contained procedural 854x480 Direct/Worker/CLI artifact matrix"
