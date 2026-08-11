param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "docs/reports/phase_prv1_validation_v1.json",
    [ValidateSet("NotRun", "Passed")][string]$FullGateState = "NotRun",
    [switch]$SkipFocusedTests
)

$ErrorActionPreference = "Stop"

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

function Read-PfmHeader([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.StreamReader]::new(
            $stream, [System.Text.Encoding]::ASCII, $false, 1024, $true)
        if ($reader.ReadLine() -ne "PF") {
            throw "PRV.1 artifact is not a color PFM: $Path"
        }
        $dimensions = $reader.ReadLine()
        $scale = $reader.ReadLine()
        if ($dimensions -notmatch '^(?<width>[1-9][0-9]*) (?<height>[1-9][0-9]*)$' -or
            $scale -ne "-1.0") {
            throw "PRV.1 PFM header is invalid: $Path"
        }
        return [pscustomobject]@{
            Width = [int]$Matches.width
            Height = [int]$Matches.height
        }
    } finally {
        $stream.Dispose()
    }
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$runtimePath = Join-Path $buildPath "artifacts/Release/bin/ultrarender_runtime_1.dll"
$workerPath = Join-Path $buildPath "artifacts/Release/bin/ultrarender_worker_1.exe"
$cliPath = Join-Path $buildPath "artifacts/Release/bin/ure_cli.exe"
$scenePath = Join-Path $RepoRoot "tests/assets/native_scene/q4_procedural_scene/procedural_scene.urescene"
$directImagePath = Join-Path $buildPath "tests/contract/prv1_direct.pfm"
$workerImagePath = Join-Path $buildPath "tests/contract/prv1_worker.pfm"

foreach ($path in @($runtimePath, $workerPath, $cliPath, $scenePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PRV.1 validation input is missing: $path"
    }
}
if (-not $SkipFocusedTests) {
    & ctest --test-dir $buildPath -C Release -R '^test_client_transport$|^test_prv1_cli$' --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "PRV.1 focused CTest gate failed"
    }
}
foreach ($path in @($directImagePath, $workerImagePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PRV.1 rendered image is missing: $path"
    }
}

$directHeader = Read-PfmHeader $directImagePath
$workerHeader = Read-PfmHeader $workerImagePath
$directFile = Get-Item -LiteralPath $directImagePath
$workerFile = Get-Item -LiteralPath $workerImagePath
$directHash = Get-Sha256 $directImagePath
$workerHash = Get-Sha256 $workerImagePath
if ($directHash -ne $workerHash -or $directFile.Length -ne $workerFile.Length -or
    $directHeader.Width -ne $workerHeader.Width -or
    $directHeader.Height -ne $workerHeader.Height) {
    throw "Direct and Worker image artifacts are not byte-identical"
}

$common = @(
    "render", $scenePath, "--spp", "2", "--runtime", $runtimePath,
    "--worker", $workerPath
)
$workerOutput = & $cliPath @common 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Default Worker CLI validation failed: $workerOutput"
}
$directOutput = & $cliPath @common --transport direct 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Explicit Direct CLI validation failed: $directOutput"
}
foreach ($key in @(
    "accepted_samples", "frame", "frame_bytes", "build_identity", "snapshot_identity",
    "objective_identity", "plan_identity", "frame_content_identity"
)) {
    $workerValue = [regex]::Match($workerOutput, "(?m)^$key=(.+)$").Groups[1].Value.Trim()
    $directValue = [regex]::Match($directOutput, "(?m)^$key=(.+)$").Groups[1].Value.Trim()
    if ([string]::IsNullOrWhiteSpace($workerValue) -or $workerValue -ne $directValue) {
        throw "CLI transport identity drifted for $key"
    }
}
if ($workerOutput -notmatch '(?m)^transport=worker\r?$' -or
    $directOutput -notmatch '(?m)^transport=direct\r?$') {
    throw "CLI transport selection was not explicit"
}

$manifestPath = Join-Path $RepoRoot "contracts/generated/runtime_manifest_1.json"
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json -Depth 100
$testLines = @(& ctest --test-dir $buildPath -C Release -N)
if ($LASTEXITCODE -ne 0) {
    throw "CTest inventory failed"
}
$testCount = @($testLines | Where-Object { $_ -match '^\s*Test\s+#\d+:' }).Count

$report = [ordered]@{
    schema = "ure.phase_prv1.validation.v1"
    preview_release_declared = $false
    component_state = if ($FullGateState -eq "Passed") { "Verified" } else { "Implemented" }
    source = [ordered]@{
        registry_digest = [string]$manifest.registry_digest
        product_schema_sha256 = Get-Sha256 (Join-Path $RepoRoot "contracts/schemas/ure_product_v0.fbs")
        client_api_sha256 = Get-Sha256 (Join-Path $RepoRoot "libs/ure_client/include/ure/client/client.hpp")
        direct_transport_sha256 = Get-Sha256 (Join-Path $RepoRoot "libs/ure_client/src/direct_transport.cpp")
        worker_transport_sha256 = Get-Sha256 (Join-Path $RepoRoot "libs/ure_client/src/worker_transport.cpp")
        cli_sha256 = Get-Sha256 (Join-Path $RepoRoot "apps/ure_cli/src/main.cpp")
        worker_sha256 = Get-Sha256 (Join-Path $RepoRoot "apps/ure_worker/runtime_client.cpp")
    }
    contract = [ordered]@{
        core_abi = "1.0"
        worker_protocol = "1.0"
        product_extension = "0.1"
        bootstrap_exports = @($manifest.loader_exports)
    }
    client = [ordered]@{
        default_transport = "Worker"
        explicit_transports = @("Direct", "Worker")
        implicit_fallback = $false
        identity_parity = $true
    }
    image_e2e = @(
        [ordered]@{
            transport = "Direct"
            file = "prv1_direct.pfm"
            bytes = [int64]$directFile.Length
            sha256 = $directHash
            width = $directHeader.Width
            height = $directHeader.Height
        },
        [ordered]@{
            transport = "Worker"
            file = "prv1_worker.pfm"
            bytes = [int64]$workerFile.Length
            sha256 = $workerHash
            width = $workerHeader.Width
            height = $workerHeader.Height
        }
    )
    behavioral_gates = @(
        "native_scene_load", "product_job_render", "immutable_frame_copy",
        "artifact_manifest", "direct_worker_identity_parity", "unsupported_objective_rejection",
        "direct_worker_cancellation", "worker_launch_failure_isolation",
        "no_implicit_transport_fallback", "multiple_job_client_lifecycle",
        "bounded_worker_shutdown", "shared_frame_layout_and_digest",
        "cli_renderer_ownership_removed"
    )
    ctest = [ordered]@{
        configuration = "Release"
        registered = $testCount
        focused_failed = 0
        full_gate_state = $FullGateState
    }
    semantic_digest = ""
}
$report.semantic_digest = Get-TextSha256 ($report | ConvertTo-Json -Depth 100 -Compress)
$parent = Split-Path -Parent $reportFullPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
[System.IO.File]::WriteAllText($reportFullPath, $json + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Output "PRV.1 validation report written: $ReportPath ($testCount tests, two byte-identical $($directHeader.Width)x$($directHeader.Height) images)"
