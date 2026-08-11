param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = (Join-Path $RepoRoot "build_modular_x64"),
    [string]$ReportPath = (Join-Path $BuildDir "phase_pb_validation_v2.json"),
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
        $_ -notin @("docs/reports/phase_pb_validation.json", "docs/reports/phase_pb_validation_v2.json") -and
        (Test-Path -LiteralPath (Join-Path $RepoRoot $_) -PathType Leaf)
    } | ForEach-Object { Get-Item -LiteralPath (Join-Path $RepoRoot $_) })
$sourceDigest = Get-TreeDigest $tracked $RepoRoot
$runtime = Join-Path $BuildDir "libs/ure_contract/ultrarender_runtime_1.dll"
$worker = Join-Path $BuildDir "apps/ure_worker/ultrarender_worker_1.exe"
$sdkManifest = Join-Path $BuildDir "pb8_packages/sdk/package_manifest.json"
$runtimeManifest = Join-Path $BuildDir "pb8_packages/runtime/package_manifest.json"
$freezeReview = Join-Path $RepoRoot "contracts/stability/core_1_0_freeze_review.json"
$callCoverage = Join-Path $RepoRoot "contracts/e2e/core_1_0_call_coverage.json"
$compatibilityMatrix = Join-Path $RepoRoot "contracts/stability/core_1_0_compatibility_matrix.json"
$seedManifest = Join-Path $RepoRoot "tests/fixtures/contracts/old_clients/core_1_0_seed_from_pb7/windows_x64/manifest.json"
$reportSchema = Join-Path $RepoRoot "contracts/reports/ure_phase_pb_validation_v2.schema.json"
foreach ($required in @($runtime, $worker, $sdkManifest, $runtimeManifest, $freezeReview, $callCoverage, $compatibilityMatrix, $seedManifest, $reportSchema)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required validation artifact is missing: $required"
    }
}

$preReleaseClientManifests = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot "tests/fixtures/contracts/old_clients") -Filter manifest.json -File -Recurse |
    Where-Object FullName -Match 'candidate_0_1_pb[2-6]' | Sort-Object FullName)
if ($preReleaseClientManifests.Count -ne 5) {
    throw "Pre-release client retention history is incomplete"
}
$preReleaseClientEntries = foreach ($manifest in $preReleaseClientManifests) {
    $value = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
    [ordered]@{
        phase = $value.phase
        baseline_commit = $value.baseline_commit
        sdk_header_sha256 = $value.sdk_header_sha256
        binary_sha256 = $value.binary_sha256
    }
}

$seed = Get-Content -Raw -LiteralPath $seedManifest | ConvertFrom-Json
$imageRoot = Join-Path $BuildDir "pb8_external_client_build/rendered_images"
$imageNames = @("direct_map.pfm", "direct_copy.pfm", "transaction_replay.pfm", "transaction_replace.pfm", "worker_first.pfm", "worker_restart.pfm")
$imageEvidence = foreach ($name in $imageNames) {
    $path = Join-Path $imageRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 32) {
        throw "Required rendered image evidence is missing or empty: $name"
    }
    [ordered]@{
        calling_mode = if ($name.StartsWith("direct_")) { "in_process_core_c11" } elseif ($name.StartsWith("transaction_")) { "in_process_unstable_transaction_cpp" } else { "local_worker_named_pipe_shared_memory" }
        file = $name
        bytes = (Get-Item -LiteralPath $path).Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
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
    schema = "ure.phase_pb.validation.v2"
    publication_state = "Stable"
    declaration_state = "Declared"
    compatibility_promise = "Core ABI 1.0 and local Worker Protocol 1.0 on Windows x64; this is not an UltraRender 1.0 product release and public distribution requires separate authorization"
    source = [ordered]@{
        tree_sha256 = $sourceDigest
        registry_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $RepoRoot "contracts/generated/registry/public_contract_registry.canonical.json")).Hash.ToLowerInvariant()
        registry_semantic_digest = "c358276424a2cdc71cfefc6edac290ee78fa75a2bf918edecb8f37f4d991af42"
        freeze_review_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $freezeReview).Hash.ToLowerInvariant()
        call_coverage_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $callCoverage).Hash.ToLowerInvariant()
        report_schema_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $reportSchema).Hash.ToLowerInvariant()
    }
    artifacts = [ordered]@{
        runtime_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtime).Hash.ToLowerInvariant()
        worker_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $worker).Hash.ToLowerInvariant()
        sdk_package_manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $sdkManifest).Hash.ToLowerInvariant()
        runtime_package_manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeManifest).Hash.ToLowerInvariant()
    }
    abi = [ordered]@{
        manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $RepoRoot "contracts/abi/windows_x64_core_1_0.json")).Hash.ToLowerInvariant()
        exports = @("ureGetRuntimeManifest", "ureQueryInterface")
        platform = "windows-x64-msvc-c11"
    }
    compatibility_matrix = [ordered]@{
        core_1_0_seed = [ordered]@{
            baseline_commit = $seed.baseline_commit
            sdk_header_sha256 = $seed.sdk_header_sha256
            binary_sha256 = $seed.binary_sha256
        }
        pre_release_history = @($preReleaseClientEntries)
        matrix_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $compatibilityMatrix).Hash.ToLowerInvariant()
        current_client_prior_stable_runtime = "NotApplicable for the first stable major; mandatory after a post-1.0 runtime is retained"
        worker_protocol = "1.0 exact major/minor negotiation with registry digest"
    }
    image_e2e = @($imageEvidence)
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
        "all_core_calls_covered_by_external_clients"
        "six_nontrivial_rendered_images"
        "unstable_scene_transaction_isolated_from_core"
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
$reportJson = (($report | ConvertTo-Json -Depth 10) -replace "`r`n", "`n") + "`n"
[IO.File]::WriteAllText($ReportPath, $reportJson, [Text.UTF8Encoding]::new($false))

$roundTrip = Get-Content -Raw -LiteralPath $ReportPath | ConvertFrom-Json
if (-not ($reportJson | Test-Json -SchemaFile $reportSchema) -or
    $roundTrip.schema -ne "ure.phase_pb.validation.v2" -or
    $roundTrip.publication_state -ne "Stable" -or
    $roundTrip.declaration_state -ne "Declared" -or
    @($roundTrip.image_e2e).Count -ne 6 -or
    $roundTrip.semantic_digest -ne $report.semantic_digest -or
    $roundTrip.ctest.failed -ne 0) {
    throw "Generated PB validation report failed schema invariants"
}
Write-Output "PB validation report: $ReportPath"
Write-Output "Semantic digest: $($report.semantic_digest)"
