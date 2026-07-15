$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Pattern([string]$Path, [string]$Pattern, [string]$Message) {
    if (-not (Select-String -LiteralPath (Join-Path $root $Path) -Pattern $Pattern -Quiet)) { throw $Message }
}

Require-Pattern "libs/ure_sceneio/src/native_adapter.cpp" "validate_scene_ir_archive" "Q.10 adapter bypasses native validation"
Require-Pattern "libs/ure_sceneio/src/native_adapter.cpp" "ure.adapter.loss/1.0" "Q.10 standardized loss report is missing"
Require-Pattern "libs/ure_sceneio/src/native_adapter.cpp" "URE-Q10-USD-001" "Q.10 USD unsupported boundary is not fail-loud"
Require-Pattern "libs/ure_sceneio/src/native_adapter.cpp" "import_materialx_native" "Q.10 MaterialX native adapter boundary is missing"
Require-Pattern "apps/ure_cli/src/main.cpp" "import_gltf_native" "Q.10 CLI does not consume the native adapter boundary"

Write-Host "Phase Q.10 static audit passed."
