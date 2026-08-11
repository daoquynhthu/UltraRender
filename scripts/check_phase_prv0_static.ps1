param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$ClosureLedgerPath = "",
    [string]$SemanticAuditPath = "",
    [string]$ScenarioManifestPath = "",
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-InputPath([string]$Candidate, [string]$DefaultRelative) {
    if ([string]::IsNullOrWhiteSpace($Candidate)) {
        return Join-Path $RepoRoot $DefaultRelative
    }
    if ([System.IO.Path]::IsPathRooted($Candidate)) {
        return $Candidate
    }
    return Join-Path $RepoRoot $Candidate
}

function Read-Json([string]$Path) {
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json -Depth 100
}

function Get-TextSha256([string]$Text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    return [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Assert-UniqueIds($Items, [string]$Label) {
    $ids = @($Items | ForEach-Object { [string]$_.id })
    if ($ids.Count -ne @($ids | Sort-Object -Unique).Count -or $ids -contains "") {
        throw "$Label IDs must be nonempty and unique"
    }
}

function Assert-RepositoryFile([string]$Relative, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [System.IO.Path]::IsPathRooted($Relative)) {
        throw "$Label must use a nonempty repository-relative path"
    }
    $normalized = $Relative.Replace('\', '/')
    if ($normalized -match '(^|/)gui(/|$)') {
        throw "$Label uses the forbidden repository GUI anchor: $Relative"
    }
    $full = Join-Path $RepoRoot $Relative
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "$Label references a missing file: $Relative"
    }
    return $full
}

function Assert-RequiredIds($Items, $Required, [string]$Label) {
    $actual = @($Items | ForEach-Object { [string]$_.id })
    $missing = @($Required | Where-Object { $actual -notcontains [string]$_ })
    if ($missing.Count -ne 0) {
        throw "$Label is missing required IDs: $($missing -join ', ')"
    }
}

$closurePath = Resolve-InputPath $ClosureLedgerPath "contracts/product_closure_ledger.json"
$semanticPath = Resolve-InputPath $SemanticAuditPath "contracts/product_semantic_audit.json"
$scenarioPath = Resolve-InputPath $ScenarioManifestPath "contracts/product_e2e_scenarios.json"
$closure = Read-Json $closurePath
$semantic = Read-Json $semanticPath
$scenarios = Read-Json $scenarioPath

if ($closure.schema -ne "ure.preview.product-closure-ledger/1.0") {
    throw "Unexpected product closure ledger schema"
}
$sourceLedgerPath = Assert-RepositoryFile $closure.source_ledger "Closure source ledger"
$sourceLedger = Read-Json $sourceLedgerPath
if ($sourceLedger.schema -ne $closure.source_ledger_schema) {
    throw "The product ledger source schema does not match the preserved PB ledger"
}
Assert-UniqueIds $closure.entries "Product closure entry"
Assert-RequiredIds $closure.entries $closure.required_capabilities "Product closure ledger"

$authorityClaims = [System.Collections.Generic.List[string]]::new()
foreach ($entry in $closure.entries) {
    if ($closure.allowed_owners -notcontains $entry.owner) {
        throw "Closure entry $($entry.id) has unknown owner $($entry.owner)"
    }
    if ($closure.allowed_closure_levels -notcontains $entry.closure_level -or
        $closure.allowed_preview_dispositions -notcontains $entry.preview_disposition -or
        $closure.allowed_bypass_status -notcontains $entry.bypass_status) {
        throw "Closure entry $($entry.id) has an invalid level, disposition, or bypass state"
    }
    foreach ($field in @("current_call_chain", "current_behavior", "ignored_semantics_risk", "migration_phase", "terminal_gate")) {
        if ([string]::IsNullOrWhiteSpace([string]$entry.$field)) {
            throw "Closure entry $($entry.id) is missing $field"
        }
    }
    if ([string]$entry.terminal_gate -match '^PB\.' -or [string]$entry.terminal_gate -eq "PostPB") {
        throw "Closure entry $($entry.id) retains an expired PB terminal gate"
    }
    if ($entry.preview_disposition -eq "MustConverge" -and $entry.migration_phase -notmatch '^PRV\.[0-9]+$') {
        throw "Closure entry $($entry.id) must converge but has no PRV migration phase"
    }
    foreach ($anchor in @($entry.anchors)) {
        [void](Assert-RepositoryFile $anchor "Closure entry $($entry.id) anchor")
    }
    foreach ($evidence in @($entry.evidence)) {
        if ([string]::IsNullOrWhiteSpace([string]$evidence.kind)) {
            throw "Closure entry $($entry.id) has evidence without a kind"
        }
        [void](Assert-RepositoryFile $evidence.path "Closure entry $($entry.id) evidence")
    }
    if ($entry.closure_level -eq "ProductE2E" -and
        @($entry.evidence | Where-Object kind -eq "ExternalArtifact").Count -eq 0) {
        throw "Closure entry $($entry.id) claims ProductE2E without an external artifact"
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$entry.authority_claim)) {
        $authorityClaims.Add([string]$entry.authority_claim)
    }
}
if ($authorityClaims.Count -ne @($authorityClaims | Sort-Object -Unique).Count) {
    throw "The product closure ledger contains duplicate execution authority claims"
}
$missingAuthorityClaims = @($closure.required_authority_claims | Where-Object { $authorityClaims -notcontains [string]$_ })
if ($missingAuthorityClaims.Count -ne 0 -or $authorityClaims.Count -ne @($closure.required_authority_claims).Count) {
    throw "The product closure ledger authority claims are incomplete or unclassified"
}

