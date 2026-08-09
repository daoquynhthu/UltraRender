$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$schemaTriples = @(
    @{ Name = "ure_scene_ir_v1"; Identifier = "URIG" },
    @{ Name = "ure_scene_ir_v2"; Identifier = "URIG"; Baseline = "ure_scene_ir_v1" },
    @{ Name = "ure_mesh_v1"; Identifier = "URMS" },
    @{ Name = "ure_mie_v1"; Identifier = "URMI" }
)
foreach ($entry in $schemaTriples) {
    $schema = Join-Path $repoRoot ("schemas\" + $entry.Name + ".fbs")
    $baselineName = if ($entry.Baseline) { $entry.Baseline } else { $entry.Name }
    $baseline = Join-Path $repoRoot ("schemas\" + $baselineName + ".baseline.fbs")
    $generated = Join-Path $repoRoot ("libs\ure_sceneio\generated\" + $entry.Name + "_generated.h")
    if (-not (Test-Path $schema) -or -not (Test-Path $baseline) -or -not (Test-Path $generated)) {
        throw "Missing Q.3 schema triple for $($entry.Name)"
    }
    if (-not (Select-String -Quiet -LiteralPath $schema -SimpleMatch ('file_identifier "' + $entry.Identifier + '"'))) {
        throw "Wrong Q.3 identifier for $($entry.Name)"
    }
}

$publicHeader = Join-Path $repoRoot "libs\ure_sceneio\include\ure\native_scene_ir.hpp"
foreach ($api in @("make_native_scene_archive", "write_scene_ir_binary", "read_scene_ir_binary",
                    "write_scene_ir_text", "read_scene_ir_text", "scene_ir_semantic_hash",
                    "validate_scene_ir_archive", "save_native_scene", "load_native_scene")) {
    if (-not (Select-String -Quiet -LiteralPath $publicHeader -SimpleMatch $api)) {
        throw "Missing Q.3 public API: $api"
    }
}

$sceneIoCmake = Get-Content -Raw (Join-Path $repoRoot "libs\ure_sceneio\CMakeLists.txt")
if ($sceneIoCmake -match "ure_core|CUDA") {
    throw "ure_sceneio must remain independent of ure_core and CUDA"
}
$fixtureRoot = Join-Path $repoRoot "tests\assets\native_scene\q3_full_scene"
foreach ($fixture in @("full_scene.ure", "full_scene.urescene", "semantic_hash.txt",
                        "textures\albedo.ppm", "spectra\albedo.spd", "spectra\emission.spd")) {
    if (-not (Test-Path (Join-Path $fixtureRoot $fixture))) {
        throw "Missing Q.3 fixture: $fixture"
    }
}
$manifest = Get-Content -Raw (Join-Path $fixtureRoot "full_scene.ure")
if ($manifest -match "base64" -or $manifest -notmatch '"scene_ir"') {
    throw "Q.3 text fixture must contain a native scene graph without Base64"
}
$manifestJson = $manifest | ConvertFrom-Json
$expectedTypedResources = @($manifestJson.document.resources |
    Where-Object { $_.kind -eq "geometry" -or $_.kind -eq "mie_phase" } |
    ForEach-Object { $_.uri.Replace('/', '\') } |
    Sort-Object)
$actualTypedResources = @(Get-ChildItem (Join-Path $fixtureRoot "resources") -Recurse -File |
    ForEach-Object { [System.IO.Path]::GetRelativePath($fixtureRoot, $_.FullName) } |
    Sort-Object)
if (Compare-Object $expectedTypedResources $actualTypedResources) {
    throw "Q.3 fixture contains missing or unreferenced typed resources"
}
$testCmake = Get-Content -Raw (Join-Path $repoRoot "tests\host\CMakeLists.txt")
if (($testCmake | Select-String -AllMatches "add_test\(NAME test_native_scene_ir").Matches.Count -ne 1) {
    throw "Q.3 must register exactly one host CTest"
}

Write-Host "Phase Q.3 static audit passed."
