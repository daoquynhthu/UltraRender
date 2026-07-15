$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Pattern([string]$Path, [string]$Pattern, [string]$Message) {
    if (-not (Select-String -LiteralPath (Join-Path $root $Path) -Pattern $Pattern -Quiet)) {
        throw $Message
    }
}

Require-Pattern "apps/ure_cli/src/main.cpp" "CliCommand::Pack" "Q.9 pack CLI dispatch is missing"
Require-Pattern "apps/ure_cli/src/main.cpp" "Adapter loss:" "Q.9 adapter-loss diagnostics are missing"
Require-Pattern "libs/ure_sceneio/src/native_scene_tooling.cpp" "URE-Q9-PACKAGE-005" "Q.9 package payload verification is missing"
Require-Pattern "libs/ure_core/src/ure_c_api.cpp" '\.urepkg' "Q.9 C Session package loading is missing"
Require-Pattern "pyure/__init__.py" "def load_package" "Q.9 Python package loading is missing"

Write-Host "Phase Q.9 static audit passed."