$requiredBypass = @{
    cli_render = "ProductServiceBypass"
    cpp_session = "ProductServiceBypass"
    legacy_python = "ProductServiceBypass"
    hydra = "ProductServiceBypass"
}
foreach ($pair in $requiredBypass.GetEnumerator()) {
    $entry = $closure.entries | Where-Object id -eq $pair.Key
    if ($entry.bypass_status -ne $pair.Value -or $entry.preview_disposition -notin @("MustConverge", "Migrate")) {
        throw "$($pair.Key) is not honestly classified as an unmerged product bypass"
    }
}

if ($semantic.schema -ne "ure.preview.semantic-audit/1.0") {
    throw "Unexpected product semantic audit schema"
}
Assert-UniqueIds $semantic.entries "Semantic audit entry"
Assert-RequiredIds $semantic.entries $semantic.required_semantics "Semantic audit"
foreach ($guard in $semantic.guarded_sources) {
    $full = Assert-RepositoryFile $guard.path "Semantic guard"
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $full).Hash.ToLowerInvariant()
    if ($actualHash -ne ([string]$guard.sha256).ToLowerInvariant()) {
        throw "Guarded semantic source changed without an audit update: $($guard.path)"
    }
}
foreach ($entry in $semantic.entries) {
    if ($semantic.allowed_current_behaviors -notcontains $entry.current_behavior -or
        $semantic.allowed_target_dispositions -notcontains $entry.target_disposition) {
        throw "Semantic entry $($entry.id) has an invalid current or target disposition"
    }
    foreach ($field in @("owner", "input_surface", "consumer", "impact", "migration_phase")) {
        if ([string]::IsNullOrWhiteSpace([string]$entry.$field)) {
            throw "Semantic entry $($entry.id) is missing $field"
        }
    }
    if ($entry.current_behavior -in @("AcceptedButIgnored", "ExecutedWithSemanticDebt") -and
        $entry.migration_phase -notmatch '^PRV\.[0-9]+$') {
        throw "Semantic debt $($entry.id) has no PRV migration phase"
    }
    $full = Assert-RepositoryFile $entry.source.path "Semantic entry $($entry.id) source"
    $text = Get-Content -Raw -LiteralPath $full
    if (-not [regex]::IsMatch($text, [string]$entry.source.pattern)) {
        throw "Semantic entry $($entry.id) source pattern is absent"
    }
}

