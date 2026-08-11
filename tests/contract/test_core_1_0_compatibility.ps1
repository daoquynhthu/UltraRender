param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$CurrentClient,
    [Parameter(Mandatory = $true)][string]$CurrentRuntime
)

$ErrorActionPreference = "Stop"
$fixtures = Join-Path $RepoRoot "tests/fixtures/contracts/old_clients"
$dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
if (-not $dumpbin) {
    $dumpbin = (Get-ChildItem -LiteralPath "D:/Microsoft Visual Studio/VC/Tools/MSVC" -Filter dumpbin.exe -File -Recurse |
        Where-Object FullName -Match 'Hostx64[\\/]x64' |
        Sort-Object FullName -Descending |
        Select-Object -First 1).FullName
}
if (-not $dumpbin) {
    throw "MSVC dumpbin.exe is required"
}

$candidateManifests = Get-ChildItem -LiteralPath $fixtures -Filter manifest.json -File -Recurse |
    Where-Object { $_.FullName -match 'candidate_0_1_pb[2-6]' } |
    Sort-Object FullName
if ($candidateManifests.Count -ne 5) {
    throw "Expected five historical Candidate baselines, found $($candidateManifests.Count)"
}
foreach ($manifestFile in $candidateManifests) {
    $manifest = Get-Content -Raw -LiteralPath $manifestFile | ConvertFrom-Json
    $directory = $manifestFile.Directory.FullName
    $source = Join-Path $directory $manifest.source
    $binary = Join-Path $directory $manifest.binary
    if ($manifest.publication_state -ne "Candidate" -or
        $manifest.compatibility_promise -ne "None before PB.8" -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant() -ne $manifest.source_sha256 -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $binary).Hash.ToLowerInvariant() -ne $manifest.binary_sha256 -or
        (Get-Item -LiteralPath $binary).Length -ne $manifest.binary_bytes) {
        throw "Historical Candidate evidence was altered: $($manifest.phase)"
    }
}

$seedRoot = Join-Path $fixtures "core_1_0_seed_from_pb7/windows_x64"
$seedManifest = Get-Content -Raw -LiteralPath (Join-Path $seedRoot "manifest.json") | ConvertFrom-Json
$seedSource = Join-Path $seedRoot $seedManifest.source
$seedBinary = Join-Path $seedRoot $seedManifest.binary
if ($seedManifest.schema -ne "ure.pb.core-client-seed/1.0" -or
    $seedManifest.core_abi -ne "1.0" -or
    $seedManifest.baseline_commit -ne "0de7e5cad21d4f372d5254c18bbbdfdbd6e1c408" -or
    (Get-FileHash -Algorithm SHA256 -LiteralPath $seedSource).Hash.ToLowerInvariant() -ne $seedManifest.source_sha256 -or
    (Get-FileHash -Algorithm SHA256 -LiteralPath $seedBinary).Hash.ToLowerInvariant() -ne $seedManifest.binary_sha256 -or
    (Get-Item -LiteralPath $seedBinary).Length -ne $seedManifest.binary_bytes) {
    throw "Core 1.0 PB.7-layout seed identity is invalid"
}
$headers = (& $dumpbin /nologo /headers $seedBinary 2>&1) -join "`n"
$dependencies = (& $dumpbin /nologo /dependents $seedBinary 2>&1) -join "`n"
if ($headers -notmatch '(?im)^\s*8664 machine \(x64\)' -or
    $dependencies -match '(?i)ultrarender_runtime(_candidate|_1)?\.dll') {
    throw "Core 1.0 seed does not preserve the dynamic-loader boundary"
}
& $seedBinary $CurrentRuntime
if ($LASTEXITCODE -ne 0) {
    throw "The retained Core 1.0 PB.7-layout seed rejected the current runtime"
}
& $CurrentClient $CurrentRuntime
if ($LASTEXITCODE -ne 0) {
    throw "The current client rejected the current Core 1.0 runtime"
}

$matrix = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "contracts/stability/core_1_0_compatibility_matrix.json") | ConvertFrom-Json
if ($matrix.schema -ne "ure.public.compatibility-matrix/1.0" -or
    $matrix.release -ne "1.0.0" -or
    -not $matrix.initial_stable_release -or
    $matrix.rows.current_client_prior_stable_runtime.status -ne "NotApplicable" -or
    $matrix.rows.oldest_core_1_client_current_runtime.status -ne "Passed" -or
    $matrix.rows.current_client_current_runtime.status -ne "Passed") {
    throw "Core 1.0 compatibility matrix is incomplete or overclaims a prior stable runtime"
}

Write-Output "Core 1.0 matrix passed: PB.7-layout seed/current runtime and current/current; prior stable runtime is truthfully N/A for the first stable major"
