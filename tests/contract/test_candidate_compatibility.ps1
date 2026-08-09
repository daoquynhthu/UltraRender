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
$manifests = Get-ChildItem -LiteralPath $fixtures -Filter manifest.json -File -Recurse |
    Where-Object { $_.FullName -match 'candidate_0_1_pb[2-6]' } |
    Sort-Object FullName
if ($manifests.Count -ne 5) {
    throw "Expected five published candidate client baselines, found $($manifests.Count)"
}

foreach ($manifestFile in $manifests) {
    $manifest = Get-Content -Raw -LiteralPath $manifestFile | ConvertFrom-Json
    $directory = $manifestFile.Directory.FullName
    $source = Join-Path $directory $manifest.source
    $binary = Join-Path $directory $manifest.binary
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant() -ne $manifest.source_sha256) {
        throw "Source digest mismatch: $source"
    }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $binary).Hash.ToLowerInvariant() -ne $manifest.binary_sha256) {
        throw "Binary digest mismatch: $binary"
    }
    if ((Get-Item -LiteralPath $binary).Length -ne $manifest.binary_bytes) {
        throw "Binary size mismatch: $binary"
    }
    $loader = (& git -C $RepoRoot show "$($manifest.baseline_commit)`:contracts/generated/include/ultrarender/ure_loader.h") -join "`n"
    $registry = (& git -C $RepoRoot show "$($manifest.baseline_commit)`:contracts/generated/include/ultrarender/ure_registry.h") -join "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    $sdkDigest = ([BitConverter]::ToString($sha.ComputeHash(
        [Text.Encoding]::UTF8.GetBytes($loader + "`n" + $registry + "`n")))).Replace("-", "").ToLowerInvariant()
    if ($sdkDigest -ne $manifest.sdk_header_sha256) {
        throw "Historical SDK digest mismatch: $($manifest.phase)"
    }
    $headers = (& $dumpbin /nologo /headers $binary 2>&1) -join "`n"
    $dependencies = (& $dumpbin /nologo /dependents $binary 2>&1) -join "`n"
    if ($headers -notmatch '(?im)^\s*8664 machine \(x64\)' -or
        $dependencies -match '(?i)ultrarender_runtime_candidate\.dll') {
        throw "Historical client platform or dynamic-loader boundary is invalid: $($manifest.phase)"
    }
    & $binary $CurrentRuntime
    if ($LASTEXITCODE -ne 0) {
        throw "$($manifest.phase) compiled client rejected the current runtime"
    }
}

& $CurrentClient $CurrentRuntime
if ($LASTEXITCODE -ne 0) {
    throw "Current client rejected the only retained supported runtime"
}

Write-Output "Candidate matrix: five historical clients -> current runtime; current client -> retained current runtime"
