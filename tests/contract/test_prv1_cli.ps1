param(
    [Parameter(Mandatory = $true)][string]$Cli,
    [Parameter(Mandatory = $true)][string]$Runtime,
    [Parameter(Mandatory = $true)][string]$Worker,
    [Parameter(Mandatory = $true)][string]$Scene
)

$ErrorActionPreference = "Stop"

function Invoke-Cli {
    param([string[]]$Arguments)
    $output = & $Cli @Arguments 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

$common = @("render", $Scene, "--spp", "2", "--runtime", $Runtime, "--worker", $Worker)
$workerResult = Invoke-Cli $common
if ($workerResult.ExitCode -ne 0) {
    throw "Worker CLI render failed: $($workerResult.Output)"
}
$directResult = Invoke-Cli ($common + @("--transport", "direct"))
if ($directResult.ExitCode -ne 0) {
    throw "Direct CLI render failed: $($directResult.Output)"
}

$keys = @("accepted_samples", "frame", "frame_bytes", "build_identity", "snapshot_identity", "objective_identity", "plan_identity", "frame_content_identity")
foreach ($key in $keys) {
    $workerValue = [regex]::Match($workerResult.Output, "(?m)^$key=(.+)$").Groups[1].Value.Trim()
    $directValue = [regex]::Match($directResult.Output, "(?m)^$key=(.+)$").Groups[1].Value.Trim()
    if ([string]::IsNullOrWhiteSpace($workerValue) -or $workerValue -ne $directValue) {
        throw "CLI transport parity failed for $key"
    }
}
if ($workerResult.Output -notmatch "(?m)^transport=worker\r?$" -or
    $directResult.Output -notmatch "(?m)^transport=direct\r?$") {
    throw "CLI did not preserve the explicit transport choice"
}

$unsupported = Invoke-Cli @("render", $Scene, "--width", "8")
if ($unsupported.ExitCode -ne 2 -or $unsupported.Output -notmatch "not executable through ProductJob 0.1") {
    throw "CLI did not reject an unimplemented product option"
}

$missingWorker = Join-Path (Split-Path -Parent $Worker) "missing_worker.exe"
$lost = Invoke-Cli @("render", $Scene, "--spp", "1", "--runtime", $Runtime, "--worker", $missingWorker)
if ($lost.ExitCode -ne 1 -or $lost.Output -notmatch "worker process creation failed") {
    throw "CLI Worker launch failure did not remain isolated and fail loudly"
}

$canceled = Invoke-Cli @("render", $Scene, "--spp", "100000", "--runtime", $Runtime, "--worker", $Worker, "--cancel-after-ms", "1")
if ($canceled.ExitCode -ne 130 -or $canceled.Output -notmatch "result=-12") {
    throw "CLI cancellation did not preserve the product terminal result"
}

Write-Output "PRV.1 CLI direct/Worker/load/render/cancel/error/frame/artifact gate passed"
