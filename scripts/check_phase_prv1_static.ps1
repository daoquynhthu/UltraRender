param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$ReportPath = "docs/reports/phase_prv1_validation_v1.json",
    [switch]$RequireVerifiedReport
)

$ErrorActionPreference = "Stop"

function Read-Json([string]$Relative) {
    $path = if ([System.IO.Path]::IsPathRooted($Relative)) {
        $Relative
    } else {
        Join-Path $RepoRoot $Relative
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required PRV.1 artifact is missing: $Relative"
    }
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -Depth 100
}

function Get-Sha256([string]$Relative) {
    $path = Join-Path $RepoRoot $Relative
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
}

function Get-TextSha256([string]$Text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    return [Convert]::ToHexString(
        [System.Security.Cryptography.SHA256]::HashData($bytes)
    ).ToLowerInvariant()
}

$rootCMake = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "CMakeLists.txt")
$cliCMake = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "apps/ure_cli/CMakeLists.txt")
$cliSource = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "apps/ure_cli/src/main.cpp")
$clientHeader = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "libs/ure_client/include/ure/client/client.hpp")
$clientCMake = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "libs/ure_client/CMakeLists.txt")
$workerSource = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "apps/ure_worker/runtime_client.cpp")
$productSchema = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "contracts/schemas/ure_product_v0.fbs")
$manifest = Read-Json "contracts/generated/runtime_manifest_1.json"
$ledger = Read-Json "contracts/product_closure_ledger.json"
$schema = Read-Json "contracts/reports/ure_phase_prv1_validation_v1.schema.json"

if ($rootCMake -notmatch 'add_subdirectory\(libs/ure_client\)' -or
    $cliCMake -notmatch 'target_link_libraries\(ure_cli\s+PRIVATE\s+ure_client\s+ure_config\s*\)' -or
    $cliCMake -match 'ure_core|ure_sceneio') {
    throw "The CLI render target is not exclusively a ure_client/ure_config consumer"
}
foreach ($forbidden in @("RenderEngineFactory", "SceneIR", "ImageSaver", "ure_core", "ure_sceneio")) {
    if ($cliSource -match [regex]::Escape($forbidden)) {
        throw "CLI retained renderer ownership: $forbidden"
    }
}
if ($clientHeader -notmatch 'enum class TransportMode' -or
    $clientHeader -notmatch '\bDirect\b' -or
    $clientHeader -notmatch '\bWorker\b' -or
    $clientCMake -match 'ure_core|ure_sceneio|ure_product') {
    throw "ure_client does not preserve a renderer-free explicit transport boundary"
}

$exportNames = @([regex]::Matches($workerSource, 'GetProcAddress\([^,]+,\s*"(?<name>[^"]+)"') |
    ForEach-Object { $_.Groups['name'].Value } | Sort-Object -Unique)
$expectedExports = @("ureGetRuntimeManifest", "ureQueryInterface")
if ((Compare-Object $exportNames $expectedExports).Count -ne 0 -or
    (Compare-Object @($manifest.loader_exports | Sort-Object) $expectedExports).Count -ne 0) {
    throw "Worker runtime loading is not limited to the two frozen bootstrap exports"
}

foreach ($field in @(
    "determinism_policy", "usage_policy", "output_semantics", "wall_time_budget_ns",
    "memory_budget_bytes", "sample_budget", "latency_budget_ns", "objective_digest"
)) {
    if ($productSchema -notmatch "\b$field\b") {
        throw "ProductJob 0.1 is missing objective field $field"
    }
}
if ($manifest.core_abi.major -ne 1 -or $manifest.core_abi.minor -ne 0 -or
    $manifest.worker_protocol.major -ne 1 -or $manifest.worker_protocol.minor -ne 0) {
    throw "PRV.1 changed the declared Core or Worker major/minor boundary"
}

$report = Read-Json $ReportPath
if ($schema.'$id' -ne "ure.phase_prv1.validation.v1" -or
    $report.schema -ne $schema.'$id' -or
    $report.preview_release_declared -ne $false -or
    $report.contract.core_abi -ne "1.0" -or
    $report.contract.worker_protocol -ne "1.0" -or
    $report.contract.product_extension -ne "0.1" -or
    $report.client.default_transport -ne "Worker" -or
    $report.client.implicit_fallback -ne $false -or
    $report.client.identity_parity -ne $true) {
    throw "PRV.1 validation report identity or bounded claims are invalid"
}
if ($RequireVerifiedReport -and
    ($report.component_state -ne "Verified" -or $report.ctest.full_gate_state -ne "Passed")) {
    throw "PRV.1 validation report is not the post-full-gate report"
}
if (@($report.image_e2e).Count -ne 2 -or
    @($report.image_e2e.transport | Sort-Object) -join ',' -ne "Direct,Worker" -or
    $report.image_e2e[0].sha256 -ne $report.image_e2e[1].sha256 -or
    $report.image_e2e[0].bytes -ne $report.image_e2e[1].bytes -or
    $report.image_e2e[0].bytes -lt 1024) {
    throw "PRV.1 direct/Worker real-image evidence is missing or divergent"
}
$sourceHashes = @{
    product_schema_sha256 = Get-Sha256 "contracts/schemas/ure_product_v0.fbs"
    client_api_sha256 = Get-Sha256 "libs/ure_client/include/ure/client/client.hpp"
    direct_transport_sha256 = Get-Sha256 "libs/ure_client/src/direct_transport.cpp"
    worker_transport_sha256 = Get-Sha256 "libs/ure_client/src/worker_transport.cpp"
    cli_sha256 = Get-Sha256 "apps/ure_cli/src/main.cpp"
    worker_sha256 = Get-Sha256 "apps/ure_worker/runtime_client.cpp"
}
foreach ($pair in $sourceHashes.GetEnumerator()) {
    if ($report.source.($pair.Key) -ne $pair.Value) {
        throw "PRV.1 report source identity drifted: $($pair.Key)"
    }
}
if ($report.source.registry_digest -ne $manifest.registry_digest) {
    throw "PRV.1 report registry identity drifted"
}
$recordedDigest = [string]$report.semantic_digest
$report.semantic_digest = ""
$actualDigest = Get-TextSha256 ($report | ConvertTo-Json -Depth 100 -Compress)
if ($recordedDigest -ne $actualDigest) {
    throw "PRV.1 validation report semantic digest is invalid"
}

$prv1Evidence = @($ledger.entries | Where-Object {
    $_.closure_level -eq "ProductE2E" -and $_.migration_phase -eq "PRV.1"
})
if ($prv1Evidence.Count -lt 4) {
    throw "PRV.1 closure ledger lacks the product service/client spine"
}
foreach ($entry in $prv1Evidence) {
    if (@($entry.evidence | Where-Object {
        $_.kind -eq "ExternalArtifact" -and $_.path -eq "docs/reports/phase_prv1_validation_v1.json"
    }).Count -ne 1) {
        throw "PRV.1 ProductE2E entry lacks the shared validation artifact: $($entry.id)"
    }
}

Write-Output "PRV.1 static audit passed: one product service, explicit Direct/Worker clients, two matching image artifacts"
