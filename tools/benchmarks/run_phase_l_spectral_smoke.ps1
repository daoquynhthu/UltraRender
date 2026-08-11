param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "",
    [int]$Spp = 1,
    [int]$Width = 64,
    [int]$Height = 64,
    [UInt64]$DomainBins = 1000000,
    [int]$PacketLanes = 1,
    [int]$MaxResidentMb = 64,
    [string]$Output = "phase_l_spectral_budget.hdr"
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$build = Join-Path $repo $BuildDir
if ([string]::IsNullOrWhiteSpace($Config)) {
    $cache = Get-Content -LiteralPath (Join-Path $build "CMakeCache.txt")
    $entry = $cache | Where-Object { $_ -match '^CMAKE_BUILD_TYPE:STRING=' } |
        Select-Object -First 1
    $Config = ($entry -split '=', 2)[1]
}
$exe = Join-Path $build "artifacts\$Config\bin\ure_cli.exe"
if (-not (Test-Path $exe)) {
    throw "ure_cli.exe not found: $exe"
}

$scene = Join-Path $repo "scenes\benchmarks\phase_l_spectral_budget.gltf"
if (-not (Test-Path $scene)) {
    throw "benchmark scene not found: $scene"
}

$args = @(
    "render",
    $scene,
    "--spp", "$Spp",
    "--width", "$Width",
    "--height", "$Height",
    "--format", "hdr",
    "--output", $Output,
    "--spectral-domain-bins", "$DomainBins",
    "--spectral-packet-lanes", "$PacketLanes",
    "--spectral-max-resident-mb", "$MaxResidentMb",
    "--spectral-sampling", "uniform_sampled"
)

if ($Config -ne "") {
    $args += @("--config", $Config)
}

& $exe @args
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
