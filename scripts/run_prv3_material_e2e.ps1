param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "docs/reports/phase_prv3_functional_validation_v1.json",
    [string]$ArtifactDir = "build_modular_x64/artifacts/Release/evidence/prv3_functional",
    [ValidateRange(4, 64)][int]$Samples = 16
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
        throw "PRV.3 ProductE2E command failed ($exitCode): $Executable $($Arguments -join ' ')`n$text"
    }
    return $text
}

function Convert-KeyValues([string]$Text) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches(
        $Text, '(?m)^(?<key>[a-z_]+)=(?<value>.*)\r?$')) {
        $values[$match.Groups['key'].Value] =
            $match.Groups['value'].Value.Trim()
    }
    return $values
}

function Invoke-Render(
    [string]$Transport,
    [string]$Scene,
    [string]$Name,
    [uint64]$ExpectedEligible,
    [string]$ExpectedResolution = "854x480",
    [uint64]$WorkSamples = $Samples) {
    $prefix = Join-Path $artifactFullPath "$Name-$Transport"
    $run = Convert-KeyValues (Invoke-Isolated $runner @(
        $Transport, $runtime, $worker, $Scene, [string]$WorkSamples, $prefix
    ) $isolation)
    foreach ($key in @(
        "accepted_samples", "completed_samples", "frame",
        "eligible_integrator_modes", "qualified_integrator_modes",
        "executed_integrator_modes", "plan_identity",
        "frame_content_identity", "device_identity", "driver_identity",
        "compiler_identity", "adapter_id", "low_raw", "mid_raw",
        "final_raw")) {
        if ([string]::IsNullOrWhiteSpace([string]$run[$key])) {
            throw "PRV.3 $Name/$Transport omitted $key"
        }
    }
    $eligible = [uint64]$run.eligible_integrator_modes
    $qualified = [uint64]$run.qualified_integrator_modes
    $executed = [uint64]$run.executed_integrator_modes
    if ($run.frame -ne $ExpectedResolution -or
        [uint64]$run.accepted_samples -ne $WorkSamples -or
        [uint64]$run.completed_samples -ne $WorkSamples -or
        ($ExpectedEligible -ne 0 -and $eligible -ne $ExpectedEligible) -or
        $qualified -eq 0 -or
        $executed -eq 0 -or ($qualified -band (-bnot $eligible)) -ne 0 -or
        ($executed -band (-bnot $qualified)) -ne 0) {
        throw "PRV.3 $Name/$Transport has invalid work or integrator evidence"
    }
    $png = "$prefix.png"
    $metrics = "$prefix.metrics.json"
    [void](Invoke-Isolated $python @(
        $validator, "--low", $run.low_raw, "--mid", $run.mid_raw,
        "--final", $run.final_raw, "--png", $png, "--report", $metrics
    ) $isolation)
    return [ordered]@{
        transport = $Transport
        scene = $Name
        requested_samples = $WorkSamples
        accepted_samples = [uint64]$run.accepted_samples
        completed_samples = [uint64]$run.completed_samples
        resolution = $run.frame
        eligible_integrator_modes = $eligible
        qualified_integrator_modes = $qualified
        executed_integrator_modes = $executed
        plan_identity = $run.plan_identity
        frame_content_identity = $run.frame_content_identity
        device_identity = $run.device_identity
        driver_identity = $run.driver_identity
        compiler_identity = $run.compiler_identity
        adapter_id = $run.adapter_id
        raw_sha256 = Get-Sha256 $run.final_raw
        png_sha256 = Get-Sha256 $png
        metrics = Get-Content -Raw -LiteralPath $metrics |
            ConvertFrom-Json -Depth 100
    }
}

function Invoke-SceneTool([string[]]$Arguments, [string]$Transport) {
    $text = Invoke-Isolated $cli ($Arguments + @(
        "--transport", $Transport, "--runtime", $runtime,
        "--worker", $worker)) $isolation
    $report = $text | ConvertFrom-Json -Depth 100
    if (-not $report.ok -or
        $report.material_program_set_identity -notmatch '^[0-9a-f]{64}$') {
        throw "PRV.3 authoring operation did not publish canonical material evidence"
    }
    return $report
}

