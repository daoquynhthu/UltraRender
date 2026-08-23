param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

function Read-RepositoryJson([string]$RelativePath) {
    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Product E2E evidence is missing: $RelativePath"
    }
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -Depth 100
}

function Get-TextSha256([string]$Text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    return [Convert]::ToHexString(
        [System.Security.Cryptography.SHA256]::HashData($bytes)
    ).ToLowerInvariant()
}

function Get-RepositorySha256([string]$RelativePath) {
    $path = Join-Path $RepoRoot $RelativePath
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
}

function Assert-SemanticDigest([object]$Report, [string]$Name) {
    $recorded = [string]$Report.semantic_digest
    $Report.semantic_digest = ""
    $actual = Get-TextSha256 ($Report | ConvertTo-Json -Depth 100 -Compress)
    $Report.semantic_digest = $recorded
    if ($recorded -ne $actual) {
        throw "$Name semantic digest does not match its content"
    }
}

function Assert-Run([object]$Run, [int]$Width, [int]$Height, [int]$MinimumSamples) {
    if ($Run.requested_samples -lt $MinimumSamples -or
        $Run.requested_samples -ne $Run.accepted_samples -or
        $Run.accepted_samples -ne $Run.completed_samples -or
        $Run.resolution -ne "$Width`x$Height" -or
        -not $Run.metrics.finite -or
        $Run.metrics.width -ne $Width -or
        $Run.metrics.height -ne $Height -or
        $Run.metrics.positive_energy -le 0.0 -or
        $Run.metrics.luminance_standard_deviation -le 0.001 -or
        $Run.metrics.mean_spatial_gradient -le 0.0001 -or
        $Run.metrics.nonzero_pixel_fraction -le 0.5 -or
        $Run.convergence.low_to_final -le 0.0 -or
        $Run.convergence.mid_to_final -le 0.0 -or
        $Run.convergence.improvement_ratio -ge 1.0) {
        throw "Product E2E run failed its accounting, image, or convergence contract: $($Run.id)"
    }
}

$matrix = Read-RepositoryJson "contracts/product_e2e_matrix_v1.json"
$fixtureManifest = Read-RepositoryJson "tests/assets/product_e2e/fixture_manifest.json"
$functional = Read-RepositoryJson "docs/reports/phase_prv1r_functional_validation_v1.json"
$quality = Read-RepositoryJson "docs/reports/phase_prv1r_quality_validation_v1.json"
$visual = Read-RepositoryJson "docs/reports/phase_prv1r_visual_review_v1.json"

if ($matrix.schema -ne "ure.preview.product-e2e-matrix/1.0" -or
    @($matrix.tiers).Count -ne 4 -or
    $functional.schema -ne "ure.preview.product-e2e-evidence/1.0" -or
    $functional.status -ne "Passed" -or
    $functional.product_release_declared -or
    $quality.schema -ne "ure.phase_prv1r.quality-validation/1.0" -or
    $quality.status -ne "AutomatedPassed" -or
    $quality.product_release_declared -or
    $visual.schema -ne "ure.phase_prv1r.visual-review/1.0" -or
    $visual.status -ne "PassedWithinDeclaredBoundary" -or
    $visual.product_release_declared) {
    throw "Product E2E evidence metadata is invalid"
}

Assert-SemanticDigest $functional "Functional report"
Assert-SemanticDigest $quality "Quality report"
Assert-SemanticDigest $visual "Visual review"

if ($functional.fixture.sha256 -ne
        (Get-RepositorySha256 $functional.fixture.source) -or
    $functional.fixture.source_gltf_sha256 -ne
        (Get-RepositorySha256 $functional.fixture.source_gltf) -or
    $quality.source.source_gltf_sha256 -ne
        (Get-RepositorySha256 $quality.source.source_gltf) -or
    $quality.source.image_validator_sha256 -ne
        (Get-RepositorySha256 "scripts/validate_product_image.py")) {
    throw "Product E2E evidence source identity drifted"
}

$functionalRuns = @($functional.runs)
if ($functionalRuns.Count -ne 2 -or
    @($functional.cli).Count -ne 2 -or
    @($functional.maintained_call_matrix).Count -lt 7 -or
    -not $functional.parity.same_plan_identity -or
    -not $functional.parity.same_frame_content_identity -or
    -not $functional.parity.byte_identical_raw_secondary_check) {
    throw "Functional product call matrix is incomplete"
}
foreach ($run in $functionalRuns) {
    Assert-Run $run 854 480 16
}
foreach ($run in @($functional.cli)) {
    if ($run.accepted_samples -lt 16 -or
        $run.accepted_samples -ne $run.completed_samples -or
        $run.resolution -ne "854x480") {
        throw "CLI product evidence has incomplete work"
    }
}

$qualityRuns = @($quality.runs)
if ($qualityRuns.Count -ne 2 -or
    $quality.visual_review.record -ne "docs/reports/phase_prv1r_visual_review_v1.json" -or
    $qualityRuns[0].scene_sha256 -ne
        (Get-RepositorySha256 $qualityRuns[0].scene) -or
    $qualityRuns[1].scene_sha256 -ne
        (Get-RepositorySha256 $qualityRuns[1].scene)) {
    throw "Quality product evidence is incomplete or source-drifted"
}
Assert-Run $qualityRuns[0] 1280 720 128
Assert-Run $qualityRuns[1] 1920 1080 128

$visualArtifacts = @($visual.artifacts)
if ($visual.source_report -ne "docs/reports/phase_prv1r_quality_validation_v1.json" -or
    $visual.view_transform -ne "ure.preview.view.linear-srgb-reinhard-srgb8/1.0" -or
    $visualArtifacts.Count -ne 2) {
    throw "Visual review does not bind the complete quality evidence"
}
foreach ($artifact in $visualArtifacts) {
    $qualityRun = $qualityRuns | Where-Object id -eq $artifact.id
    if (@($qualityRun).Count -ne 1 -or
        $qualityRun.derived_png.sha256 -ne $artifact.png_sha256 -or
        $qualityRun.derived_png.view_transform -ne $visual.view_transform) {
        throw "Visual review artifact identity does not match quality evidence: $($artifact.id)"
    }
}

foreach ($fixture in $fixtureManifest.fixtures) {
    if ($fixture.sha256 -ne (Get-RepositorySha256 $fixture.path)) {
        throw "Retained product fixture drifted: $($fixture.id)"
    }
}

Write-Output "PRV.1R retained functional, quality, and visual ProductE2E evidence is consistent"
