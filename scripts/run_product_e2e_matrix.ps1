param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "build_modular_x64/artifacts/Release/evidence/prv1r_functional/report.json",
    [string]$ArtifactDir = "build_modular_x64/artifacts/Release/evidence/prv1r_functional",
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
        throw "Product E2E command failed ($exitCode): $Executable $($Arguments -join ' ')`n$output"
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

function Invoke-ImageValidation(
    [string]$Python,
    [System.Collections.IDictionary]$Run,
    [string]$Prefix,
    [string]$ArtifactDirectory,
    [string]$WorkingDirectory) {
    $png = Join-Path $ArtifactDirectory "$Prefix.png"
    $metrics = Join-Path $ArtifactDirectory "$Prefix.metrics.json"
    $arguments = @(
        (Join-Path $RepoRoot "scripts/validate_product_image.py"),
        "--low", $Run.low_raw,
        "--mid", $Run.mid_raw,
        "--final", $Run.final_raw,
        "--png", $png,
        "--report", $metrics
    )
    [void](Invoke-Isolated $Python $arguments $WorkingDirectory)
    return Get-Content -Raw -LiteralPath $metrics | ConvertFrom-Json -Depth 100
}

function Select-RunEvidence(
    [System.Collections.IDictionary]$Run,
    [object]$Image) {
    return [ordered]@{
        transport = $Run.transport
        requested_samples = [uint64]$Run.requested_samples
        accepted_samples = [uint64]$Run.accepted_samples
        completed_samples = [uint64]$Run.completed_samples
        low_samples = [uint64]$Run.low_samples
        mid_samples = [uint64]$Run.mid_samples
        resolution = $Run.frame
        elapsed_ms = [uint64]$Run.elapsed_ms
        build_identity = $Run.build_identity
        snapshot_identity = $Run.snapshot_identity
        objective_identity = $Run.objective_identity
        plan_identity = $Run.plan_identity
        frame_content_identity = $Run.frame_content_identity
        device = [ordered]@{
            identity = $Run.device_identity
            backend = [uint32]$Run.backend
            provider = [uint32]$Run.provider
            name = $Run.device_name
            adapter_id = $Run.adapter_id
            driver_identity = $Run.driver_identity
            compiler_identity = $Run.compiler_identity
            total_memory_bytes = [uint64]$Run.total_memory_bytes
            available_memory_bytes = [uint64]$Run.available_memory_bytes
            selected_memory_budget_bytes = [uint64]$Run.selected_memory_budget_bytes
        }
        raw = [ordered]@{
            final_sha256 = $Image.raw_authority.final_sha256
            low_sha256 = $Image.raw_authority.low_sha256
            mid_sha256 = $Image.raw_authority.mid_sha256
        }
        derived_png = [ordered]@{
            sha256 = $Image.derived_view.sha256
            view_transform = $Image.derived_view.view_transform
        }
        metrics = $Image.metrics
        convergence = $Image.convergence
    }
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$artifactFullPath = Resolve-RepositoryPath $ArtifactDir
$runtime = Join-Path $buildPath "artifacts/Release/bin/ultrarender_runtime_1.dll"
$worker = Join-Path $buildPath "artifacts/Release/bin/ultrarender_worker_1.exe"
$cli = Join-Path $buildPath "artifacts/Release/bin/ure_cli.exe"
$runner = Join-Path $buildPath "artifacts/Release/bin/product_scenario_runner.exe"
$scene = Join-Path $RepoRoot "tests/assets/product_e2e/cornell_854x480.urescene"
$python = (Get-Command python -ErrorAction Stop).Source
$isolation = Join-Path $buildPath "product_e2e_isolated_cwd"
New-Item -ItemType Directory -Force -Path $artifactFullPath, $isolation | Out-Null

foreach ($path in @($runtime, $worker, $cli, $runner, $scene, $python)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Product E2E input is missing: $path"
    }
}

$directText = Invoke-Isolated $runner @(
    "direct", $runtime, $worker, $scene, [string]$Samples,
    (Join-Path $artifactFullPath "direct")) $isolation
$workerText = Invoke-Isolated $runner @(
    "worker", $runtime, $worker, $scene, [string]$Samples,
    (Join-Path $artifactFullPath "worker")) $isolation
$direct = Convert-KeyValues $directText
$workerRun = Convert-KeyValues $workerText
$directImage = Invoke-ImageValidation $python $direct "direct" $artifactFullPath $isolation
$workerImage = Invoke-ImageValidation $python $workerRun "worker" $artifactFullPath $isolation

