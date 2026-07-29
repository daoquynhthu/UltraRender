$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing W.10 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing W.10 contract '$pattern' in $Path"
        }
    }
}

Require-Text "libs/ure_core/include/ure/local_fullwave.hpp" @(
    "LocalFullWaveSolverKind",
    "Rcwa",
    "Fdtd",
    "Fem",
    "Bem",
    "Fmm",
    "Dda",
    "SMatrixImport",
    "LocalFullWaveRequest",
    "LocalFullWaveProviderDescriptor",
    "LocalFullWaveEvidence",
    "LocalFullWaveArtifact",
    "LocalFullWaveCache",
    "LocalFullWaveRegistry",
    "kMaxLocalFullWaveInputBytes",
    "kMaxLocalFullWaveArtifactBytes"
)
Require-Text "libs/ure_core/src/local_fullwave.cpp" @(
    "sha256_hex",
    "expected_entry_count",
    "exact_grid_match",
    "content_digest",
    "ure::wave::is_valid(",
    "provider.invoke(request_bytes)",
    "descriptor.executable_digest",
    "descriptor.semantic_digest",
    "cache->insert(",
    "read_local_fullwave_artifact"
)
Require-Text "tests/host/test_local_fullwave.cpp" @(
    "test_request_and_artifact_roundtrip",
    "test_registry_cache_and_consumption",
    "test_fail_loud_boundaries",
    "Kind::Rcwa",
    "Kind::Fdtd",
    "Kind::Fem",
    "Kind::Bem",
    "Kind::Fmm",
    "Kind::Dda",
    "Kind::SMatrixImport",
    "deterministic cache did not reuse artifact",
    "incomplete scattering grid was accepted"
)
Require-Text "tests/gpu/test_wave_optics_gpu.cu" @(
    "make_verified_gpu_fullwave_artifact",
    "make_local_fullwave_artifact",
    "fullwave_artifact.scattering"
)
Require-Text "tests/host/test_public_surface_sdk_free.cpp" @(
    "ure/local_fullwave.hpp"
)

$implementation = Get-Content -Raw -LiteralPath (
    Join-Path $root "libs/ure_core/src/local_fullwave.cpp")
foreach ($forbidden in @(
    "CreateProcess",
    "ShellExecute",
    "Start-Process",
    "std::system(",
    "system(")) {
    if ($implementation.Contains($forbidden)) {
        throw "W.10 core starts external processes instead of using the provider boundary: $forbidden"
    }
}

Write-Host "Phase W.10 static audit passed."
