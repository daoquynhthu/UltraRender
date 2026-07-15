$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Pattern([string]$Path, [string]$Pattern, [string]$Message) {
    if (-not (Select-String -LiteralPath (Join-Path $root $Path) -Pattern $Pattern -Quiet)) { throw $Message }
}

Require-Pattern "libs/ure_sceneio/src/native_compiled_cache.cpp" "URE-Q11-IDENTITY-001" "Q.11 source/compiler cache identity gate is missing"
Require-Pattern "libs/ure_sceneio/src/native_compiled_cache.cpp" "gpu_upload_plan" "Q.11 GPU upload plan serialization is missing"
Require-Pattern "libs/ure_sceneio/src/native_compiled_cache.cpp" "spectral_cache" "Q.11 spectral cache serialization is missing"
Require-Pattern "libs/ure_sceneio/src/native_compiled_cache.cpp" "local_content_hashes" "Q.11 farm locality scheduling is missing"

Write-Host "Phase Q.11 static audit passed."
