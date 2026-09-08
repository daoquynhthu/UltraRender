param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

function Read-Json([string]$Relative) {
    $path = Join-Path $RepoRoot $Relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required PRV.3 artifact is missing: $Relative"
    }
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -Depth 100
}

function Read-Text([string]$Relative) {
    $path = Join-Path $RepoRoot $Relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required PRV.3 source is missing: $Relative"
    }
    return Get-Content -Raw -LiteralPath $path
}

$diagnostics = Read-Json "contracts/diagnostics/product_diagnostic_catalog_v0.json"
$registry = Read-Json "contracts/registry/public_contract_registry.json"
$freeze = Read-Json "contracts/stability/core_1_0_freeze_review.json"
$fixtures = Read-Json "tests/assets/product_e2e/prv3/fixture_manifest.json"
$functional = Read-Json "docs/reports/phase_prv3_functional_validation_v1.json"
$quality = Read-Json "docs/reports/phase_prv3_quality_validation_v1.json"
$visual = Read-Json "docs/reports/phase_prv3_visual_review_v1.json"
$publicHeader = Read-Text "contracts/generated/include/ultrarender/ure_loader.h"
$registryHeader = Read-Text "contracts/generated/include/ultrarender/ure_registry.h"
$materialSource = Read-Text "libs/ure_product/src/product_material.cpp"
$realizerSource = Read-Text "libs/ure_product/src/product_scene.cpp"
$wavefrontSource = Read-Text "libs/ure_core/src/path_tracer_wavefront.cuh"
$lifecycleSource = Read-Text "libs/ure_contract/src/lifecycle.cpp"
$runtimeAdapterSource = Read-Text "libs/ure_contract/src/runtime_adapter.cpp"
$sceneToolAdapterSource = Read-Text "libs/ure_contract/src/scene_tool_adapter.cpp"

$expectedDetails = @(700, 701, 702, 703, 704, 705, 706, 711, 712, 713, 714)
$actualDetails = @($diagnostics.details | Where-Object {
    $_.value -in $expectedDetails
})
if ($actualDetails.Count -ne $expectedDetails.Count -or
    (Compare-Object $expectedDetails @(
        $actualDetails.value | Sort-Object)).Count -ne 0) {
    throw "PRV.3 material diagnostic details are incomplete or duplicated"
}
$knownResults = @($diagnostics.results.name)
foreach ($detail in $actualDetails) {
    if ([string]::IsNullOrWhiteSpace($detail.owner) -or
        [string]::IsNullOrWhiteSpace($detail.name) -or
        $detail.result -notin $knownResults) {
        throw "PRV.3 diagnostic entry is incomplete: $($detail.value)"
    }
}

$extensionEntries = @($registry.entries | Where-Object {
    $_.canonical_name -match 'product_job|scene_tool'
})
if ($extensionEntries.Count -ne 24 -or
    @($extensionEntries | Where-Object {
        $_.stability -ne "UnstableExtension" -or
        $_.namespace -ne "unstable_experimental"
    }).Count -ne 0) {
    throw "PRV.3 changed the exact-build extension stability boundary"
}
$coreIds = @($registry.entries | Where-Object stability -eq "Core" |
    ForEach-Object { [uint32]$_.registry_id } | Sort-Object)
$frozenCoreIds = @($freeze.groups.registry_ids | ForEach-Object {
    [uint32]$_
} | Sort-Object)
if ((Compare-Object $coreIds $frozenCoreIds).Count -ne 0) {
    throw "PRV.3 changed the frozen Core identity set"
}
if ($lifecycleSource -notmatch 'descriptor->version_minor = 4' -or
    $sceneToolAdapterSource -notmatch '\{sizeof\(ure_scene_tool_interface_t\), 0, 2\}' -or
    $publicHeader -notmatch 'eligible_integrator_modes' -or
    $publicHeader -notmatch 'adapter_loss_report_size' -or
    $runtimeAdapterSource -notmatch '"material_selector"' -or
    $runtimeAdapterSource -notmatch '"preset_name"' -or
    $runtimeAdapterSource -notmatch '"material_program_set_identity"' -or
    $runtimeAdapterSource -notmatch '"material_program_count"' -or
    $runtimeAdapterSource -notmatch '"adapter_loss_report_size"') {
    throw "PRV.3 generated ProductJob 0.4 or Scene Tool 0.2 surface is incomplete"
}

