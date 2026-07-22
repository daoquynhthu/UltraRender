param(
    [Parameter(Mandatory = $true)]
    [string]$CsvPath,
    [Parameter(Mandatory = $true)]
    [string]$VramEvidencePath,
    [string]$OutputPath = "output\benchmarks\phase_r_nsight_evidence.json",
    [string]$ToolVersion = "unknown"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$resolvedCsv = Resolve-Path -LiteralPath $CsvPath
$resolvedVram = Resolve-Path -LiteralPath $VramEvidencePath
$vram = Get-Content -Raw -LiteralPath $resolvedVram | ConvertFrom-Json
if ($vram.schema -ne "ure.phase_r.vram_evidence.v1" -or
    $vram.status -ne "collected" -or
    $vram.measurement -notin @("device_used_delta", "process_used_memory") -or
    $vram.executable_sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
    [uint64]$vram.peak_vram_bytes -eq 0) {
    throw "invalid measured VRAM evidence"
}
$rows = @(Import-Csv -LiteralPath $resolvedCsv)
if ($rows.Count -eq 0) { throw "Nsight CSV is empty" }

$kernelColumn = @("Kernel Name", "Kernel Name (Demangled)", "Kernel") | Where-Object { $_ -in $rows[0].PSObject.Properties.Name } | Select-Object -First 1
$metricColumn = @("Metric Name", "Metric") | Where-Object { $_ -in $rows[0].PSObject.Properties.Name } | Select-Object -First 1
$valueColumn = @("Metric Value", "Value") | Where-Object { $_ -in $rows[0].PSObject.Properties.Name } | Select-Object -First 1
if ($null -eq $kernelColumn -or $null -eq $metricColumn -or $null -eq $valueColumn) {
    throw "Nsight CSV does not expose kernel, metric, and value columns"
}

$occupancyMetric = "sm__warps_active.avg.pct_of_peak_sustained_active"
$kernels = @()
foreach ($group in $rows | Group-Object { $_.$kernelColumn }) {
    if ([string]::IsNullOrWhiteSpace($group.Name)) { continue }
    $occupancyRows = @($group.Group | Where-Object { $_.$metricColumn -eq $occupancyMetric })
    if ($occupancyRows.Count -eq 0) { throw "missing achieved occupancy for kernel $($group.Name)" }
    $values = @($occupancyRows | ForEach-Object {
        [double](($_.$valueColumn -replace ',', '') -replace '%', '')
    })
    $kernels += [ordered]@{
        name = $group.Name
        launches = $occupancyRows.Count
        occupancy_pct = [Math]::Round(($values | Measure-Object -Average).Average, 6)
    }
}
if ($kernels.Count -eq 0) { throw "Nsight CSV contains no kernel evidence" }
$totalKernelLaunches = 0
foreach ($kernel in $kernels) { $totalKernelLaunches += [int]$kernel.launches }

$destination = if ([System.IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $RepoRoot $OutputPath }
New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
$report = [ordered]@{
    schema = "ure.phase_r.nsight_evidence.v1"
    status = "collected"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    tool_version = $ToolVersion
    source = $resolvedCsv.Path
    source_sha256 = (Get-FileHash -LiteralPath $resolvedCsv -Algorithm SHA256).Hash.ToLowerInvariant()
    peak_vram_bytes = [uint64]$vram.peak_vram_bytes
    vram_source = $resolvedVram.Path
    vram_source_sha256 = (Get-FileHash -LiteralPath $resolvedVram -Algorithm SHA256).Hash.ToLowerInvariant()
    vram_measurement = $vram.measurement
    profiled_executable_sha256 = $vram.executable_sha256
    total_kernel_launches = $totalKernelLaunches
    kernels = $kernels
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $destination -Encoding utf8
Write-Host "Wrote $destination"
