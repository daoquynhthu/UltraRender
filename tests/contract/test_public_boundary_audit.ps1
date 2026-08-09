param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = (Join-Path $RepoRoot "build_modular_x64")
)

$ErrorActionPreference = "Stop"
$audit = Join-Path $RepoRoot "scripts/audit_public_boundary.ps1"
$pwsh = Join-Path $PSHOME "pwsh.exe"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("ure_pb0_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temp | Out-Null

function Write-Json {
    param($Value, [string]$Path)
    Set-Content -LiteralPath $Path -Value ($Value | ConvertTo-Json -Depth 100) -Encoding utf8
}

function Invoke-ExpectedFailure {
    param([string[]]$Arguments, [string]$Pattern, [string]$Label)
    $output = & $pwsh -NoProfile -File $audit -RepoRoot $RepoRoot -BuildDir $BuildDir @Arguments 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        throw "$Label unexpectedly passed"
    }
    if ($output -notmatch $Pattern) {
        throw "$Label failed for the wrong reason: $output"
    }
}

try {
    $reportA = Join-Path $temp "report_a.json"
    $reportB = Join-Path $temp "report_b.json"
    & $audit -RepoRoot $RepoRoot -BuildDir $BuildDir -ReportPath $reportA
    & $audit -RepoRoot $RepoRoot -BuildDir $BuildDir -ReportPath $reportB
    if ((Get-FileHash -LiteralPath $reportA -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $reportB -Algorithm SHA256).Hash) {
        throw "PB.0 audit reports are not deterministic"
    }
    $report = Get-Content -LiteralPath $reportA -Raw | ConvertFrom-Json -Depth 100
    if ($report.schema -ne "ure.pb.boundary-audit/1.0" -or
        [int]$report.unresolved_classification_count -ne 0 -or
        [int]$report.duplicate_authority_count -ne 0 -or
        [int]$report.forbidden_inspection_count -ne 0 -or
        [int]$report.forbidden_public_header_count -ne 0 -or
        [int]$report.registry_entry_count -ne 55 -or
        [int]$report.legacy.intended_c_export_count -ne 55 -or
        [int]$report.legacy.client_fixture_exit_code -ne 0) {
        throw "PB.0 audit report does not preserve the closure evidence"
    }

    $ledgerSource = Join-Path $RepoRoot "contracts/public_interaction_surface_ledger.json"
    $duplicateIdPath = Join-Path $temp "duplicate_id.json"
    $ledger = Get-Content -LiteralPath $ledgerSource -Raw | ConvertFrom-Json -Depth 100
    $ledger.entries[1].id = $ledger.entries[0].id
    Write-Json $ledger $duplicateIdPath
    Invoke-ExpectedFailure @("-LedgerPath", $duplicateIdPath) "nonempty and unique" "Duplicate surface ID fixture"

    $duplicateAuthorityPath = Join-Path $temp "duplicate_authority.json"
    $ledger = Get-Content -LiteralPath $ledgerSource -Raw | ConvertFrom-Json -Depth 100
    $ledger.entries[1].authority_claims = @("client_interaction")
    Write-Json $ledger $duplicateAuthorityPath
    Invoke-ExpectedFailure @("-LedgerPath", $duplicateAuthorityPath) "Duplicate authority" "Duplicate authority fixture"

    $missingSurfacePath = Join-Path $temp "missing_surface.json"
    $ledger = Get-Content -LiteralPath $ledgerSource -Raw | ConvertFrom-Json -Depth 100
    $ledger.entries = @($ledger.entries | Select-Object -Skip 1)
    Write-Json $ledger $missingSurfacePath
    Invoke-ExpectedFailure @("-LedgerPath", $missingSurfacePath) "does not exactly cover" "Missing surface fixture"

    $forbiddenAnchorPath = Join-Path $temp "forbidden_anchor.json"
    $ledger = Get-Content -LiteralPath $ledgerSource -Raw | ConvertFrom-Json -Depth 100
    $ledger.entries[0].anchors = @([pscustomobject]@{path = "gui/README.md"; pattern = "."})
    Write-Json $ledger $forbiddenAnchorPath
    Invoke-ExpectedFailure @("-LedgerPath", $forbiddenAnchorPath) "forbidden root" "Forbidden GUI inspection fixture"

    $registrySource = Join-Path $RepoRoot "contracts/registry/public_contract_registry.json"
    $floatingRegistryPath = Join-Path $temp "floating_registry.json"
    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry | Add-Member -NotePropertyName forbidden_fraction -NotePropertyValue 0.5
    Write-Json $registry $floatingRegistryPath
    Invoke-ExpectedFailure @("-RegistryPath", $floatingRegistryPath) "floating-point" "Floating registry fixture"

    $forbiddenHeaderRoot = Join-Path $temp "forbidden_public_header"
    New-Item -ItemType Directory -Path $forbiddenHeaderRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $forbiddenHeaderRoot "bad.h") -Value "std::vector<int> leaked;" -Encoding utf8
    $forbiddenHeaderRegistryPath = Join-Path $temp "forbidden_header_registry.json"
    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry.public_header_policy.roots = @(
        [pscustomobject]@{path = $forbiddenHeaderRoot; required_from_phase = "PB.0-test"})
    Write-Json $registry $forbiddenHeaderRegistryPath
    Invoke-ExpectedFailure @("-RegistryPath", $forbiddenHeaderRegistryPath) "forbidden token cpp_stl" "Forbidden public header fixture"

    $legacySource = Join-Path $RepoRoot "tests/fixtures/contracts/registry/legacy_surface.json"
    $extraExportPath = Join-Path $temp "extra_export.json"
    $legacy = Get-Content -LiteralPath $legacySource -Raw | ConvertFrom-Json -Depth 100
    $legacy.intended_c_exports[0] = "ure_injected_extra_export"
    Write-Json $legacy $extraExportPath
    Invoke-ExpectedFailure @("-LegacySurfacePath", $extraExportPath) "no longer declares" "Injected export fixture"

    Write-Host "PB.0 public boundary audit contract passed."
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
