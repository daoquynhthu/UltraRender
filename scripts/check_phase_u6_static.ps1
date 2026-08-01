$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing U.6 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing U.6 contract '$pattern' in $Path"
        }
    }
}

Require-Text "libs/ure_sceneio/include/ure/native_adapter.hpp" @(
    "UsdExportPolicy",
    "UsdExportResult",
    "export_usda_native",
    "save_usda_native"
)
Require-Text "libs/ure_sceneio/src/usd_scene_export.cpp" @(
    "#usda 1.0",
    "ure.adapter.usda-export/1.0",
    "UsdPreviewSurface",
    "prepend references",
    "instanceable = true",
    "ure:physics:enabled",
    "MOVEFILE_REPLACE_EXISTING",
    "Lossy USDA export requires an explicit loss-report path"
)
Require-Text "libs/ure_sceneio/src/native_adapter.cpp" @(
    "URE-U6-ERROR-GLOBAL-MEDIUM",
    "URE-U6-LOSS-MATERIAL",
    "URE-U6-LOSS-QUAD-LIGHT"
)
Require-Text "apps/ure_cli/src/main.cpp" @(
    "CliCommand::Export",
    "export_native_scene_usda",
    "AllowDocumentedLoss",
    "cli.scene_id"
)
Require-Text "tests/host/test_native_adapter.cpp" @(
    "USDA export is not deterministic",
    "lossy USDA save did not enforce a durable report",
    "native package USDA scene selection is incomplete"
)
Require-Text "tests/hydra/test_usda_export.cpp" @(
    "ImportFromString",
    "ComputeBoundMaterial",
    "Preview Surface parameters"
)
Require-Text "scripts/run_phase_u6_usda_export_gate.ps1" @(
    "test_usda_export",
    "UR_ENABLE_CUDA=OFF"
)

Write-Host "Phase U.6 static audit passed."
