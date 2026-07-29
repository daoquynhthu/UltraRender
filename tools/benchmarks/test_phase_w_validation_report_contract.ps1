param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath
)

$ErrorActionPreference = "Stop"
$Validator = Join-Path $PSScriptRoot `
    "validate_phase_w_validation_report.ps1"
$ResolvedReport = Resolve-Path -LiteralPath $ReportPath
$TemporaryRoot = Join-Path (
    [System.IO.Path]::GetTempPath()) (
    "ure-phase-w-contract-" +
    [guid]::NewGuid().ToString("N"))

function Write-Fixture {
    param(
        [object]$Value,
        [string]$Name
    )
    $path = Join-Path $TemporaryRoot "$Name.json"
    $Value | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $path `
            -Encoding utf8
    $path
}

function Assert-Rejected {
    param(
        [scriptblock]$Mutation,
        [string]$Name
    )
    $fixture =
        Get-Content -Raw -LiteralPath $ResolvedReport |
        ConvertFrom-Json
    & $Mutation $fixture
    $path = Write-Fixture $fixture $Name
    try {
        & $Validator -ReportPath $path *>$null
    } catch {
        return
    }
    throw "validator accepted invalid fixture: $Name"
}

New-Item -ItemType Directory -Force `
    -Path $TemporaryRoot | Out-Null
try {
    & $Validator -ReportPath $ResolvedReport
    Assert-Rejected {
        param($report)
        $report.schema = "invalid"
    } "schema"
    Assert-Rejected {
        param($report)
        $report.artifacts.wave_gpu_sha256 = "00"
    } "artifact_digest"
    Assert-Rejected {
        param($report)
        $report.evidence.thin_film_complex_phase.
            status = "failed"
    } "physical_evidence"
    Assert-Rejected {
        param($report)
        $report.test_gate.failed = 1
    } "ctest"
    Assert-Rejected {
        param($report)
        $report.static_gates.phase_w_12 = $false
    } "static_gate"
    Write-Host (
        "Phase W validation report negative " +
        "contract passed.")
} finally {
    Remove-Item -LiteralPath $TemporaryRoot `
        -Recurse -Force `
        -ErrorAction SilentlyContinue
}
