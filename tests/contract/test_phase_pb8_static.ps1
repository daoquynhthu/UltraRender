param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = "Stop"
function Read-Json([string]$Relative) {
    return Get-Content -Raw -LiteralPath (Join-Path $RepoRoot $Relative) | ConvertFrom-Json
}

$registry = Read-Json "contracts/registry/public_contract_registry.json"
$review = Read-Json "contracts/stability/core_1_0_freeze_review.json"
$coverage = Read-Json "contracts/e2e/core_1_0_call_coverage.json"
$compatibility = Read-Json "contracts/registry/registry_compatibility.json"
$manifest = Read-Json "contracts/generated/runtime_manifest_1.json"
$ledger = Read-Json "contracts/public_interaction_surface_ledger.json"
$header = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "contracts/generated/include/ultrarender/ure_loader.h")
$registryHeader = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "contracts/generated/include/ultrarender/ure_registry.h")
$currentDigest = [regex]::Match(
    $registryHeader,
    'URE_REGISTRY_DIGEST_HEX "(?<digest>[0-9a-f]{64})"').Groups['digest'].Value

if ($registry.publication_state -ne "Stable" -or $registry.version -ne "1.0.0" -or
    $review.registry_digest -notmatch '^[0-9a-f]{64}$' -or
    $currentDigest -ne $manifest.registry_digest -or
    $manifest.publication_state -ne "Stable" -or $manifest.version -ne "1.0.0" -or
    $manifest.core_abi.major -ne 1 -or $manifest.worker_protocol.major -ne 1 -or
    $manifest.frame_schema.major -ne 1) {
    throw "Stable 1.0 registry or manifest identity is inconsistent"
}
$publicTransport = @($ledger.entries | Where-Object disposition -eq "PublicTransport")
if ($publicTransport.Count -ne 1 -or
    $publicTransport[0].id -ne "pb_public_boundary_core_1_0" -or
    $publicTransport[0].contract_stability -ne "Core" -or
    $publicTransport[0].bypass_status -ne "None") {
    throw "The interaction-surface ledger does not identify one stable Core transport"
}

$coreIds = @($registry.entries | Where-Object stability -EQ "Core" | ForEach-Object { [uint32]$_.registry_id } | Sort-Object)
$reviewIds = @($review.groups | ForEach-Object { $_.registry_ids } | ForEach-Object { [uint32]$_ })
$duplicateReviewIds = @($reviewIds | Group-Object | Where-Object Count -NE 1)
$reviewIds = @($reviewIds | Sort-Object)
if ($duplicateReviewIds.Count -ne 0 -or
    (Compare-Object $coreIds $reviewIds).Count -ne 0 -or
    $coreIds.Count -ne $review.frozen_core_registry_entry_count) {
    throw "The freeze review does not cover every Core registry identity exactly once"
}
foreach ($group in $review.groups) {
    if ($group.decision -ne "Freeze" -or
        [string]::IsNullOrWhiteSpace($group.extension_impossibility) -or
        [string]::IsNullOrWhiteSpace($group.evolution)) {
        throw "A Core review group lacks a freeze rationale or evolution rule: $($group.name)"
    }
}

$tombstones = @($registry.tombstones.registry_id | ForEach-Object { [uint32]$_ } | Sort-Object)
$compatibilityTombstones = @($compatibility.tombstones | ForEach-Object { [uint32]$_ } | Sort-Object)
$demoted = @($review.pre_release_demotions.old_registry_id | ForEach-Object { [uint32]$_ } | Sort-Object)
if ((Compare-Object $tombstones $compatibilityTombstones).Count -ne 0 -or
    (Compare-Object $tombstones $demoted).Count -ne 0 -or
    $review.stable_extensions.Count -ne 0) {
    throw "Pre-release demotions, tombstones, or the empty initial StableExtension list drifted"
}

