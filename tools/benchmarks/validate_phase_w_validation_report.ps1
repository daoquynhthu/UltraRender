param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,
    [switch]$RequireCleanTree
)

$ErrorActionPreference = "Stop"

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Digest {
    param(
        [object]$Value,
        [string]$Label
    )
    Assert-Condition (
        "$Value" -match "^[0-9a-f]{64}$"
    ) "$Label is not a SHA-256 digest"
}

$Report =
    Get-Content -Raw -LiteralPath (
        Resolve-Path -LiteralPath $ReportPath) |
    ConvertFrom-Json
$ExpectedEvidence = @(
    "airy_first_zero",
    "slit_and_grating_angles",
    "two_beam_interference",
    "thin_film_complex_phase",
    "rough_dielectric_pdf",
    "stokes_jones",
    "fluorescence_shift",
    "energy_and_pdf",
    "coherent_merge_order",
    "unsupported_fail_loud",
    "config_api_parity"
)
$ExpectedTests = @(
    "gpu_polarization",
    "gpu_spectral_soa",
    "gpu_wave_optics",
    "test_config",
    "test_wave_optics",
    "test_local_fullwave",
    "test_material_graph",
    "test_native_solver_contract",
    "test_session",
    "test_distributed_wave_io",
    "test_pyure_smoke"
)
$ExpectedStatic = @(
    "phase_w_2",
    "phase_w_5",
    "phase_w_6",
    "phase_w_7",
    "phase_w_9",
    "phase_w_10",
    "phase_w_11",
    "phase_w_12",
    "phase_t",
    "physics_optics",
    "documentation"
)

Assert-Condition (
    $Report.schema -eq
        "ure.phase_w.validation.v1"
) "invalid Phase W validation schema"
Assert-Condition (
    $Report.status -eq "passed"
) "Phase W validation status is not passed"
Assert-Condition (
    "$($Report.source.commit)" -match
        "^[0-9a-f]{40}$"
) "Phase W source commit is invalid"
Assert-Condition (
    $Report.source.tree_state -in @(
        "clean",
        "dirty")
) "Phase W source tree state is invalid"
if ($RequireCleanTree) {
    Assert-Condition (
        $Report.source.tree_state -eq "clean"
    ) "Phase W report was not produced from a clean tree"
}

Assert-Digest `
    $Report.artifacts.wave_host_sha256 `
    "host wave executable"
Assert-Digest `
    $Report.artifacts.wave_gpu_sha256 `
    "GPU wave executable"
Assert-Digest `
    $Report.artifacts.core_library_sha256 `
    "core library"

$ActualEvidence = @(
    $Report.evidence.PSObject.Properties.Name |
        Sort-Object
)
Assert-Condition (
    ($ActualEvidence -join ",") -eq
    (($ExpectedEvidence | Sort-Object) -join ",")
) "Phase W evidence set is incomplete"
foreach ($name in $ExpectedEvidence) {
    $entry = $Report.evidence.$name
    Assert-Condition (
        $entry.status -eq "passed" -and
        -not [string]::IsNullOrWhiteSpace(
            "$($entry.test)") -and
        -not [string]::IsNullOrWhiteSpace(
            "$($entry.contract)")
    ) "Phase W evidence is invalid: $name"
}

Assert-Condition (
    $Report.test_gate.status -eq "passed" -and
    [uint32]$Report.test_gate.failed -eq 0 -and
    [uint32]$Report.test_gate.passed -ge
        $ExpectedTests.Count
) "Phase W CTest gate is invalid"
$ActualTests = @(
    $Report.test_gate.required |
        ForEach-Object { "$_" } |
        Sort-Object
)
Assert-Condition (
    ($ActualTests -join ",") -eq
    (($ExpectedTests | Sort-Object) -join ",")
) "Phase W required test set changed"

$ActualStatic = @(
    $Report.static_gates.PSObject.Properties.Name |
        Sort-Object
)
Assert-Condition (
    ($ActualStatic -join ",") -eq
    (($ExpectedStatic | Sort-Object) -join ",")
) "Phase W static gate set is incomplete"
foreach ($name in $ExpectedStatic) {
    Assert-Condition (
        [bool]$Report.static_gates.$name
    ) "Phase W static gate failed: $name"
}

Write-Host "Phase W validation report passed."
