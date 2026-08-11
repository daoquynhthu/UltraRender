param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = "Stop"
$checker = Join-Path $RepoRoot "scripts/check_phase_prv0_static.ps1"
$closurePath = Join-Path $RepoRoot "contracts/product_closure_ledger.json"
$semanticPath = Join-Path $RepoRoot "contracts/product_semantic_audit.json"
$scenarioPath = Join-Path $RepoRoot "contracts/product_e2e_scenarios.json"
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ure_prv0_" + [guid]::NewGuid().ToString("N"))

function Read-Json([string]$Path) {
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json -Depth 100
}

function Save-Json($Value, [string]$Path) {
    $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM
}

function Expect-Failure([string]$Label, [scriptblock]$Mutation, [ValidateSet("Closure", "Semantic", "Scenario")][string]$Kind) {
    $closureCopy = Join-Path $temporary "closure.json"
    $semanticCopy = Join-Path $temporary "semantic.json"
    $scenarioCopy = Join-Path $temporary "scenario.json"
    Copy-Item -LiteralPath $closurePath -Destination $closureCopy -Force
    Copy-Item -LiteralPath $semanticPath -Destination $semanticCopy -Force
    Copy-Item -LiteralPath $scenarioPath -Destination $scenarioCopy -Force
    $target = switch ($Kind) {
        "Closure" { $closureCopy }
        "Semantic" { $semanticCopy }
        "Scenario" { $scenarioCopy }
    }
    $document = Read-Json $target
    & $Mutation $document
    Save-Json $document $target
    $failed = $false
    try {
        & $checker -RepoRoot $RepoRoot -ClosureLedgerPath $closureCopy -SemanticAuditPath $semanticCopy -ScenarioManifestPath $scenarioCopy *> $null
    } catch {
        $failed = $true
    }
    if (-not $failed) {
        throw "PRV.0 negative fixture was accepted: $Label"
    }
}

New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    $first = (& $checker -RepoRoot $RepoRoot | Out-String).Trim()
    $second = (& $checker -RepoRoot $RepoRoot | Out-String).Trim()
    if ($first -ne $second) {
        throw "PRV.0 static audit output is not deterministic"
    }

    Expect-Failure "unknown owner" { param($j) $j.entries[0].owner = "UnknownOwner" } Closure
    Expect-Failure "duplicate execution authority" {
        param($j)
        $j.entries[1].authority_claim = $j.entries[0].authority_claim
    } Closure
    Expect-Failure "expired PB terminal gate" { param($j) $j.entries[2].terminal_gate = "PB.8" } Closure
    Expect-Failure "unsupported ProductE2E claim" {
        param($j)
        $entry = $j.entries | Where-Object id -eq "materialx_adapter"
        $entry.closure_level = "ProductE2E"
    } Closure
    Expect-Failure "forbidden GUI anchor" { param($j) $j.entries[0].anchors[0] = "gui/legacy.cpp" } Closure
    Expect-Failure "missing semantic migration" {
        param($j)
        $entry = $j.entries | Where-Object current_behavior -eq "ExecutedWithSemanticDebt" | Select-Object -First 1
        $entry.migration_phase = ""
    } Semantic
    Expect-Failure "guarded source drift" { param($j) $j.guarded_sources[0].sha256 = ("0" * 64) } Semantic
    Expect-Failure "missing scenario dimension" {
        param($j)
        $j.scenarios[0].dimensions.client = ""
    } Scenario
    Expect-Failure "scenario source drift" { param($j) $j.scenarios[0].source.sha256 = ("0" * 64) } Scenario
    Expect-Failure "unknown scenario capability" { param($j) $j.scenarios[0].required_capabilities[0] = "unknown_capability" } Scenario

    Write-Output "PRV.0 static positive, deterministic, and negative-fixture gates passed"
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