foreach ($property in $coverage.core_functions.PSObject.Properties) {
    $table = [regex]::Escape($property.Name)
    $match = [regex]::Match($header, "typedef struct $table \{(?<body>[\s\S]*?)\} $table;")
    if (-not $match.Success) {
        throw "Covered Core table is absent from the public header: $($property.Name)"
    }
    $actual = @([regex]::Matches($match.Groups['body'].Value, '\(URE_CALL \*(?<name>[a-z0-9_]+)\)') |
        ForEach-Object { $_.Groups['name'].Value } | Sort-Object)
    $expected = @($property.Value | Sort-Object)
    if ((Compare-Object $actual $expected).Count -ne 0) {
        throw "Core function coverage drifted for $($property.Name)"
    }
}
$sceneBlock = [regex]::Match($header, 'typedef struct ure_scene_interface_t \{(?<body>[\s\S]*?)\} ure_scene_interface_t;').Groups['body'].Value
$transactionBlock = [regex]::Match($header, 'typedef struct ure_scene_transaction_interface_t \{(?<body>[\s\S]*?)\} ure_scene_transaction_interface_t;').Groups['body'].Value
if ($sceneBlock -match 'apply_transaction' -or $transactionBlock -notmatch 'apply_transaction' -or
    $registryHeader -notmatch 'URE_INTERFACE_SCENE_TRANSACTION_UUID_BYTES') {
    throw "The Experimental transaction surface leaked back into the Core Scene table"
}

$sourceText = (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "libs/ure_contract/src/runtime_objects.hpp")) +
    (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "libs/ure_contract/src/loader.cpp"))
if ($sourceText -match 'header\.size\s*<\s*sizeof\((?!header\))[^)]*\)' -or
    $sourceText -notmatch 'core_1_0_size') {
    throw "Core input/output validation is not anchored to frozen 1.0 prefixes"
}
$workerRuntimeClient = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "apps/ure_worker/runtime_client.cpp")
if ($workerRuntimeClient -match 'response\.table_size\s*<\s*sizeof\(Table\)' -or
    $workerRuntimeClient -notmatch 'required_prefix_size') {
    throw "The product worker does not negotiate frozen Core table prefixes"
}
$workerE2eClient = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "tests/contract/external_client/worker_client.cpp")
foreach ($field in @("protocol_min_major", "protocol_min_minor", "protocol_max_major", "protocol_max_minor", "core_min_major", "core_min_minor", "core_max_major", "core_max_minor", "frame_schema_min_major", "frame_schema_min_minor", "frame_schema_max_major", "frame_schema_max_minor")) {
    if ($workerE2eClient -notmatch "response->handshake->$field") {
        throw "The worker E2E does not assert negotiated $field"
    }
}

$activeStablePaths = @(
    "contracts/generated/runtime_manifest_1.json",
    "contracts/abi/windows_x64_core_1_0.json",
    "contracts/schemas/ure_payload_v1.fbs",
    "contracts/schemas/ure_frame_v1.fbs",
    "contracts/schemas/ure_scene_v1.fbs",
    "contracts/schemas/ure_worker_v1.fbs",
    "contracts/reports/ure_phase_pb_validation_v2.schema.json",
    "docs/Public_API_Integration.md",
    "docs/Public_API_Support_Policy.md",
    "docs/PB8_Stable_Compatibility_Report.md"
)
foreach ($relative in $activeStablePaths) {
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $relative) -PathType Leaf)) {
        throw "Stable boundary artifact is missing: $relative"
    }
}
if ($coverage.calling_modes.Count -ne 3 -or
    @($coverage.calling_modes.image_artifacts | ForEach-Object { $_ }).Count -ne 6 -or
    $coverage.bootstrap_exports.Count -ne 2) {
    throw "Public calling-mode or real-image E2E coverage is incomplete"
}
$imageSources = (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "tests/contract/external_client/direct_client.c")) +
    (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "tests/contract/external_client/transaction_client.cpp")) +
    (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "tests/contract/external_client/worker_e2e.cpp"))
if ([regex]::Matches($imageSources, 'ure_write_pfm_rgba').Count -lt 5 -or
    [regex]::Matches($imageSources, 'render_identity\(').Count -lt 3) {
    throw "The E2E clients do not write every declared real image artifact"
}

Write-Output "PB.8 freeze audit passed: $($coreIds.Count) Core identities, 39 Core functions, three calling modes, six real images"