if ($scenarios.schema -ne "ure.preview.e2e-scenario-manifest/1.0") {
    throw "Unexpected product E2E scenario schema"
}
Assert-UniqueIds $scenarios.scenarios "Product scenario"
Assert-RequiredIds $scenarios.scenarios $scenarios.required_scenarios "Product scenario manifest"
$closureIds = @($closure.entries | ForEach-Object id)
foreach ($scenario in $scenarios.scenarios) {
    $source = Assert-RepositoryFile $scenario.source.path "Scenario $($scenario.id) source"
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant()
    if ($actualHash -ne ([string]$scenario.source.sha256).ToLowerInvariant()) {
        throw "Scenario source hash drifted: $($scenario.id)"
    }
    foreach ($dimension in $scenarios.required_dimensions) {
        if ([string]::IsNullOrWhiteSpace([string]$scenario.dimensions.$dimension)) {
            throw "Scenario $($scenario.id) is missing dimension $dimension"
        }
    }
    if (@($scenario.required_capabilities).Count -eq 0 -or
        @($scenario.required_capabilities | Where-Object { $closureIds -notcontains [string]$_ }).Count -ne 0) {
        throw "Scenario $($scenario.id) has missing or unknown required capabilities"
    }
    if ($scenarios.artifact_schemas -notcontains $scenario.artifact_schema -or
        $scenario.expected_disposition -ne "ProductE2E" -or
        $scenario.target_phase -notmatch '^PRV\.[0-9]+$' -or
        @($scenario.metrics).Count -lt 3) {
        throw "Scenario $($scenario.id) lacks a Preview target, artifact schema, or metrics"
    }
    foreach ($failure in @($scenario.acceptable_failures)) {
        if ($scenarios.allowed_failure_classes -notcontains $failure) {
            throw "Scenario $($scenario.id) has unknown acceptable failure class $failure"
        }
    }
}

$reportSchemaPath = Join-Path $RepoRoot "contracts/reports/ure_preview_baseline_v1.schema.json"
$reportSchema = Read-Json $reportSchemaPath
if ($reportSchema.'$id' -ne "ure.preview.baseline.v1") {
    throw "Unexpected Preview baseline report schema"
}
if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    $resolvedReport = Resolve-InputPath $ReportPath $ReportPath
    $report = Read-Json $resolvedReport
    if ($report.schema -ne "ure.preview.baseline.v1" -or $report.preview_release_declared -ne $false) {
        throw "Preview baseline report identity or release declaration is invalid"
    }
    if ($report.closure.entry_count -ne @($closure.entries).Count -or
        $report.semantic_audit.entry_count -ne @($semantic.entries).Count -or
        $report.scenarios.count -ne @($scenarios.scenarios).Count -or
        $report.scenarios.product_e2e_now -ne 0) {
        throw "Preview baseline report counts drifted from the source contracts"
    }
    $expectedContractHashes = @{
        closure_ledger_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $closurePath).Hash.ToLowerInvariant()
        semantic_audit_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $semanticPath).Hash.ToLowerInvariant()
        scenario_manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $scenarioPath).Hash.ToLowerInvariant()
        schema_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $reportSchemaPath).Hash.ToLowerInvariant()
    }
    foreach ($pair in $expectedContractHashes.GetEnumerator()) {
        if ($report.contracts.($pair.Key) -ne $pair.Value) {
            throw "Preview baseline report contract digest drifted: $($pair.Key)"
        }
    }
    if ($report.source.commit -notmatch '^[0-9a-f]{40}$' -or
        $report.source.tree_sha256 -notmatch '^[0-9a-f]{64}$' -or
        $report.ctest.inventory_sha256 -notmatch '^[0-9a-f]{64}$' -or
        $report.ctest.full_gate_state -notin @("NotRun", "Passed", "Failed")) {
        throw "Preview baseline report source or CTest identity is malformed"
    }
    $recordedDigest = [string]$report.semantic_digest
    $report.semantic_digest = ""
    $actualDigest = Get-TextSha256 ($report | ConvertTo-Json -Depth 100 -Compress)
    $report.semantic_digest = $recordedDigest
    if ($recordedDigest -ne $actualDigest) {
        throw "Preview baseline semantic digest is invalid"
    }
}

Write-Output "PRV.0 static audit passed: $(@($closure.entries).Count) closure entries, $(@($semantic.entries).Count) semantics, $(@($scenarios.scenarios).Count) retained scenarios"
