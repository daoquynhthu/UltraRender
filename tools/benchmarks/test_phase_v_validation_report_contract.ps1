param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath
)

$ErrorActionPreference = "Stop"
$Validator = Join-Path $PSScriptRoot `
    "validate_phase_v_validation_report.ps1"
$ResolvedReport = Resolve-Path -LiteralPath $ReportPath
$TemporaryRoot = Join-Path (
    [System.IO.Path]::GetTempPath()) (
    "ure-phase-v-contract-" +
    [guid]::NewGuid().ToString("N"))

function Write-Fixture {
    param(
        [object]$Value,
        [string]$Name
    )
    $Path = Join-Path $TemporaryRoot $Name
    $Value | ConvertTo-Json -Depth 24 |
        Set-Content -LiteralPath $Path -Encoding utf8
    $Path
}

function Assert-Rejected {
    param(
        [scriptblock]$Mutation,
        [string]$Name
    )
    $Fixture =
        Get-Content -Raw -LiteralPath $ResolvedReport |
        ConvertFrom-Json
    & $Mutation $Fixture
    $Path = Write-Fixture $Fixture "$Name.json"
    try {
        & $Validator -ReportPath $Path *>$null
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
        $report.evidence.build_telemetry[0].
            large_mesh.trace_mrays_per_second[0] = 0
    } "throughput"
    Assert-Rejected {
        param($report)
        $report.thresholds.dense_build_ms_max[0] = 1.0e9
    } "threshold"
    Assert-Rejected {
        param($report)
        $report.evidence.dynamic_geometry[0].
            operations.blas_rebuild = 0
    } "dynamic"
    Assert-Rejected {
        param($report)
        $report.evidence.backend_parity.
            assertions.result = "failed"
    } "parity"
    Assert-Rejected {
        param($report)
        $report.evidence.distributed_shards.
            inventory.workers[1].sample_start = 0
    } "shard_overlap"
    Write-Host "Phase V validation report negative contract passed"
} finally {
    Remove-Item -LiteralPath $TemporaryRoot `
        -Recurse -Force -ErrorAction SilentlyContinue
}
