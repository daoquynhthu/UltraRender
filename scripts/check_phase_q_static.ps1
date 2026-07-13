$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

function Require-Path([string]$RelativePath) {
    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing Phase Q artifact: $RelativePath"
    }
}

function Require-Text([string]$RelativePath, [string]$Pattern) {
    $path = Join-Path $repoRoot $RelativePath
    if (-not (Select-String -LiteralPath $path -Pattern $Pattern -Quiet)) {
        throw "Missing '$Pattern' in $RelativePath"
    }
}

$required = @(
    "docs\Phase_Q_Native_Scene_Format.md",
    "docs\superpowers\specs\2026-07-13-phase-q-native-scene-foundation-design.md",
    "docs\superpowers\plans\2026-07-13-phase-q-native-scene-foundation.md",
    "schemas\ure_native_v1.fbs",
    "schemas\ure_native_v1.baseline.fbs",
    "libs\ure_sceneio\generated\ure_native_v1_generated.h",
    "third_party\flatbuffers\LICENSE.txt",
    "tests\assets\native_scene\empty_package.ure",
    "tests\assets\native_scene\single_scene.ure",
    "tests\assets\native_scene\shared_resources.ure",
    "tests\assets\native_scene\resources\shared_spectrum.bin"
)
foreach ($path in $required) {
    Require-Path $path
}

Require-Text "scripts\regenerate_native_scene_schema.ps1" "25\.12\.19"
Require-Text "libs\ure_sceneio\generated\ure_native_v1_generated.h" "FLATBUFFERS_VERSION_MAJOR == 25"
Require-Text "libs\ure_sceneio\generated\ure_native_v1_generated.h" "FLATBUFFERS_VERSION_MINOR == 12"
Require-Text "libs\ure_sceneio\generated\ure_native_v1_generated.h" "FLATBUFFERS_VERSION_REVISION == 19"
Require-Text "docs\Phase_Q_Native_Scene_Format.md" "\.urecache"
Require-Text "libs\ure_types\include\ure\native_scene.hpp" "kReservedChunkKindIds"
Require-Text "schemas\ure_native_v1.fbs" 'file_identifier "UREM"'

$audit = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "docs\Phase_Q_Native_Scene_Format.md")
foreach ($token in @(
    "SceneIR", "RenderConfig", "WaveOpticsConfig", "IntegratorRuntimeConfig",
    "MaterialGraph", "MiePhaseResource", "SceneDiff", "DistributedShardMetadata",
    "scene.graph", "scene.geometry", "scene.material", "scene.resource", "scene.medium",
    "render.spectral", "render.integrator", "render.wave", "scene.mutation", "render.distributed",
    "scene.physics", "scene.acoustic", "build.procedural", "build.script",
    "render.backend", "render.acceleration", "scene.animation", "scene.video", "validation.contract"
)) {
    if (-not $audit.Contains($token)) {
        throw "Phase Q ownership audit is missing token '$token'"
    }
}

$nativeHeader = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "libs\ure_types\include\ure\native_scene.hpp")
if ($nativeHeader -match "cuda|SceneIR|Vk[A-Z]|D3D12|Optix") {
    throw "Native scene foundation header contains a backend or compiled-IR dependency"
}

$hashSource = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "libs\ure_sceneio\src\native_scene_hash.cpp")
if ($hashSource -match "manifest\.caches") {
    throw "Package semantic hashing must exclude cache descriptors"
}

$schema = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "schemas\ure_native_v1.fbs")
$baseline = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "schemas\ure_native_v1.baseline.fbs")
if ($schema -cne $baseline) {
    throw "Version 1 schema diverged from its conformance baseline"
}

$sharedManifest = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "tests\assets\native_scene\shared_resources.ure") | ConvertFrom-Json
$sharedResource = Join-Path $repoRoot "tests\assets\native_scene\resources\shared_spectrum.bin"
$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sharedResource).Hash.ToLowerInvariant()
if ($actualHash -ne $sharedManifest.resources[0].content_hash) {
    throw "Shared resource fixture hash mismatch"
}
if ((Get-Item -LiteralPath $sharedResource).Length -ne $sharedManifest.resources[0].byte_length) {
    throw "Shared resource fixture byte length mismatch"
}

Write-Host "Phase Q static audit passed."
