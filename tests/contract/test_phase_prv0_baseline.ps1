param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$CliExecutable
)

$ErrorActionPreference = "Stop"
$evidenceRoot = Join-Path $BuildDir "prv0_cli_evidence"
$outputRoot = Join-Path $evidenceRoot "output"
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ure_prv0_baseline_" + [guid]::NewGuid().ToString("N"))
$runner = Join-Path $RepoRoot "scripts/run_phase_prv0_baseline.ps1"
$scene = Join-Path $RepoRoot "scenes/cornell_box.gltf"

New-Item -ItemType Directory -Force -Path $evidenceRoot, $temporary | Out-Null
Remove-Item -LiteralPath $outputRoot -Recurse -Force -ErrorAction SilentlyContinue
try {
    Push-Location $evidenceRoot
    try {
        & $CliExecutable --quiet render $scene --spp 1 --width 16 --height 16 --integrator-mode wavefront --output prv0_cli.hdr --format hdr
        if ($LASTEXITCODE -ne 0) {
            throw "Legacy CLI baseline render failed"
        }
    } finally {
        Pop-Location
    }

    $first = Join-Path $temporary "baseline_first.json"
    $second = Join-Path $temporary "baseline_second.json"
    & $runner -RepoRoot $RepoRoot -BuildDir $BuildDir -ReportPath $first -RequireLiveImages | Out-Null
    & $runner -RepoRoot $RepoRoot -BuildDir $BuildDir -ReportPath $second -RequireLiveImages | Out-Null
    $firstHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $first).Hash
    $secondHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $second).Hash
    if ($firstHash -ne $secondHash) {
        throw "Preview baseline report is not byte-deterministic"
    }
    $report = Get-Content -Raw -LiteralPath $first | ConvertFrom-Json -Depth 100
    if ($report.preview_release_declared -ne $false -or
        $report.scenarios.product_e2e_now -ne 0 -or
        $report.cli_image_evidence.closure -ne "RendererIntegrated") {
        throw "Preview baseline report overstates product closure"
    }
    Write-Output "PRV.0 live CLI image and deterministic baseline report gate passed"
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
