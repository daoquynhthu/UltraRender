param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [string[]]$ArgumentList = @(),
    [int]$DeviceIndex = 0,
    [int]$PollMilliseconds = 100,
    [string]$OutputPath = "output\benchmarks\phase_r_vram_evidence.json"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ($PollMilliseconds -lt 20 -or $PollMilliseconds -gt 5000) {
    throw "PollMilliseconds must be in [20, 5000]"
}
$resolvedExecutable = Resolve-Path -LiteralPath $Executable
$nvidiaSmi = (Get-Command nvidia-smi.exe -ErrorAction Stop).Source
$readDeviceBytes = {
    $rows = & $nvidiaSmi --query-gpu=index,memory.used --format=csv,noheader,nounits 2>$null
    foreach ($row in $rows) {
        $parts = $row -split ',' | ForEach-Object { $_.Trim() }
        if ($parts.Count -eq 2 -and [int]$parts[0] -eq $DeviceIndex) {
            return [uint64]$parts[1] * 1MB
        }
    }
    throw "GPU device $DeviceIndex was not reported by nvidia-smi"
}
[uint64]$baselineBytes = & $readDeviceBytes
$process = Start-Process -FilePath $resolvedExecutable -ArgumentList $ArgumentList -PassThru -NoNewWindow
[uint64]$peakDeviceBytes = $baselineBytes
$sampleCount = 0
try {
    while (-not $process.HasExited) {
        [uint64]$bytes = & $readDeviceBytes
        if ($bytes -gt $peakDeviceBytes) { $peakDeviceBytes = $bytes }
        ++$sampleCount
        Start-Sleep -Milliseconds $PollMilliseconds
        $process.Refresh()
    }
    $process.WaitForExit()
} finally {
    if (-not $process.HasExited) { $process.Kill($true) }
}
if ($process.ExitCode -ne 0) { throw "profiled process failed with exit code $($process.ExitCode)" }
[uint64]$peakBytes = $peakDeviceBytes - $baselineBytes
if ($peakBytes -eq 0 -or $sampleCount -eq 0) { throw "no positive VRAM delta was collected" }

$destination = if ([System.IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $RepoRoot $OutputPath }
New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
[ordered]@{
    schema = "ure.phase_r.vram_evidence.v1"
    status = "collected"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    executable = $resolvedExecutable.Path
    executable_sha256 = (Get-FileHash -LiteralPath $resolvedExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    process_exit_code = $process.ExitCode
    measurement = "device_used_delta"
    device_index = $DeviceIndex
    poll_milliseconds = $PollMilliseconds
    sample_count = $sampleCount
    baseline_device_vram_bytes = $baselineBytes
    peak_device_vram_bytes = $peakDeviceBytes
    peak_vram_bytes = $peakBytes
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $destination -Encoding utf8
Write-Host "Wrote $destination"
