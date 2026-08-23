param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$Generator
)

$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$manifestPath = Join-Path $RepoRoot "tests/assets/product_e2e/fixture_manifest.json"
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json -Depth 100

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

if ($manifest.schema -ne "ure.preview.product-e2e-fixtures/1.0" -or
    @($manifest.fixtures).Count -ne 3) {
    throw "Product E2E fixture manifest shape is invalid"
}
foreach ($entry in @($manifest.source, $manifest.generator) + @($manifest.fixtures)) {
    $path = Join-Path $RepoRoot $entry.path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-Sha256 $path) -ne $entry.sha256) {
        throw "Product E2E fixture input drifted: $($entry.path)"
    }
}

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) (
    "ure_product_e2e_fixtures_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    Push-Location $temporary
    try {
        & $Generator (Join-Path $RepoRoot $manifest.source.path) $temporary *> $null
        if ($LASTEXITCODE -ne 0) {
            throw "Product E2E fixture generator failed"
        }
    } finally {
        Pop-Location
    }
    foreach ($fixture in $manifest.fixtures) {
        $generated = Join-Path $temporary ([System.IO.Path]::GetFileName($fixture.path))
        if (-not (Test-Path -LiteralPath $generated -PathType Leaf) -or
            (Get-Sha256 $generated) -ne $fixture.sha256) {
            throw "Product E2E fixture is not reproducible: $($fixture.id)"
        }
    }
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output "Product E2E Cornell fixtures are source-bound and reproducible"
