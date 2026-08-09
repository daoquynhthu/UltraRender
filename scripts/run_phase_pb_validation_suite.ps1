param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = (Join-Path $RepoRoot "build_modular_x64"),
    [string]$ReportPath = (Join-Path $BuildDir "phase_pb_validation.json"),
    [switch]$RequireClean
)

$ErrorActionPreference = "Stop"

function Get-Sha256Bytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant()
}

function Get-TreeDigest([System.IO.FileInfo[]]$Files, [string]$Root) {
    $records = foreach ($file in ($Files | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($Root.Length).TrimStart('\').Replace('\', '/')
        "$relative`0$((Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant())`n"
    }
    return Get-Sha256Bytes ([Text.Encoding]::UTF8.GetBytes(($records -join "")))
}

if ($RequireClean -and (git -C $RepoRoot status --porcelain)) {
    throw "PB validation requires a clean tracked and untracked worktree"
}

$ctestOutput = & ctest --test-dir $BuildDir -C Release --output-on-failure 2>&1
$ctestExit = $LASTEXITCODE
$ctestText = $ctestOutput -join "`n"
if ($ctestExit -ne 0) {
    throw "Complete maintained CTest gate failed:`n$ctestText"
}
$summary = [regex]::Match($ctestText, "100% tests passed, 0 tests failed out of (\d+)")
if (-not $summary.Success) {
    throw "CTest result summary is not a complete green gate"
}
$testNames = @(& ctest --test-dir $BuildDir -C Release -N | ForEach-Object {
    if ($_ -match '^\s*Test\s+#\d+:\s*(.+)$') { $Matches[1] }
})
if ($testNames.Count -ne [int]$summary.Groups[1].Value) {
    throw "CTest inventory and execution count differ"
}

$auditPath = Join-Path $BuildDir "phase_pb_boundary_audit.json"
& (Join-Path $RepoRoot "scripts/audit_public_boundary.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -ReportPath $auditPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Public interaction surface ledger audit failed"
}
$audit = Get-Content -Raw -LiteralPath $auditPath | ConvertFrom-Json
if ($audit.unresolved_classification_count -ne 0 -or
    $audit.duplicate_authority_count -ne 0 -or
    $audit.forbidden_inspection_count -ne 0) {
    throw "Public interaction surface ledger is not closed"
}

$tracked = @(& git -C $RepoRoot ls-files --cached --others --exclude-standard |
    Where-Object {
        $_ -ne "docs/reports/phase_pb_validation.json" -and
        (Test-Path -LiteralPath (Join-Path $RepoRoot $_) -PathType Leaf)
    } | ForEach-Object { Get-Item -LiteralPath (Join-Path $RepoRoot $_) })
$sourceDigest = Get-TreeDigest $tracked $RepoRoot
$runtime = Join-Path $BuildDir "libs/ure_contract/ultrarender_runtime_candidate.dll"
$worker = Join-Path $BuildDir "apps/ure_worker/ure_worker.exe"
$sdkManifest = Join-Path $BuildDir "pb7_packages/sdk/package_manifest.json"
$runtimeManifest = Join-Path $BuildDir "pb7_packages/runtime/package_manifest.json"
foreach ($required in @($runtime, $worker, $sdkManifest, $runtimeManifest)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required validation artifact is missing: $required"
    }
}

$oldClientManifests = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot "tests/fixtures/contracts/old_clients") -Filter manifest.json -File -Recurse |
    Where-Object FullName -Match 'candidate_0_1_pb[2-6]' | Sort-Object FullName)
if ($oldClientManifests.Count -ne 5) {
    throw "Candidate client retention matrix is incomplete"
}
$compatibilityEntries = foreach ($manifest in $oldClientManifests) {
    $value = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
    [ordered]@{
        phase = $value.phase
        baseline_commit = $value.baseline_commit
        sdk_header_sha256 = $value.sdk_header_sha256
        binary_sha256 = $value.binary_sha256
    }
}

