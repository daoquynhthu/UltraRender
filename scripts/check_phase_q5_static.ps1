$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/include/ure/native_script_build.hpp")
$source = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/src/native_script_build.cpp")
foreach ($token in @("build.script", "enabled = false", "IScriptSandboxRunner", "dependency_lock_hash", "policy_hash")) {
    if (($header + $source) -notmatch [regex]::Escape($token)) { throw "Q.5 contract token missing: $token" }
}
if ($source -match "CreateProcess|ShellExecute|system\s*\(") { throw "Q.5 coordinator must not launch a process" }
foreach ($path in @("schemas/ure_script_build_v1.fbs", "schemas/ure_script_build_v1.baseline.fbs", "libs/ure_sceneio/generated/ure_script_build_v1_generated.h")) {
    if (-not (Test-Path (Join-Path $root $path))) { throw "Q.5 schema artifact missing: $path" }
}
$schema = Get-Content -Raw (Join-Path $root "schemas/ure_script_build_v1.fbs")
if ($schema -notmatch 'file_identifier "URSB"') { throw "Q.5 schema identity is not URSB" }
$native = Get-Content -Raw (Join-Path $root "libs/ure_types/include/ure/native_scene.hpp")
$container = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/src/native_scene_container.cpp")
if ($native -notmatch 'ScriptBuild = 18' -or $container -notmatch 'ChunkKind::ResourceCatalog') { throw "Q.5 chunk is not recognized by the native container" }
Write-Host "Phase Q.5 static audit passed."
