param(
    [string]$Flatc = "flatc"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (& $Flatc --version 2>&1 | Out-String).Trim()
if ($version -ne "flatc version 25.12.19") {
    throw "Phase Q requires flatc version 25.12.19, got '$version'"
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ure-native-schema-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
try {
    $schemas = @(
        @{ Name = "ure_native_v1"; ObjectApi = $false },
        @{ Name = "ure_scene_ir_v1"; ObjectApi = $true },
        @{ Name = "ure_mesh_v1"; ObjectApi = $true },
        @{ Name = "ure_mie_v1"; ObjectApi = $true }
    )
    foreach ($entry in $schemas) {
        $schema = Join-Path $repoRoot ("schemas\" + $entry.Name + ".fbs")
        $baseline = Join-Path $repoRoot ("schemas\" + $entry.Name + ".baseline.fbs")
        & $Flatc --conform $baseline $schema
        if ($LASTEXITCODE -ne 0) {
            throw "FlatBuffers schema conformance failed for $($entry.Name)"
        }
        $arguments = @("--cpp", "--cpp-std", "c++17", "--scoped-enums")
        if ($entry.ObjectApi) {
            $arguments += "--gen-object-api"
        }
        $arguments += @("-o", $temporaryRoot, $schema)
        & $Flatc @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "FlatBuffers C++ generation failed for $($entry.Name)"
        }
        $generated = Join-Path $temporaryRoot ($entry.Name + "_generated.h")
        $checkedIn = Join-Path $repoRoot ("libs\ure_sceneio\generated\" + $entry.Name + "_generated.h")
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $generated).Hash -ne
            (Get-FileHash -Algorithm SHA256 -LiteralPath $checkedIn).Hash) {
            throw "Checked-in FlatBuffers generated header is stale for $($entry.Name)"
        }
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

Write-Host "Phase Q FlatBuffers schemas are conformant and generated output is current."