if ($fixtures.schema -ne "ure.preview.prv3-fixture-manifest/1.0" -or
    @($fixtures.fixtures).Count -ne 10) {
    throw "PRV.3 retained fixture matrix is incomplete"
}
$expectedCapabilities = @(
    "material_matrix_functional", "material_matrix_quality",
    "glass_functional", "glass_quality", "mie_volume_functional",
    "mie_volume_quality", "diffractive_functional",
    "diffractive_quality", "fluorescent_functional",
    "fluorescent_quality")
if ((Compare-Object $expectedCapabilities @(
        $fixtures.fixtures.id | Sort-Object)).Count -ne 0) {
    throw "PRV.3 retained fixture identities drifted"
}
foreach ($entry in @($fixtures.generator) + @($fixtures.sources) +
                    @($fixtures.resources) + @($fixtures.fixtures)) {
    $path = Join-Path $RepoRoot $entry.path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() -ne
            $entry.sha256) {
        throw "PRV.3 retained fixture hash mismatch: $($entry.path)"
    }
}

if ($functional.schema -ne "ure.preview.prv3-material-e2e/1.0" -or
    $functional.status -ne "Passed" -or $functional.samples -lt 16 -or
    @($functional.runs).Count -ne 12 -or
    @($functional.runs | Where-Object {
        $_.requested_samples -lt 16 -or $_.accepted_samples -ne $_.requested_samples -or
        $_.completed_samples -ne $_.requested_samples -or $_.resolution -ne "854x480" -or
        $_.qualified_integrator_modes -eq 0 -or $_.executed_integrator_modes -eq 0 -or
        -not $_.metrics.metrics.finite
    }).Count -ne 0) {
    throw "PRV.3 functional ProductE2E evidence is incomplete"
}
if (@($functional.transport_parity).Count -ne 6 -or
    @($functional.transport_parity | Where-Object {
        $_.relative_rgb_nrmse -gt 0.005
    }).Count -ne 0) {
    throw "PRV.3 Direct/Worker image parity exceeds the perceptual bound"
}
foreach ($field in @(
    "runtime_sha256", "worker_sha256", "cli_sha256",
    "scenario_runner_sha256", "image_validator_sha256",
    "image_comparator_sha256", "orchestration_sha256")) {
    if ($functional.source.$field -notmatch '^[0-9a-f]{64}$') {
        throw "PRV.3 functional provenance is incomplete: $field"
    }
}
$authoringRuns = @($functional.authoring.runs)
$authoringOperations = @(
    $functional.authoring.operations.gltf_build,
    $functional.authoring.operations.preset_realization,
    $functional.authoring.operations.materialx_export,
    $functional.authoring.operations.materialx_import)