function Compare-Transport(
    [string]$Name,
    [uint64]$WorkSamples) {
    $path = Join-Path $artifactFullPath "$Name-transport-parity.json"
    [void](Invoke-Isolated $python @(
        $comparator,
        "--candidate", (Join-Path $artifactFullPath "$Name-worker.spp$WorkSamples.pfm"),
        "--reference", (Join-Path $artifactFullPath "$Name-direct.spp$WorkSamples.pfm"),
        "--max-relative-rmse", "0.005", "--report", $path
    ) $isolation)
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -Depth 100
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$artifactFullPath = Resolve-RepositoryPath $ArtifactDir
$fixtureRoot = Join-Path $RepoRoot "tests/assets/product_e2e/prv3"
$runtime = Join-Path $buildPath "artifacts/Release/bin/ultrarender_runtime_1.dll"
$worker = Join-Path $buildPath "artifacts/Release/bin/ultrarender_worker_1.exe"
$cli = Join-Path $buildPath "artifacts/Release/bin/ure_cli.exe"
$runner = Join-Path $buildPath "artifacts/Release/bin/product_scenario_runner.exe"
$validator = Join-Path $RepoRoot "scripts/validate_product_image.py"
$comparator = Join-Path $RepoRoot "scripts/compare_product_images.py"
$orchestrator = [System.IO.Path]::GetFullPath($PSCommandPath)
$python = (Get-Command python -ErrorAction Stop).Source
$isolation = Join-Path $buildPath "prv3_product_e2e_isolated"
New-Item -ItemType Directory -Force -Path $artifactFullPath, $isolation |
    Out-Null

$fixtures = @(
    [ordered]@{ name = "material_matrix"; file = "material_matrix_854x480.urescene"; eligible = 55 },
    [ordered]@{ name = "glass"; file = "glass_854x480.urescene"; eligible = 255 },
    [ordered]@{ name = "mie_volume"; file = "mie_volume_854x480.urescene"; eligible = 51 },
    [ordered]@{ name = "diffractive"; file = "diffractive_854x480.urescene"; eligible = 1 },
    [ordered]@{ name = "fluorescent"; file = "fluorescent_854x480.urescene"; eligible = 1 }
)
foreach ($path in @($runtime, $worker, $cli, $runner, $validator, $comparator,
                    $orchestrator, $python)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PRV.3 ProductE2E input is missing: $path"
    }
}

$runs = @()
$transportParity = @()
foreach ($fixture in $fixtures) {
    $scene = Join-Path $fixtureRoot $fixture.file
    if (-not (Test-Path -LiteralPath $scene -PathType Leaf)) {
        throw "PRV.3 fixture is missing: $scene"
    }
    $direct = Invoke-Render "direct" $scene $fixture.name $fixture.eligible
    $isolated = Invoke-Render "worker" $scene $fixture.name $fixture.eligible
    if ($direct.plan_identity -ne $isolated.plan_identity) {
        throw "PRV.3 Direct/Worker plan parity failed for $($fixture.name)"
    }
    $parityPath = Join-Path $artifactFullPath "$($fixture.name)-transport-parity.json"
    [void](Invoke-Isolated $python @(
        $comparator,
        "--candidate", (Join-Path $artifactFullPath "$($fixture.name)-worker.spp$Samples.pfm"),
        "--reference", (Join-Path $artifactFullPath "$($fixture.name)-direct.spp$Samples.pfm"),
        "--max-relative-rmse", "0.005", "--report", $parityPath
    ) $isolation)
    $transportParity += Get-Content -Raw -LiteralPath $parityPath |
        ConvertFrom-Json -Depth 100
    $runs += $direct
    $runs += $isolated
}

$matrixScene = Join-Path $fixtureRoot "material_matrix_854x480.urescene"
$cliCommon = @(
    "render", $matrixScene, "--spp", [string]$Samples,
    "--runtime", $runtime, "--worker", $worker
)
$cliWorker = Convert-KeyValues (
    Invoke-Isolated $cli $cliCommon $isolation)
$cliDirect = Convert-KeyValues (
    Invoke-Isolated $cli ($cliCommon + @("--transport", "direct")) $isolation)
foreach ($key in @(
    "completed_samples", "frame", "eligible_integrator_modes",
    "qualified_integrator_modes", "executed_integrator_modes",
    "plan_identity", "frame_content_identity")) {
    if ($cliDirect[$key] -ne $cliWorker[$key] -or
        [string]::IsNullOrWhiteSpace([string]$cliDirect[$key])) {
        throw "PRV.3 CLI Direct/Worker render parity failed for $key"
    }
}

