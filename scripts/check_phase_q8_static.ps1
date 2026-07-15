$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
foreach ($path in @("schemas/ure_simulation_contract_v1.fbs", "schemas/ure_simulation_contract_v1.baseline.fbs", "libs/ure_sceneio/generated/ure_simulation_contract_v1_generated.h", "libs/ure_sceneio/include/ure/native_simulation_contract.hpp", "libs/ure_sceneio/src/native_simulation_contract.cpp", "tests/host/test_native_simulation_contract.cpp")) { if (-not (Test-Path (Join-Path $root $path))) { throw "Missing Q.8 artifact: $path" } }
$schema = Get-Content -Raw (Join-Path $root "schemas/ure_simulation_contract_v1.fbs")
if ($schema -notmatch 'file_identifier "URPC"') { throw "Q.8 schema identifier is not URPC" }
$header = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/include/ure/native_simulation_contract.hpp")
foreach ($token in @("SoftBody", "AcousticModal", "AcousticRay", "AcousticWave", "CouplingChannel", "SolverMigrationPolicy", "synchronization_epoch")) { if (-not $header.Contains($token)) { throw "Missing Q.8 contract token: $token" } }
$source = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/src/native_simulation_contract.cpp")
foreach ($token in @("URE-Q8-CAPABILITY", "URE-Q8-COUPLING", "URE-Q8-RESOURCE", "URE-Q8-MIGRATION")) { if (-not $source.Contains($token)) { throw "Missing Q.8 validation token: $token" } }
Write-Host "Phase Q.8 static audit passed."
