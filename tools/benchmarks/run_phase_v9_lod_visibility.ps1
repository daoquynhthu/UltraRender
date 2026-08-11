param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$OutputPath =
        "output/benchmarks/phase_v9_lod_visibility.json",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ArtifactBin = Join-Path $BuildPath "artifacts\$Config\bin"

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") `
        -BuildDir $BuildDir `
        -Config $Config `
        -SkipConfigure `
        -Targets "gpu_test_cluster_lod,test_cluster_lod"
    if ($LASTEXITCODE -ne 0) {
        throw "Phase V.9 LoD targets failed to build"
    }
}

$GpuTest = Join-Path $ArtifactBin "gpu_test_cluster_lod.exe"
$HostTest = Join-Path $ArtifactBin "test_cluster_lod.exe"
if (-not (Test-Path $GpuTest) -or
    -not (Test-Path $HostTest)) {
    throw "Phase V.9 LoD executables are missing"
}

$HostOutput = (& $HostTest 2>&1 |
    ForEach-Object { "$_" }) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Host cluster LoD contract failed`n$HostOutput"
}
$GpuOutput = (& $GpuTest 2>&1 |
    ForEach-Object { "$_" }) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "GPU cluster LoD contract failed`n$GpuOutput"
}

$Pattern = [regex]::new(
    'schema=ure\.phase_v\.cluster_lod\.v1 rays=(\d+) ' +
    'protected_shadow_mismatch=(\d+) ' +
    'preview_shadow_mismatch=(\d+) ' +
    'protected_reflection_mismatch=(\d+) ' +
    'preview_reflection_mismatch=(\d+) ' +
    'diffuse_coarse=(\d+)')
$Match = $Pattern.Match($GpuOutput)
if (-not $Match.Success) {
    throw "Phase V.9 GPU output did not match the stable schema"
}

$Rays = [uint32]$Match.Groups[1].Value
$ProtectedShadow = [uint32]$Match.Groups[2].Value
$PreviewShadow = [uint32]$Match.Groups[3].Value
$ProtectedReflection = [uint32]$Match.Groups[4].Value
$PreviewReflection = [uint32]$Match.Groups[5].Value
$DiffuseCoarse = [uint32]$Match.Groups[6].Value
if ($Rays -eq 0 -or
    $ProtectedShadow -ne 0 -or
    $ProtectedReflection -ne 0 -or
    $PreviewShadow -ne $Rays -or
    $PreviewReflection -ne $Rays -or
    $DiffuseCoarse -ne $Rays) {
    throw "Phase V.9 physical visibility gate failed"
}

$Report = [ordered]@{
    schema = "ure.phase_v.cluster_lod.v1"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    build_dir = $BuildDir
    rays = $Rays
    shadow = [ordered]@{
        protected_mismatch = $ProtectedShadow
        preview_proxy_mismatch = $PreviewShadow
    }
    reflection = [ordered]@{
        protected_mismatch = $ProtectedReflection
        preview_proxy_mismatch = $PreviewReflection
    }
    diffuse = [ordered]@{
        coarse_selection_count = $DiffuseCoarse
    }
    gates = [ordered]@{
        physical_visibility_preserved = $true
        unsafe_preview_proxy_exposed = $true
        diffuse_lod_reduction_exercised = $true
    }
}

$ResolvedOutput = Join-Path $RepoRoot $OutputPath
$OutputDirectory = Split-Path -Parent $ResolvedOutput
New-Item -ItemType Directory -Force `
    -Path $OutputDirectory | Out-Null
$Report | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $ResolvedOutput -Encoding utf8
Write-Host "Phase V.9 LoD visibility: $ResolvedOutput"