$author = Join-Path $isolation "author"
Remove-Item -LiteralPath $author -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $fixtureRoot -Destination $author -Recurse
$package = Join-Path $artifactFullPath "material_matrix.urepkg"
[void](Invoke-Isolated $cli @(
    "pack", (Join-Path $author "material_matrix_854x480.urescene"),
    "-o", $package, "--transport", "direct", "--runtime", $runtime,
    "--worker", $worker
) $isolation)
Remove-Item -LiteralPath $author -Recurse -Force
$packageDirect = Invoke-Render "direct" $package "packaged_matrix" 55
$packageWorker = Invoke-Render "worker" $package "packaged_matrix" 55
if ($packageDirect.plan_identity -ne $packageWorker.plan_identity) {
    throw "PRV.3 self-contained package plan parity failed"
}
$packageParityPath = Join-Path $artifactFullPath "packaged_matrix-transport-parity.json"
[void](Invoke-Isolated $python @(
    $comparator,
    "--candidate", (Join-Path $artifactFullPath "packaged_matrix-worker.spp$Samples.pfm"),
    "--reference", (Join-Path $artifactFullPath "packaged_matrix-direct.spp$Samples.pfm"),
    "--max-relative-rmse", "0.005", "--report", $packageParityPath
) $isolation)
$transportParity += Get-Content -Raw -LiteralPath $packageParityPath |
    ConvertFrom-Json -Depth 100
$runs += $packageDirect
$runs += $packageWorker

$authoringRoot = Join-Path $artifactFullPath "authoring"
Remove-Item -LiteralPath $authoringRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $authoringRoot | Out-Null
$selector = "material/00000000"
$gltfSource = Join-Path $RepoRoot "scenes/textured_quad_validation.gltf"
$gltfDirectScene = Join-Path $authoringRoot "gltf-direct.urescene"
$gltfWorkerScene = Join-Path $authoringRoot "gltf-worker.urescene"
$gltfDirectBuild = Invoke-SceneTool @(
    "build", $gltfSource, "-o", $gltfDirectScene) "direct"
$gltfWorkerBuild = Invoke-SceneTool @(
    "build", $gltfSource, "-o", $gltfWorkerScene) "worker"
foreach ($sidecar in @(
    (Join-Path $authoringRoot "textured_quad_validation.ppm"))) {
    if (-not (Test-Path -LiteralPath $sidecar -PathType Leaf)) {
        throw "PRV.3 glTF authoring omitted required sidecar: $sidecar"
    }
}
$gltfDirect = Invoke-Render "direct" $gltfDirectScene "authored_gltf" 255 "64x64" 64
$gltfWorker = Invoke-Render "worker" $gltfWorkerScene "authored_gltf" 255 "64x64" 64
if ($gltfDirect.plan_identity -ne $gltfWorker.plan_identity) {
    throw "PRV.3 authored glTF plan parity failed"
}

$presetDirectScene = Join-Path $authoringRoot "preset-direct.urescene"
$presetWorkerScene = Join-Path $authoringRoot "preset-worker.urescene"
$presetDirect = Invoke-SceneTool @(
    "material-preset", $matrixScene, "-o", $presetDirectScene,
    "--material", $selector, "--preset", "clear_glass") "direct"
$presetWorker = Invoke-SceneTool @(
    "material-preset", $matrixScene, "-o", $presetWorkerScene,
    "--material", $selector, "--preset", "clear_glass") "worker"
$presetDirectRun = Invoke-Render "direct" $presetDirectScene "authored_preset" 0
$presetWorkerRun = Invoke-Render "worker" $presetWorkerScene "authored_preset" 0
if ($presetDirectRun.plan_identity -ne $presetWorkerRun.plan_identity) {
    throw "PRV.3 authored preset plan parity failed"
}

$directMtlx = Join-Path $authoringRoot "clear-glass-direct.mtlx"
$workerMtlx = Join-Path $authoringRoot "clear-glass-worker.mtlx"
$exportDirect = Invoke-SceneTool @(
    "material-export", $presetDirectScene, "-o", $directMtlx,
    "--material", $selector) "direct"
