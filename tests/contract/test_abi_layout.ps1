param(
    [Parameter(Mandatory = $true)][string]$CExecutable,
    [Parameter(Mandatory = $true)][string]$CppExecutable,
    [Parameter(Mandatory = $true)][string]$Expected
)

$ErrorActionPreference = "Stop"

function Read-Layout([string]$Executable) {
    $lines = @(& $Executable 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "ABI layout executable failed: $Executable"
    }
    return (($lines -join "`n").Trim() -replace "`r", "")
}

$cLayout = Read-Layout $CExecutable
$cppLayout = Read-Layout $CppExecutable
$expectedLayout = ((Get-Content -LiteralPath $Expected -Raw).Trim() -replace "`r", "")
$null = $cLayout | ConvertFrom-Json
$null = $cppLayout | ConvertFrom-Json
$null = $expectedLayout | ConvertFrom-Json
if ($cLayout -cne $cppLayout) {
    throw "C and C++ ABI layouts differ"
}
if ($cLayout -cne $expectedLayout) {
    throw "Windows x64 Core 1.0 ABI layout drifted"
}
Write-Output "Windows x64 C/C++ ABI layout matched the Core 1.0 manifest"
