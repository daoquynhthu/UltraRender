param(
    [Parameter(Mandatory = $true)][string]$Request
)

$ErrorActionPreference = "Stop"
$requestData = Get-Content -Raw -LiteralPath $Request | ConvertFrom-Json -Depth 20
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $requestData.executable
$start.WorkingDirectory = $requestData.working_directory
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in $requestData.arguments) {
    [void]$start.ArgumentList.Add([string]$argument)
}
$process = [System.Diagnostics.Process]::Start($start)
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
$process.WaitForExit()
$stdout = $stdoutTask.GetAwaiter().GetResult()
$stderr = $stderrTask.GetAwaiter().GetResult()
[System.IO.File]::WriteAllText(
    $requestData.stdout, $stdout,
    [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText(
    $requestData.stderr, $stderr,
    [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText(
    $requestData.exit_code, [string]$process.ExitCode + "`n",
    [System.Text.UTF8Encoding]::new($false))
exit $process.ExitCode
