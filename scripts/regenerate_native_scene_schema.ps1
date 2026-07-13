param(
    [string]$Flatc = "flatc"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (& $Flatc --version 2>&1 | Out-String).Trim()
if ($version -ne "flatc version 25.12.19") {
    throw "Phase Q requires flatc version 25.12.19, got '$version'"
}

$schema = Join-Path $repoRoot "schemas\ure_native_v1.fbs"
$baseline = Join-Path $repoRoot "schemas\ure_native_v1.baseline.fbs"
& $Flatc --conform $baseline $schema
if ($LASTEXITCODE -ne 0) {
    throw "FlatBuffers schema conformance failed"
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ure-native-schema-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
try {
    & $Flatc --cpp --cpp-std c++17 --scoped-enums -o $temporaryRoot $schema
    if ($LASTEXITCODE -ne 0) {
        throw "FlatBuffers C++ generation failed"
    }
    $generated = Join-Path $temporaryRoot "ure_native_v1_generated.h"
    $checkedIn = Join-Path $repoRoot "libs\ure_sceneio\generated\ure_native_v1_generated.h"
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $generated).Hash -ne
        (Get-FileHash -Algorithm SHA256 -LiteralPath $checkedIn).Hash) {
        throw "Checked-in FlatBuffers generated header is stale"
    }
}
finally {
    $resolvedTemporary = [System.IO.Path]::GetFullPath($temporaryRoot)
    $resolvedSystemTemp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if (-not $resolvedTemporary.StartsWith($resolvedSystemTemp, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a temporary directory outside the system temp root"
    }
    Remove-Item -Recurse -Force -LiteralPath $resolvedTemporary
}

Write-Host "Phase Q FlatBuffers schema is conformant and generated output is current."