$fuzzFiles = @(
    Get-Item -LiteralPath (Join-Path $RepoRoot "tests/contract/test_pb7_fuzz.cpp")
    Get-Item -LiteralPath (Join-Path $RepoRoot "tests/contract/test_worker_crash.cpp")
    Get-Item -LiteralPath (Join-Path $RepoRoot "tests/contract/test_contract_codegen_negative.ps1")
    Get-Item -LiteralPath (Join-Path $RepoRoot "contracts/reports/pb7_fuzz_corpus.json")
    Get-Item -LiteralPath (Join-Path $RepoRoot "contracts/generated/mock_scenarios.json")
) + @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot "contracts/generated/golden_messages") -File)
$fuzzDigest = Get-TreeDigest $fuzzFiles $RepoRoot

$core = [ordered]@{
    schema = "ure.phase_pb.validation.v1"
    publication_state = "Candidate"
    compatibility_promise = "None before PB.8"
    source = [ordered]@{
        tree_sha256 = $sourceDigest
        registry_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $RepoRoot "contracts/generated/registry/public_contract_registry.canonical.json")).Hash.ToLowerInvariant()
        registry_semantic_digest = "0e56eea2d03b2528ceefe2f686de3b63510d956738ee19cf107835abb297f554"
    }
    artifacts = [ordered]@{
        runtime_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtime).Hash.ToLowerInvariant()
        worker_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $worker).Hash.ToLowerInvariant()
        sdk_package_manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $sdkManifest).Hash.ToLowerInvariant()
        runtime_package_manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeManifest).Hash.ToLowerInvariant()
    }
    abi = [ordered]@{
        manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $RepoRoot "contracts/abi/windows_x64_candidate.json")).Hash.ToLowerInvariant()
        exports = @("ureGetRuntimeManifest", "ureQueryInterface")
        platform = "windows-x64-msvc-c11"
    }
    compatibility_matrix = [ordered]@{
        historical_clients_to_current_runtime = @($compatibilityEntries)
        current_client_supported_runtimes = @("PB.7 current content-digested runtime")
        worker_protocol = @("Candidate protocol/core/frame 0.1 with exact registry digest")
    }
    fuzz_corpus = [ordered]@{
        seed = "0x8d12e519a73bc641"
        loader_cases = 256
        scene_blob_cases = 128
        transaction_payload_cases = 128
        identity_sha256 = $fuzzDigest
        policy = "fixed corpus and bounded budgets"
    }
    behavioral_gates = @(
        "manifest_and_interface_negotiation",
        "table_size_prefix_bounds",
        "required_optional_capabilities",
        "native_scene_and_objective_session",
        "progressive_operations_events",
        "immutable_frame_map_copy",
        "camera_transaction_revision_conflict",
        "cancellation_and_full_reload_fallback",
        "worker_shared_memory_misuse",
        "worker_crash_restart_identity",
        "no_network_or_ambient_discovery",
        "interaction_surface_ledger_closed",
        "independent_sdk_runtime_external_client"
    )
    ctest = [ordered]@{
        configuration = "Release"
        total = $testNames.Count
        failed = 0
        inventory_sha256 = Get-Sha256Bytes ([Text.Encoding]::UTF8.GetBytes((($testNames | Sort-Object) -join "`n") + "`n"))
        boundary_audit_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $auditPath).Hash.ToLowerInvariant()
    }
}

$semanticJson = $core | ConvertTo-Json -Depth 10 -Compress
$report = [ordered]@{}
foreach ($entry in $core.GetEnumerator()) { $report[$entry.Key] = $entry.Value }
$report.environment = [ordered]@{
    excluded_from_semantic_digest = $true
    os = [Environment]::OSVersion.VersionString
    powershell = $PSVersionTable.PSVersion.ToString()
    cmake = (& cmake --version | Select-Object -First 1)
    flatc = (& flatc --version)
}
$report.semantic_digest = Get-Sha256Bytes ([Text.Encoding]::UTF8.GetBytes($semanticJson))
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ReportPath) | Out-Null
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ReportPath -Encoding utf8NoBOM

$roundTrip = Get-Content -Raw -LiteralPath $ReportPath | ConvertFrom-Json
if ($roundTrip.schema -ne "ure.phase_pb.validation.v1" -or
    $roundTrip.semantic_digest -ne $report.semantic_digest -or
    $roundTrip.ctest.failed -ne 0) {
    throw "Generated PB validation report failed schema invariants"
}
Write-Output "PB validation report: $ReportPath"
Write-Output "Semantic digest: $($report.semantic_digest)"
