param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$Generator
)

$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$manifestPath = Join-Path $RepoRoot `
    "tests/assets/product_e2e/prv3/fixture_manifest.json"
$manifest = Get-Content -Raw -LiteralPath $manifestPath |
    ConvertFrom-Json -Depth 100

function Assert-Hash([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "PRV.3 fixture input is missing: $Path"
    }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "PRV.3 fixture identity mismatch: $Path"
    }
}

Assert-Hash (Join-Path $RepoRoot $manifest.generator.path) `
    $manifest.generator.sha256
foreach ($entry in @($manifest.sources) + @($manifest.resources) +
                    @($manifest.fixtures)) {
    Assert-Hash (Join-Path $RepoRoot $entry.path) $entry.sha256
}

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) (
    "ultrarender-prv3-assets-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    & $Generator `
        (Join-Path $RepoRoot "scenes/cornell_box.gltf") `
        (Join-Path $RepoRoot `
            "tests/assets/native_scene/q3_full_scene/full_scene.urescene") `
        $temporary
    if ($LASTEXITCODE -ne 0) {
        throw "PRV.3 fixture generator failed with $LASTEXITCODE"
    }
    foreach ($entry in @($manifest.resources) + @($manifest.fixtures)) {
        $relative = $entry.path -replace `
            '^tests/assets/product_e2e/prv3/', ''
        Assert-Hash (Join-Path $temporary $relative) $entry.sha256
    }
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force `
        -ErrorAction SilentlyContinue
}

Write-Output "PRV.3 fixture manifest and deterministic regeneration passed"
