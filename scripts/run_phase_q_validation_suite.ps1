param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$Flatc = "$env:TEMP\flatbuffers-25.12.19-ultrarender\flatc.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$cli = Join-Path $build "artifacts/$Config/bin/ure_cli.exe"
$scene = Join-Path $root "tests/assets/native_scene/q3_full_scene/full_scene.urescene"
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ure-q12-" + [guid]::NewGuid().ToString("N"))

function Invoke-Checked([scriptblock]$Command, [string]$Failure) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw $Failure }
}

try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    Invoke-Checked { & (Join-Path $root "scripts/build_x64.ps1") -BuildDir $BuildDir -Config $Config -SkipConfigure -Targets test_native_validation_suite,test_native_scene_ir,test_native_procedural_graph,test_native_resource_catalog,test_native_solver_contract,test_native_simulation_contract,test_native_tooling,test_native_adapter,test_native_compiled_cache,ure_cli } "Phase Q validation targets failed to build"
    Invoke-Checked { & ctest --test-dir $build -C $Config -R "test_native_(scene_ir|procedural_graph|resource_catalog|solver_contract|simulation_contract|tooling|adapter|compiled_cache|validation_suite)$" --output-on-failure } "Phase Q native CTest fixture gate failed"
    Invoke-Checked { & $cli validate $scene } "Native binary validation failed"
    $text = Join-Path $temporary "rebuilt.ure"
    $binary = Join-Path $temporary "rebuilt.urescene"
    $package = Join-Path $temporary "validation.urepkg"
    $unpacked = Join-Path $temporary "unpacked"
    Invoke-Checked { & $cli build $scene --output $text } "Native text build failed"
    Invoke-Checked { & $cli migrate $text --output $binary } "Native migration failed"
    Invoke-Checked { & $cli pack $binary --output $package } "Native package build failed"
    Invoke-Checked { & $cli validate $package } "Native package validation failed"
    Invoke-Checked { & $cli inspect $package } "Native package inspection failed"
    Invoke-Checked { & $cli unpack $package --output $unpacked } "Native package unpack failed"
    Invoke-Checked { & (Join-Path $root "scripts/regenerate_native_scene_schema.ps1") -Flatc $Flatc } "Native schema conformance failed"
    $staticScripts = @("check_phase_q_static.ps1", "check_phase_q3_static.ps1", "check_phase_q4_static.ps1", "check_phase_q5_static.ps1", "check_phase_q6_static.ps1", "check_phase_q7_static.ps1", "check_phase_q8_static.ps1", "check_phase_q9_static.ps1", "check_phase_q10_static.ps1", "check_phase_q11_static.ps1")
    foreach ($script in $staticScripts) {
        Invoke-Checked { & pwsh -NoProfile -File (Join-Path $root "scripts/$script") } "Phase Q static gate failed: $script"
    }
    Invoke-Checked { & git -C $root diff --check } "Repository diff check failed"
    Write-Host "Phase Q native validation suite passed."
} finally {
    $resolvedTemporary = [System.IO.Path]::GetFullPath($temporary)
    $resolvedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if (-not $resolvedTemporary.StartsWith($resolvedTempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove validation directory outside the system temporary root"
    }
    Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force -ErrorAction SilentlyContinue
}
