$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
foreach ($path in @("schemas/ure_solver_contract_v1.fbs", "schemas/ure_solver_contract_v1.baseline.fbs", "libs/ure_sceneio/generated/ure_solver_contract_v1_generated.h", "libs/ure_sceneio/include/ure/native_solver_contract.hpp", "libs/ure_sceneio/src/native_solver_contract.cpp", "tests/host/test_native_solver_contract.cpp")) { if (-not (Test-Path (Join-Path $root $path))) { throw "Missing Q.7 artifact: $path" } }
$schema = Get-Content -Raw (Join-Path $root "schemas/ure_solver_contract_v1.fbs")
if ($schema -notmatch 'file_identifier "URSC"') { throw "Q.7 schema identifier is not URSC" }
$header = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/include/ure/native_solver_contract.hpp")
foreach ($token in @("BDPT", "VCM", "ExecutionBackend", "AccelerationProvider", "CoherentMergeMode", "ValidationRequirement", "SolverCapabilityRegistry")) { if (-not $header.Contains($token)) { throw "Missing Q.7 contract token: $token" } }
$source = Get-Content -Raw (Join-Path $root "libs/ure_sceneio/src/native_solver_contract.cpp")
foreach ($token in @("URE-Q7-CAPABILITY", "URE-Q7-BIAS", "URE-Q7-WAVE", "compile_solver_contract")) { if (-not $source.Contains($token)) { throw "Missing Q.7 validation token: $token" } }
if ($source -cmatch "cuda_runtime|Gpu[A-Z]|Vk[A-Z]|D3D12[A-Z]|Optix[A-Z]") { throw "Q.7 solver contract contains backend implementation state" }
Write-Host "Phase Q.7 static audit passed."
