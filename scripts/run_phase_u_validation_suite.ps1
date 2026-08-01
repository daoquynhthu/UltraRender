param(
    [string]$OpenUsdRoot = $env:HFS,
    [string]$BuildDir = "build_modular_x64",
    [string]$HydraBuildDir = ".build/phase_u5_hydra_vs18",
    [string]$UsdBuildDir = ".build/phase_u6_usda",
    [string]$Config = "Release",
    [string]$OutputPath =
        "output/validation/phase_u_validation.json",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ResolvedOutput = Join-Path $RepoRoot $OutputPath
if ([string]::IsNullOrWhiteSpace($OpenUsdRoot)) {
    throw "Pass -OpenUsdRoot explicitly or set HFS"
}
$OpenUsd = Resolve-Path -LiteralPath $OpenUsdRoot

function Invoke-Checked {
    param(
        [scriptblock]$Command,
        [string]$Label
    )
    $global:LASTEXITCODE = 0
    & $Command
    if (-not $? -or $LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function File-Digest {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing Phase U artifact: $Path"
    }
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

if (-not $SkipBuild) {
    Invoke-Checked {
        & (Join-Path $PSScriptRoot "build_x64.ps1") `
            -BuildDir $BuildDir `
            -Config $Config
    } "Phase U Release build"
}

$RequiredTests = @(
    "test_config",
    "test_native_scene",
    "test_native_scene_ir",
    "test_native_tooling",
    "test_native_adapter",
    "test_usd_schema_adapter",
    "test_session"
)
$InventoryOutput = & ctest `
    --test-dir $BuildPath `
    -C $Config `
    -N 2>&1 |
    Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Phase U CTest inventory failed"
}
foreach ($test in $RequiredTests) {
    if ($InventoryOutput -notmatch
        "(?m)Test\s+#\d+:\s+$([regex]::Escape($test))\s*$") {
        throw "Phase U required test is not registered: $test"
    }
}

Invoke-Checked {
    & (Join-Path $PSScriptRoot "run_phase_u5_hydra_render_gate.ps1") `
        -OpenUsdRoot $OpenUsd `
        -BuildDir $HydraBuildDir
} "Phase U.5 actual Hydra gate"
Invoke-Checked {
    & (Join-Path $PSScriptRoot "run_phase_u6_usda_export_gate.ps1") `
        -OpenUsdRoot $OpenUsd `
        -BuildDir $UsdBuildDir
} "Phase U.6 actual USDA gate"

foreach ($step in 1..6) {
    $script = Join-Path $PSScriptRoot `
        "check_phase_u${step}_static.ps1"
    Invoke-Checked { & $script } `
        "Phase U.$step static gate"
}
Invoke-Checked {
    & (Join-Path $PSScriptRoot `
        "check_documentation_consistency.ps1") `
        -BuildDir $BuildDir
} "Phase U documentation gate"

$CTestOutput = & ctest `
    --test-dir $BuildPath `
    -C $Config `
    --output-on-failure 2>&1 |
    Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Phase U registered test gate failed`n$CTestOutput"
}
$CTestMatch = [regex]::Match(
    $CTestOutput,
    '100% tests passed, 0 tests failed out of (\d+)')
if (-not $CTestMatch.Success) {
    throw "Phase U registered test summary is missing"
}

$Commit = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or
    $Commit -notmatch "^[0-9a-f]{40}$") {
    throw "Phase U source commit is unavailable"
}
$TreeState = if ([string]::IsNullOrWhiteSpace(
        (& git -C $RepoRoot status --porcelain |
            Out-String))) {
    "clean"
} else {
    "dirty"
}
$HydraBuildPath = Join-Path $RepoRoot $HydraBuildDir
$UsdBuildPath = Join-Path $RepoRoot $UsdBuildDir
$Report = [ordered]@{
    schema = "ure.phase_u.validation.v1"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = "passed"
    source = [ordered]@{
        commit = $Commit
        tree_state = $TreeState
    }
    artifacts = [ordered]@{
        sceneio_sha256 = File-Digest (
            Join-Path $BuildPath "libs/ure_sceneio/ure_sceneio.lib")
        hydra_plugin_sha256 = File-Digest (
            Join-Path $HydraBuildPath "hydra/ure_hydra/Release/ure_hydra.dll")
        usda_export_test_sha256 = File-Digest (
            Join-Path $UsdBuildPath "tests/hydra/test_usda_export.exe")
    }
    evidence = [ordered]@{
        usd_native_mapping = "passed"
        hydra_plugin_discovery = "passed"
        mesh_and_material_mapping = "passed"
        progressive_cuda_render = "passed"
        deterministic_usda_export = "passed"
        strict_and_documented_loss = "passed"
        actual_openusd_parse = "passed"
    }
    test_gate = [ordered]@{
        status = "passed"
        passed = [uint32]$CTestMatch.Groups[1].Value
        failed = 0
        required = $RequiredTests
    }
    static_gates = [ordered]@{
        phase_u_1_through_u_6 = $true
        documentation = $true
    }
}
New-Item -ItemType Directory -Force `
    -Path (Split-Path -Parent $ResolvedOutput) |
    Out-Null
$Report | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath $ResolvedOutput -Encoding utf8

Write-Host (
    "Phase U validation passed: {0} registered tests, actual OpenUSD/Hydra/CUDA gates, output {1}" -f `
        $CTestMatch.Groups[1].Value,
        $ResolvedOutput)
