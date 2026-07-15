$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$suite = Join-Path $root "scripts/run_phase_q_validation_suite.ps1"
$fixture = Join-Path $root "tests/assets/native_scene/q12_validation/fixture_manifest.json"
if (-not (Test-Path -LiteralPath $suite)) { throw "Q.12 validation suite is missing" }
if (-not (Test-Path -LiteralPath $fixture)) { throw "Q.12 fixture manifest is missing" }
$text = Get-Content -LiteralPath $fixture -Raw
foreach ($capability in @("basic_scene", "procedural_scene", "spectral_resource", "mie_volume", "wave_optics_request", "integrator_request", "physics_placeholder", "acoustic_placeholder", "video_stream_placeholder", "adapter_loss", "package_build")) {
    if ($text -notmatch [regex]::Escape($capability)) { throw "Q.12 fixture coverage is missing: $capability" }
}

Write-Host "Phase Q.12 static audit passed."