$cliCommon = @(
    "render", $scene, "--spp", [string]$Samples,
    "--runtime", $runtime, "--worker", $worker
)
$cliWorker = Convert-KeyValues (Invoke-Isolated $cli $cliCommon $isolation)
$cliDirect = Convert-KeyValues (
    Invoke-Isolated $cli ($cliCommon + @("--transport", "direct")) $isolation)

$identityKeys = @(
    "accepted_samples", "completed_samples", "frame", "build_identity",
    "snapshot_identity", "objective_identity", "plan_identity",
    "frame_content_identity"
)
foreach ($key in $identityKeys) {
    if ([string]::IsNullOrWhiteSpace([string]$direct[$key]) -or
        $direct[$key] -ne $workerRun[$key] -or
        $direct[$key] -ne $cliDirect[$key] -or
        $direct[$key] -ne $cliWorker[$key]) {
        throw "Product E2E parity failed for $key"
    }
}
if ($directImage.raw_authority.final_sha256 -ne $workerImage.raw_authority.final_sha256) {
    throw "Direct and Worker authoritative raw artifacts differ"
}

$report = [ordered]@{
    schema = "ure.preview.product-e2e-evidence/1.0"
    tier = "ProductFunctional"
    status = "Passed"
    product_release_declared = $false
    product_path = "ure_client/generated SDK -> explicit transport -> ProductJob 0.3 -> ure_product -> immutable Frame/artifact"
    production_profile = "ProductJob 0.3 default automatic production profile"
    fixture = [ordered]@{
        id = "cornell_854x480"
        source = "tests/assets/product_e2e/cornell_854x480.urescene"
        sha256 = Get-Sha256 $scene
        source_gltf = "scenes/cornell_box.gltf"
        source_gltf_sha256 = Get-Sha256 (Join-Path $RepoRoot "scenes/cornell_box.gltf")
    }
    binaries = [ordered]@{
        runtime_sha256 = Get-Sha256 $runtime
        worker_sha256 = Get-Sha256 $worker
        cli_sha256 = Get-Sha256 $cli
        scenario_runner_sha256 = Get-Sha256 $runner
    }
    runs = @(
        Select-RunEvidence $direct $directImage
        Select-RunEvidence $workerRun $workerImage
    )
    cli = @(
        [ordered]@{
            transport = "Direct"
            accepted_samples = [uint64]$cliDirect.accepted_samples
            completed_samples = [uint64]$cliDirect.completed_samples
            resolution = $cliDirect.frame
            plan_identity = $cliDirect.plan_identity
            frame_content_identity = $cliDirect.frame_content_identity
        },
        [ordered]@{
            transport = "Worker"
            accepted_samples = [uint64]$cliWorker.accepted_samples
            completed_samples = [uint64]$cliWorker.completed_samples
            resolution = $cliWorker.frame
            plan_identity = $cliWorker.plan_identity
            frame_content_identity = $cliWorker.frame_content_identity
        }
    )
    parity = [ordered]@{
        same_plan_identity = $true
        same_frame_content_identity = $true
        byte_identical_raw_secondary_check = $true
        same_device_identity = ($direct.device_identity -eq $workerRun.device_identity)
    }
    maintained_call_matrix = @(
        [ordered]@{ call = "ure_client Direct"; evidence = "this report"; artifact = "authoritative PFM plus derived PNG" },
        [ordered]@{ call = "ure_client Worker"; evidence = "this report"; artifact = "authoritative PFM plus derived PNG" },
        [ordered]@{ call = "CLI Direct"; evidence = "this report"; artifact = "runtime Frame and artifact identity" },
        [ordered]@{ call = "CLI Worker"; evidence = "this report"; artifact = "runtime Frame and artifact identity" },
        [ordered]@{ call = "external C Core client"; evidence = "test_external_client_package"; artifact = "PFM" },
        [ordered]@{ call = "external C++ exact-build SDK Direct/Worker"; evidence = "test_external_client_package"; artifact = "PFM" },
        [ordered]@{ call = "cancel/error/isolated CWD"; evidence = "test_client_transport,test_prv1_cli,this report"; artifact = "structured terminal evidence" }
    )
    semantic_digest = ""
}
$report.semantic_digest = Get-TextSha256 (
    $report | ConvertTo-Json -Depth 100 -Compress)
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
[System.IO.File]::WriteAllText(
    $reportFullPath, $json + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Output "Product functional E2E matrix passed: 4 maintained product calls, two validated $($direct.frame) raw/PNG artifacts"