if ($authoringRuns.Count -ne 6 -or
    @($authoringRuns | Where-Object {
        $_.accepted_samples -ne $_.requested_samples -or
        $_.completed_samples -ne $_.requested_samples -or
        $_.qualified_integrator_modes -eq 0 -or
        $_.executed_integrator_modes -eq 0 -or
        -not $_.metrics.metrics.finite
    }).Count -ne 0 -or
    @($authoringRuns | Where-Object {
        $_.scene -eq "authored_gltf" -and
        ($_.resolution -ne "64x64" -or $_.requested_samples -ne 64)
    }).Count -ne 0 -or
    @($authoringRuns | Where-Object {
        $_.scene -ne "authored_gltf" -and
        ($_.resolution -ne "854x480" -or $_.requested_samples -lt 16)
    }).Count -ne 0 -or
    @($functional.authoring.transport_parity).Count -ne 3 -or
    @($functional.authoring.transport_parity | Where-Object {
        $_.relative_rgb_nrmse -gt 0.005
    }).Count -ne 0) {
    throw "PRV.3 authored glTF/preset/MaterialX ProductJob evidence is incomplete"
}
foreach ($operation in $authoringOperations) {
    if (@($operation).Count -ne 2 -or
        @($operation | Where-Object {
            -not $_.ok -or
            $_.material_program_set_identity -notmatch '^[0-9a-f]{64}$'
        }).Count -ne 0) {
        throw "PRV.3 authoring operation omitted canonical material evidence"
    }
}
if ($quality.schema -ne "ure.phase_prv3.quality-validation/1.0" -or
    $quality.status -ne "AutomatedPassed" -or
    @($quality.runs).Count -ne 5 -or
    @($quality.runs | Where-Object {
        $_.requested_samples -ne 500 -or $_.accepted_samples -ne 500 -or
        $_.completed_samples -ne 500 -or $_.resolution -ne "1280x720" -or
        $_.qualified_integrator_modes -eq 0 -or
        $_.executed_integrator_modes -eq 0 -or
        -not $_.metrics.finite -or
        $_.convergence.mid_to_final -ge $_.convergence.low_to_final
    }).Count -ne 0) {
    throw "PRV.3 500-spp ProductQuality evidence is incomplete"
}
foreach ($field in @(
    "fixture_manifest_sha256", "runtime_sha256", "worker_sha256",
    "scenario_runner_sha256", "image_validator_sha256",
    "orchestration_sha256", "process_wrapper_sha256",
    "image_comparator_sha256", "system_device_identity")) {
    if ($quality.source.$field -notmatch '^[0-9a-f]{64}$') {
        throw "PRV.3 quality provenance is incomplete: $field"
    }
}
if ($visual.schema -ne "ure.phase_prv3.visual-review/1.0" -or
    $visual.status -ne "PassedWithinDeclaredBoundary" -or
    @($visual.reviews).Count -ne 5 -or
    @($visual.reviews | Where-Object {
        $_.samples -ne 500 -or $_.resolution -ne "1280x720" -or
        $_.verdict -ne "Pass"
    }).Count -ne 0) {
    throw "PRV.3 500-spp visual review is incomplete"
}
$functionalHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (
    Join-Path $RepoRoot "docs/reports/phase_prv3_functional_validation_v1.json")).Hash.ToLowerInvariant()
$expectedAuthoringReviews = @{
    "gltf-derived-product-render" = @("authored_gltf", "64x64", 64)
    "preset-derived-product-render" = @("authored_preset", "854x480", 16)
    "materialx-derived-product-render" = @("authored_materialx", "854x480", 16)
}
if ($visual.supplementary_authoring_report_sha256 -ne $functionalHash -or
    @($visual.authoring_reviews).Count -ne 3) {
    throw "PRV.3 authoring visual review is not bound to functional evidence"
}
foreach ($review in $visual.authoring_reviews) {
    $expected = $expectedAuthoringReviews[$review.id]
    $run = $authoringRuns | Where-Object {
        $_.scene -eq $expected[0] -and $_.transport -eq "direct"
    } | Select-Object -First 1
    if ($null -eq $expected -or $null -eq $run -or
        $review.resolution -ne $expected[1] -or
        $review.samples -ne $expected[2] -or
        $review.png_sha256 -ne $run.png_sha256 -or
        $review.verdict -ne "Pass") {
        throw "PRV.3 authoring visual review drifted: $($review.id)"
    }
}

if ($materialSource -notmatch 'canonicalize_product_materials' -or
    $materialSource -notmatch 'eligible_integrator_modes' -or
    $realizerSource -notmatch 'validate_material_payloads' -or
    $realizerSource -notmatch 'URE-PRV3-MATERIAL-TEXTURE-' -or
    $wavefrontSource -notmatch 'fminf\(t_hit, max_allowed\)') {
    throw "PRV.3 material authority, resource preflight, or bounded-medium fix is absent"
}

Write-Output "PRV.3 static audit passed: canonical material authority, authored adapter renders, 11 diagnostics, frozen Core, retained E2E matrix, and 500-spp visual evidence"
