param(
    [string]$BuildDir = "build_modular_x64"
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$result = Join-Path $repo "output\benchmarks\phase_r_mlt_suite.json"
& (Join-Path $PSScriptRoot "run_phase_r_mlt_suite.ps1") `
    -BuildDir $BuildDir -Width 4 -Height 4 -CurveSpp 1,2 `
    -ReferenceSpp 8 -ReplicateCount 4 -ReferenceBaseSample 10000 `
    -Scenes sds -MinBenefitScenes 0 -SkipBuild

$report = Get-Content -Raw -LiteralPath $result | ConvertFrom-Json
if ($report.schema -ne "ure.phase_r.mlt_suite.v2" -or
    $report.replicate_count -ne 4) {
    throw "MLT suite does not expose replicated schema metadata"
}
$workload = $report.workloads[0]
if ($workload.reference_shards.Count -ne 4 -or
    $workload.wavefront[0].replicates.Count -ne 4 -or
    $workload.mlt[0].replicates.Count -ne 4) {
    throw "MLT suite did not preserve independent replicate evidence"
}
$referenceBegins = @($workload.reference_shards.sample_begin | Sort-Object -Unique)
$waveBegins = @($workload.wavefront[0].replicates.sample_begin | Sort-Object -Unique)
$mltIdentities = @($workload.mlt[0].replicates.identity_offset | Sort-Object -Unique)
if ($referenceBegins.Count -ne 4 -or $waveBegins.Count -ne 4 -or
    $mltIdentities.Count -ne 4) {
    throw "MLT suite replicate identities are not independent"
}
for ($index = 1; $index -lt $mltIdentities.Count; $index++) {
    if ($mltIdentities[$index] - $mltIdentities[$index - 1] -lt 16) {
        throw "MLT suite replicate chain-identity intervals overlap"
    }
}
Write-Host "Phase R MLT suite replication contract passed"
