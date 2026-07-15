$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$schema = Join-Path $repoRoot "schemas\ure_procedural_graph_v1.fbs"
$baseline = Join-Path $repoRoot "schemas\ure_procedural_graph_v1.baseline.fbs"
$generated = Join-Path $repoRoot "libs\ure_sceneio\generated\ure_procedural_graph_v1_generated.h"
foreach ($path in @($schema, $baseline, $generated)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing Q.4 schema artifact: $path" }
}
if (-not (Select-String -Quiet -LiteralPath $schema -SimpleMatch 'file_identifier "URPG"')) {
    throw "Q.4 schema identifier must be URPG"
}
$types = Get-Content -Raw (Join-Path $repoRoot "libs\ure_types\include\ure\native_scene.hpp")
if ($types -notmatch "ProceduralGraph\s*=\s*17" -or $types -notmatch "kReservedChunkKindIds\{0, 16\}") {
    throw "Q.4 must preserve reserved chunk 16 and assign ProceduralGraph chunk 17"
}
$public = Get-Content -Raw (Join-Path $repoRoot "libs\ure_sceneio\include\ure\native_procedural_graph.hpp")
foreach ($name in @("ProceduralGraph", "SceneIRFragment", "ProceduralBuildResult",
                     "validate_procedural_graph", "procedural_source_hash",
                     "procedural_cache_key", "build_procedural_scene")) {
    if (-not $public.Contains($name)) { throw "Missing Q.4 public contract: $name" }
}
$regen = Get-Content -Raw (Join-Path $repoRoot "scripts\regenerate_native_scene_schema.ps1")
if (-not $regen.Contains('ure_procedural_graph_v1')) { throw "Pinned schema gate omits URPG" }
$sceneIo = Get-Content -Raw (Join-Path $repoRoot "libs\ure_sceneio\CMakeLists.txt")
if ($sceneIo -match "ure_core|CUDA") { throw "ure_sceneio must remain independent of ure_core and CUDA" }
$tests = Get-Content -Raw (Join-Path $repoRoot "tests\host\CMakeLists.txt")
if (($tests | Select-String -AllMatches "add_test\(NAME test_native_procedural_graph").Matches.Count -ne 1) {
    throw "Q.4 must register exactly one host CTest"
}
$fixture = Join-Path $repoRoot "tests\assets\native_scene\q4_procedural_scene"
foreach ($name in @("procedural_scene.ure", "procedural_scene.urescene", "expected_hashes.txt")) {
    if (-not (Test-Path -LiteralPath (Join-Path $fixture $name))) { throw "Missing Q.4 fixture: $name" }
}
$manifest = Get-Content -Raw (Join-Path $fixture "procedural_scene.ure")
if ($manifest -notmatch '"procedural_graph"' -or $manifest -match "base64") {
    throw "Q.4 text fixture must contain a typed procedural graph without Base64"
}
$hashes = @(Get-Content (Join-Path $fixture "expected_hashes.txt"))
if ($hashes.Count -ne 3 -or @($hashes | Where-Object { $_ -notmatch '^[0-9a-f]{64}$' }).Count -ne 0) {
    throw "Q.4 fixture must pin source, cache, and output SHA-256 hashes"
}
$generatedSpectra = @(Get-ChildItem (Join-Path $fixture "resources\generated\spectrum") -Filter *.spd -File)
if ($generatedSpectra.Count -ne 1) { throw "Q.4 fixture must retain exactly one generated SPD oracle" }

Write-Host "Phase Q.4 static audit passed."
