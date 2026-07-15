$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$paths = @(
    "schemas/ure_resource_catalog_v1.fbs",
    "schemas/ure_resource_catalog_v1.baseline.fbs",
    "libs/ure_sceneio/generated/ure_resource_catalog_v1_generated.h",
    "libs/ure_sceneio/include/ure/native_resource_catalog.hpp",
    "libs/ure_sceneio/src/native_resource_catalog.cpp",
    "tests/host/test_native_resource_catalog.cpp"
)
foreach ($path in $paths) { if (-not (Test-Path (Join-Path $root $path))) { throw "Missing Q.6 artifact: $path" } }
$schema = Get-Content -Raw (Join-Path $root "schemas/ure_resource_catalog_v1.fbs")
if ($schema -notmatch 'file_identifier "URRC"') { throw "Q.6 schema identifier is not URRC" }
$header = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/include/ure/native_resource_catalog.hpp")
foreach ($token in @("domain_bins", "packet_lanes_hint", "SourceSampleGrid", "Basis", "Tiled", "MediumResourceContract", "VideoResourceContract")) {
    if (-not $header.Contains($token)) { throw "Missing Q.6 contract token: $token" }
}
$source = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/src/native_resource_catalog.cpp")
if ($source -match "cuda|Gpu[A-Z]|Vk[A-Z]|D3D12|Optix") { throw "Q.6 catalog contains backend-specific state" }
if ($source -notmatch "URE-Q6-") { throw "Q.6 structured diagnostics are missing" }
Write-Host "Phase Q.6 static audit passed."
