param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$CliExecutable = ""
)

$ErrorActionPreference = "Stop"
$reportPath = Join-Path $RepoRoot "docs/reports/ure_preview_baseline_v1.json"
$staticGate = Join-Path $RepoRoot "scripts/check_phase_prv0_static.ps1"

& $staticGate -RepoRoot $RepoRoot -ReportPath $reportPath | Out-Null
$report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json -Depth 100
if ($report.preview_release_declared -ne $false -or
    $report.scenarios.product_e2e_now -ne 0 -or
    $report.cli_image_evidence.closure -ne "RendererIntegrated" -or
    $report.cli_image_evidence.calling_mode -ne "legacy_cli_direct_renderer") {
    throw "The read-only PRV.0 baseline no longer describes its historical boundary"
}
if (-not (Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt"))) {
    throw "The configured build used by the PRV.0 historical gate is missing"
}
Write-Output "PRV.0 read-only baseline contract gate passed"