$exportWorker = Invoke-SceneTool @(
    "material-export", $presetWorkerScene, "-o", $workerMtlx,
    "--material", $selector) "worker"
$mtlxDirectScene = Join-Path $authoringRoot "materialx-direct.urescene"
$mtlxWorkerScene = Join-Path $authoringRoot "materialx-worker.urescene"
$importDirect = Invoke-SceneTool @(
    "material-import", $matrixScene, $directMtlx, "-o", $mtlxDirectScene,
    "--material", $selector) "direct"
$importWorker = Invoke-SceneTool @(
    "material-import", $matrixScene, $workerMtlx, "-o", $mtlxWorkerScene,
    "--material", $selector) "worker"
$mtlxDirectRun = Invoke-Render "direct" $mtlxDirectScene "authored_materialx" 0
$mtlxWorkerRun = Invoke-Render "worker" $mtlxWorkerScene "authored_materialx" 0
if ($mtlxDirectRun.plan_identity -ne $mtlxWorkerRun.plan_identity) {
    throw "PRV.3 authored MaterialX plan parity failed"
}

$authoringRuns = @(
    $gltfDirect, $gltfWorker,
    $presetDirectRun, $presetWorkerRun,
    $mtlxDirectRun, $mtlxWorkerRun)
$authoringParity = @(
    (Compare-Transport "authored_gltf" 64),
    (Compare-Transport "authored_preset" $Samples),
    (Compare-Transport "authored_materialx" $Samples))

$report = [ordered]@{
    schema = "ure.preview.prv3-material-e2e/1.0"
    status = "Passed"
    tier = "ProductFunctional"
    product_release_declared = $false
    product_surface = "exact-build ProductJob 0.4 / Scene Tool 0.2"
    samples = $Samples
    source = [ordered]@{
        runtime_sha256 = Get-Sha256 $runtime
        worker_sha256 = Get-Sha256 $worker
        cli_sha256 = Get-Sha256 $cli
        scenario_runner_sha256 = Get-Sha256 $runner
        image_validator_sha256 = Get-Sha256 $validator
        image_comparator_sha256 = Get-Sha256 $comparator
        orchestration_sha256 = Get-Sha256 $orchestrator
    }
    fixtures = @($fixtures | ForEach-Object {
        $path = Join-Path $fixtureRoot $_.file
        [ordered]@{
            id = $_.name
            path = "tests/assets/product_e2e/prv3/$($_.file)"
            sha256 = Get-Sha256 $path
            expected_eligible_integrator_modes = $_.eligible
        }
    })
    runs = $runs
    transport_parity = $transportParity
    cli = @(
        [ordered]@{ transport = "Direct"; values = $cliDirect },
        [ordered]@{ transport = "Worker"; values = $cliWorker }
    )
    package = [ordered]@{
        path = $package
        sha256 = Get-Sha256 $package
        authoring_source_deleted_before_render = $true
    }
    authoring = [ordered]@{
        purpose = "Supplementary adapter-to-canonical-artifact-to-ProductJob evidence; the 64x64 glTF case is contract smoke and does not replace the 480p functional matrix."
        sources = @(
            [ordered]@{ kind = "glTF"; path = "scenes/textured_quad_validation.gltf"; sha256 = Get-Sha256 $gltfSource },
            [ordered]@{ kind = "Preset"; name = "clear_glass"; base_scene_sha256 = Get-Sha256 $matrixScene },
            [ordered]@{ kind = "MaterialXDerived"; direct_sha256 = Get-Sha256 $directMtlx; worker_sha256 = Get-Sha256 $workerMtlx })
        operations = [ordered]@{
            gltf_build = @($gltfDirectBuild, $gltfWorkerBuild)
            preset_realization = @($presetDirect, $presetWorker)
            materialx_export = @($exportDirect, $exportWorker)
            materialx_import = @($importDirect, $importWorker)
        }
        runs = $authoringRuns
        transport_parity = $authoringParity
    }
}
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
New-Item -ItemType Directory -Force -Path (
    Split-Path -Parent $reportFullPath) | Out-Null
[System.IO.File]::WriteAllText(
    $reportFullPath, $json + "`n",
    [System.Text.UTF8Encoding]::new($false))

Write-Output "PRV.3 material ProductE2E passed: 5 feature fixtures, 3 authored material paths, Direct/Worker raw+PNG parity, CLI parity, self-contained package"
