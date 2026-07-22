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
$occupancyMetric = "sm__warps_active.avg.pct_of_peak_sustained_active"
$csvLines = @(Get-Content -LiteralPath $resolvedCsv)
$headerIndex = -1
for ($index = 0; $index -lt $csvLines.Count; ++$index) {
    if ($csvLines[$index] -match '(?:^|,)"?Kernel Name"?(?:,|$)' -and
        ($csvLines[$index] -match '(?:^|,)"?Metric Name"?(?:,|$)' -or
         $csvLines[$index] -like "*$occupancyMetric*")) {
        $headerIndex = $index
        break
    }
}
if ($headerIndex -lt 0) { throw "Nsight CSV header was not found" }
$rows = @($csvLines[$headerIndex..($csvLines.Count - 1)] | ConvertFrom-Csv)
if ($rows.Count -eq 0) { throw "Nsight CSV is empty" }

$kernelColumn = @("Kernel Name", "Kernel Name (Demangled)", "Kernel") | Where-Object { $_ -in $rows[0].PSObject.Properties.Name } | Select-Object -First 1
$metricColumn = @("Metric Name", "Metric") | Where-Object { $_ -in $rows[0].PSObject.Properties.Name } | Select-Object -First 1
$valueColumn = @("Metric Value", "Value") | Where-Object { $_ -in $rows[0].PSObject.Properties.Name } | Select-Object -First 1
$wideOccupancyColumn = $occupancyMetric | Where-Object { $_ -in $rows[0].PSObject.Properties.Name }
if ($null -eq $kernelColumn -or
    (($null -eq $metricColumn -or $null -eq $valueColumn) -and
     $null -eq $wideOccupancyColumn)) {
    throw "Nsight CSV does not expose a supported kernel/occupancy layout"
}

$kernels = @()
foreach ($group in $rows | Group-Object { $_.$kernelColumn }) {
    if ([string]::IsNullOrWhiteSpace($group.Name)) { continue }
    $occupancyRows = if ($null -ne $wideOccupancyColumn) {
        @($group.Group | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_.$wideOccupancyColumn)
        })
    } else {
        @($group.Group | Where-Object { $_.$metricColumn -eq $occupancyMetric })
    }
    if ($occupancyRows.Count -eq 0) { throw "missing achieved occupancy for kernel $($group.Name)" }
    $values = @($occupancyRows | ForEach-Object {
        $value = if ($null -ne $wideOccupancyColumn) {
            $_.$wideOccupancyColumn
        } else {
            $_.$valueColumn
        }
        [double](($value -replace ',', '') -replace '%', '')
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
