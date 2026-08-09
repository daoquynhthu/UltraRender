param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$required = @(
    "contracts/reports/ure_phase_pb_validation_v1.schema.json",
    "contracts/reports/pb7_fuzz_corpus.json",
    "docs/Public_API_Candidate_Integration.md",
    "docs/PB7_Compatibility_Report.md",
    "scripts/run_phase_pb_validation_suite.ps1",
    "tests/contract/external_client/CMakeLists.txt",
    "tests/contract/test_pb7_fuzz.cpp"
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $relative) -PathType Leaf)) {
        throw "PB.7 artifact is missing: $relative"
    }
}

$scenarioManifest = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "contracts/generated/mock_scenarios.json") | ConvertFrom-Json
$requiredScenarios = @(
    "normal_lifecycle", "missing_optional_capability",
    "missing_required_capability", "registry_mismatch", "old_minor",
    "unknown_optional_field", "worker_crash", "malformed_message",
    "truncated_message", "oversized_message"
)
$scenarioNames = @($scenarioManifest.scenarios.name)
foreach ($name in $requiredScenarios) {
    if ($scenarioNames -notcontains $name) {
        throw "PB.7 golden scenario is missing: $name"
    }
}

$fixtures = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot "tests/fixtures/contracts/old_clients") -Filter manifest.json -File -Recurse |
    Where-Object FullName -Match 'candidate_0_1_pb[2-6]')
if ($fixtures.Count -ne 5) {
    throw "PB.7 must retain five published Candidate client baselines"
}
foreach ($fixture in $fixtures) {
    $manifest = Get-Content -Raw -LiteralPath $fixture | ConvertFrom-Json
    foreach ($field in @("baseline_commit", "source_sha256", "binary_sha256", "sdk_header_sha256", "compiler", "windows_sdk", "expected_capabilities")) {
        if (-not $manifest.$field) {
            throw "$($fixture.FullName) lacks $field"
        }
    }
    if ($manifest.compatibility_promise -ne "None before PB.8") {
        throw "$($fixture.FullName) implies a compatibility promise"
    }
}

$ledger = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "contracts/public_interaction_surface_ledger.json") | ConvertFrom-Json
$boundary = $ledger.entries | Where-Object id -EQ "pb_public_boundary_candidate"
foreach ($evidence in @("test_candidate_compatibility", "test_pb7_fuzz", "test_worker_runtime_security", "test_external_client_package")) {
    if ($boundary.conformance_evidence -notcontains $evidence) {
        throw "Public boundary ledger lacks PB.7 evidence: $evidence"
    }
}

$schema = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "contracts/reports/ure_phase_pb_validation_v1.schema.json") | ConvertFrom-Json
if ($schema.'$id' -ne "ure.phase_pb.validation.v1" -or
    $schema.properties.publication_state.const -ne "Candidate" -or
    $schema.properties.compatibility_promise.const -ne "None before PB.8") {
    throw "PB validation report schema weakens Candidate governance"
}
$corpus = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "contracts/reports/pb7_fuzz_corpus.json") | ConvertFrom-Json
$targets = @($corpus.targets.name)
foreach ($target in @("loader_headers_and_chains", "registry_json_and_schema", "flatbuffers_worker_messages", "native_scene_blobs", "scene_transaction_payloads", "handle_lifecycle", "frame_mapping", "cancellation_races", "worker_crash_races")) {
    if ($targets -notcontains $target) {
        throw "PB.7 bounded fuzz corpus omits $target"
    }
}

Write-Output "PB.7 static closure contract passed"
