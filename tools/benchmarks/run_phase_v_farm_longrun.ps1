param(
    [Parameter(Mandatory = $true)]
    [string]$RunId,
    [Parameter(Mandatory = $true)]
    [uint32]$ShardIndex,
    [Parameter(Mandatory = $true)]
    [uint32]$ShardCount,
    [Parameter(Mandatory = $true)]
    [uint64]$SampleStart,
    [Parameter(Mandatory = $true)]
    [uint64]$SampleCount,
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [ValidateRange(3, 32)]
    [int]$Repetitions = 5,
    [string]$OutputPath = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ($ShardCount -eq 0 -or
    $ShardIndex -ge $ShardCount -or
    $SampleCount -eq 0) {
    throw "invalid Phase V farm shard"
}
if (-not [string]::IsNullOrWhiteSpace(
        (& git -C $RepoRoot status --porcelain |
            Out-String))) {
    throw "Phase V farm long-run requires a clean source tree"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath =
        "output/benchmarks/phase_v_farm/" +
        "$RunId/shard_$ShardIndex.json"
}

& (Join-Path $RepoRoot `
    "scripts\run_phase_v_validation_suite.ps1") `
    -BuildDir $BuildDir `
    -Config $Config `
    -Profile Farm `
    -Repetitions $Repetitions `
    -OutputPath $OutputPath `
    -RunId $RunId `
    -ShardIndex $ShardIndex `
    -ShardCount $ShardCount `
    -SampleStart $SampleStart `
    -SampleCount $SampleCount `
    -SkipBuild:$SkipBuild
if ($LASTEXITCODE -ne 0) {
    throw "Phase V farm long-run failed"
}
Write-Host "Phase V farm shard passed: $OutputPath"
