param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$OutputPath =
        "output/validation/phase_w_validation.json",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (
    Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ResolvedOutput = Join-Path $RepoRoot $OutputPath

function Invoke-Checked {
    param(
        [scriptblock]$Command,
        [string]$Label
    )
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function File-Digest {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "missing Phase W artifact: $Path"
    }
    (
        Get-FileHash -LiteralPath $Path `
            -Algorithm SHA256
    ).Hash.ToLowerInvariant()
}

$RequiredTests = @(
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

if (-not $SkipBuild) {
    Invoke-Checked {
        & (Join-Path $RepoRoot `
            "scripts\build_x64.ps1") `
            -BuildDir $BuildDir `
            -Config $Config
    } "Phase W Release build"
}

$InventoryOutput = & ctest `
    --test-dir $BuildPath `
    -C $Config `
    -N 2>&1 |
    Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Phase W CTest inventory failed"
}
foreach ($test in $RequiredTests) {
    if ($InventoryOutput -notmatch
        "(?m)Test\s+#\d+:\s+$([regex]::Escape($test))\s*$") {
        throw "Phase W required test is not registered: $test"
    }
}

$StaticScripts = @(
    "check_phase_w2_static.ps1",
    "check_phase_w5_static.ps1",
    "check_phase_w6_static.ps1",
    "check_phase_w7_static.ps1",
    "check_phase_w9_static.ps1",
    "check_phase_w10_static.ps1",
    "check_phase_w11_static.ps1",
    "check_phase_w12_static.ps1",
    "check_phase_t_static.ps1",
    "check_documentation_consistency.ps1"
)
foreach ($script in $StaticScripts) {
    Invoke-Checked {
        & (Join-Path $RepoRoot "scripts\$script")
    } "Phase W static gate $script"
}
Invoke-Checked {
    & (Join-Path $RepoRoot `
        "scripts\check_physics_optics.ps1")
} "Phase W physics/optics gate"

$CTestOutput = & ctest `
    --test-dir $BuildPath `
    -C $Config `
    --output-on-failure 2>&1 |
    Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Phase W registered test gate failed`n$CTestOutput"
}
$CTestMatch = [regex]::Match(
    $CTestOutput,
    '100% tests passed, 0 tests failed out of (\d+)')
if (-not $CTestMatch.Success) {
    throw "Phase W registered test summary is missing"
}
$PassedTests = [uint32]$CTestMatch.Groups[1].Value

$Commit = (
    & git -C $RepoRoot rev-parse HEAD
).Trim()
if ($LASTEXITCODE -ne 0 -or
    $Commit -notmatch "^[0-9a-f]{40}$") {
    throw "Phase W source commit is unavailable"
}
$TreeState = if (
    [string]::IsNullOrWhiteSpace(
        (& git -C $RepoRoot status --porcelain |
            Out-String))) {
    "clean"
} else {
    "dirty"
}

$Evidence = [ordered]@{
    airy_first_zero = [ordered]@{
        status = "passed"
        test = "test_wave_optics"
        contract = "analytic 1.2196698912665045 lambda over aperture"
    }
    slit_and_grating_angles = [ordered]@{
        status = "passed"
        test = "test_wave_optics"
        contract = "analytic first zero and propagating grating orders"
    }
    two_beam_interference = [ordered]@{
        status = "passed"
        test = "test_wave_optics"
        contract = "in-phase power four and pi-shift cancellation"
    }
    thin_film_complex_phase = [ordered]@{
        status = "passed"
        test = "gpu_polarization"
        contract = "independent complex Airy reflection oracle"
    }
    rough_dielectric_pdf = [ordered]@{
        status = "passed"
        test = "gpu_spectral_soa"
        contract = "spectral and UV-dependent eval/pdf/sample consistency"
    }
    stokes_jones = [ordered]@{
        status = "passed"
        test = "gpu_wave_optics"
        contract = "identity, polarizer and quarter-wave coherency transforms"
    }
    fluorescence_shift = [ordered]@{
        status = "passed"
        test = "test_wave_optics,gpu_wave_optics"
        contract = "adjoint excitation-to-emission shift and energy weighting"
    }
    energy_and_pdf = [ordered]@{
        status = "passed"
        test = "gpu_polarization,gpu_spectral_soa,gpu_wave_optics"
        contract = "boundary, BSDF and diffractive energy/PDF bounds"
    }
    coherent_merge_order = [ordered]@{
        status = "passed"
        test = "test_distributed_wave_io"
        contract = "field merge before realization power and incoherent average"
    }
    unsupported_fail_loud = [ordered]@{
        status = "passed"
        test = "test_wave_optics,test_native_solver_contract,test_material_graph"
        contract = "unsupported wave modes and nodes reject before rendering"
    }
    config_api_parity = [ordered]@{
        status = "passed"
        test = "test_config,test_session,test_pyure_smoke"
        contract = "JSON, CLI, C ABI, native session and Python gates"
    }
}

New-Item -ItemType Directory -Force `
    -Path (Split-Path -Parent $ResolvedOutput) |
    Out-Null
$Report = [ordered]@{
    schema = "ure.phase_w.validation.v1"
    generated_utc =
        [DateTime]::UtcNow.ToString("o")
    status = "passed"
    source = [ordered]@{
        commit = $Commit
        tree_state = $TreeState
    }
    artifacts = [ordered]@{
        wave_host_sha256 = File-Digest (
            Join-Path $BuildPath `
                "tests\host\test_wave_optics.exe")
        wave_gpu_sha256 = File-Digest (
            Join-Path $BuildPath `
                "tests\gpu\gpu_test_wave_optics.exe")
        core_library_sha256 = File-Digest (
            Join-Path $BuildPath `
                "libs\ure_core\ure_core.lib")
    }
    evidence = $Evidence
    test_gate = [ordered]@{
        status = "passed"
        passed = $PassedTests
        failed = 0
        required = $RequiredTests
    }
    static_gates = [ordered]@{
        phase_w_2 = $true
        phase_w_5 = $true
        phase_w_6 = $true
        phase_w_7 = $true
        phase_w_9 = $true
        phase_w_10 = $true
        phase_w_11 = $true
        phase_w_12 = $true
        phase_t = $true
        physics_optics = $true
        documentation = $true
    }
}
$Report | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath $ResolvedOutput `
        -Encoding utf8

$Persisted =
    Get-Content -Raw -LiteralPath $ResolvedOutput |
    ConvertFrom-Json
$EvidenceCount = @(
    $Persisted.evidence.PSObject.Properties
).Count
if ($Persisted.schema -ne
        "ure.phase_w.validation.v1" -or
    $Persisted.status -ne "passed" -or
    $Persisted.test_gate.failed -ne 0 -or
    $EvidenceCount -ne 11) {
    throw "Phase W validation report contract failed"
}
Invoke-Checked {
    & (Join-Path $RepoRoot `
        "tools\benchmarks\validate_phase_w_validation_report.ps1") `
        -ReportPath $ResolvedOutput
} "Phase W validation report"
Invoke-Checked {
    & (Join-Path $RepoRoot `
        "tools\benchmarks\test_phase_w_validation_report_contract.ps1") `
        -ReportPath $ResolvedOutput
} "Phase W validation report negative contract"

Write-Host (
    "Phase W validation suite passed: " +
    $ResolvedOutput)
