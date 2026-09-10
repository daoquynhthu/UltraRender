param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

function Read-Json([string]$Relative) {
    $path = Join-Path $RepoRoot $Relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required PRV.4 artifact is missing: $Relative"
    }
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -Depth 100
}

function Read-Text([string]$Relative) {
    $path = Join-Path $RepoRoot $Relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required PRV.4 source is missing: $Relative"
    }
    return Get-Content -Raw -LiteralPath $path
}

$diagnostics = Read-Json "contracts/diagnostics/product_diagnostic_catalog_v0.json"
$registry = Read-Json "contracts/registry/public_contract_registry.json"
$freeze = Read-Json "contracts/stability/core_1_0_freeze_review.json"
$functional = Read-Json "docs/reports/phase_prv4_functional_validation_v1.json"
$quality = Read-Json "docs/reports/phase_prv4_quality_validation_v1.json"
$visual = Read-Json "docs/reports/phase_prv4_visual_review_v1.json"
$publicHeader = Read-Text "contracts/generated/include/ultrarender/ure_loader.h"
$registryHeader = Read-Text "contracts/generated/include/ultrarender/ure_registry.h"
$productCmake = Read-Text "libs/ure_product/CMakeLists.txt"
$outputSource = Read-Text "libs/ure_product/src/product_output.cpp"
$workerSource = Read-Text "apps/ure_worker/main.cpp"
$workerRuntime = Read-Text "apps/ure_worker/runtime_client.cpp"
$workerTransport = Read-Text "libs/ure_client/src/worker_transport.cpp"
$directTransport = Read-Text "libs/ure_client/src/direct_transport.cpp"
$sdkConfig = Read-Text "cmake/UltraRenderPreviewSdkConfig.cmake"

$detailValues = @($diagnostics.details.value | ForEach-Object { [uint32]$_ })
if (($detailValues | Sort-Object -Unique).Count -ne $detailValues.Count) {
    throw "PRV.4 diagnostic detail values are duplicated"
}
$expectedDetails = @(
    320, 415,
    800, 801, 802, 803, 804, 805,
    815, 816, 817,
    821, 822, 823, 824, 825, 826, 827, 828, 829,
    830, 831, 832, 833, 834, 835, 836, 837, 838,
    840, 841, 842, 843, 844,
    850, 851, 852, 853, 854, 855,
    860, 861, 862, 863, 864, 865, 866, 867, 868, 869)
if ((Compare-Object $expectedDetails @(
        $detailValues | Where-Object { $_ -in $expectedDetails } | Sort-Object
    )).Count -ne 0) {
    throw "PRV.4 producer/output/transfer diagnostic coverage is incomplete"
}

$coreIds = @($registry.entries | Where-Object stability -eq "Core" |
    ForEach-Object { [uint32]$_.registry_id } | Sort-Object)
$frozenCoreIds = @($freeze.groups.registry_ids | ForEach-Object {
    [uint32]$_
} | Sort-Object)
if ((Compare-Object $coreIds $frozenCoreIds).Count -ne 0) {
    throw "PRV.4 changed the frozen Core identity set"
}
$measurementEntries = @($registry.entries | Where-Object {
    [uint64]$_.registry_id -ge 2147483724 -and
    [uint64]$_.registry_id -le 2147483774
})
if ($measurementEntries.Count -ne 51 -or
    @($measurementEntries | Where-Object {
        $_.stability -ne "UnstableExtension" -or
        $_.namespace -ne "unstable_experimental"
    }).Count -ne 0) {
    throw "PRV.4 expanded or stabilized the exact-build measurement boundary"
}

foreach ($pattern in @(
    'ure_measurement_frame_info_t', 'ure_measurement_plane_info_t',
    'ure_measurement_plane_copy_t', 'ure_output_request_t',
    'ure_output_manifest_t', 'ure_measurement_output_interface_t')) {
    if ($publicHeader -notmatch $pattern) {
        throw "PRV.4 generated loader surface is incomplete: $pattern"
    }
}
foreach ($pattern in @(
    'URE_FRAME_PLANE_BEAUTY_RAW', 'URE_FRAME_PLANE_VARIANCE',
    'URE_FRAME_PLANE_TAIL_EVENT_COUNT', 'URE_OUTPUT_FORMAT_OPENEXR',
    'URE_OUTPUT_FORMAT_HDR', 'URE_OUTPUT_FORMAT_PPM', 'URE_OUTPUT_FORMAT_BMP',
    'URE_TONE_MAP_LINEAR', 'URE_TONE_MAP_REINHARD', 'URE_TONE_MAP_ACES')) {
    if ($registryHeader -notmatch $pattern) {
        throw "PRV.4 generated registry surface is incomplete: $pattern"
    }
}
if ($productCmake -notmatch 'v3\.4\.14\.tar\.gz' -or
    $productCmake -notmatch '13C3327100A7B92E4C6A048DB03EF07EE2DB8E79BAA4C517C6FAE71E5B80034B' -or
    $outputSource -notmatch 'MultiPartOutputFile' -or
    $outputSource -notmatch 'MultiPartInputFile' -or
    $outputSource -notmatch 'atomic_replace' -or
    $outputSource -notmatch 'measurement-row-order') {
    throw "PRV.4 official OpenEXR or atomic artifact graph implementation is incomplete"
}
if ($workerSource -notmatch 'retain_legacy_color_plane' -or
    $workerSource -notmatch 'measurement_identity' -or
    $workerRuntime -notmatch 'UINT64_C\(512\) \* 1024 \* 1024' -or
    $workerTransport -notmatch 'UINT64_C\(512\) \* 1024 \* 1024' -or
    $directTransport -notmatch 'UINT64_C\(2147483648\)' -or
    $sdkConfig -notmatch 'PREVIEW_CLIENT_VERSION "0\.4"') {
    throw "PRV.4 Worker transfer, stable fallback, retention, or SDK boundary is incomplete"
}

if ($functional.schema -ne "ure.phase_prv4.functional-validation/1.0" -or
    $functional.status -ne "Passed" -or @($functional.runs).Count -ne 2 -or
    @($functional.runs | Where-Object {
        $_.resolution -ne "854x480" -or $_.samples -lt 16 -or
        $_.plane_count -lt 20 -or $_.partial_bytes -ne 4096 -or
        -not $_.image.metrics.finite
    }).Count -ne 0 -or @($functional.cli).Count -ne 2 -or
    @($functional.cli | Where-Object {
        $_.resolution -ne "854x480" -or $_.accepted_samples -lt 16 -or
        $_.completed_samples -ne $_.accepted_samples -or
        $_.output_artifacts -ne 4
    }).Count -ne 0) {
    throw "PRV.4 functional product-call matrix is incomplete"
}
if ($functional.runs[0].identities.measurement -ne
        $functional.runs[1].identities.measurement -or
    $functional.runs[0].artifacts.exr_sha256 -ne
        $functional.runs[1].artifacts.exr_sha256 -or
    $functional.runs[0].artifacts.checkpoint_sha256 -ne
        $functional.runs[1].artifacts.checkpoint_sha256 -or
    $functional.cli[0].artifacts.exr_sha256 -ne
        $functional.cli[1].artifacts.exr_sha256 -or
    $functional.cli[0].artifacts.checkpoint_sha256 -ne
        $functional.cli[1].artifacts.checkpoint_sha256) {
    throw "PRV.4 Direct/Worker authoritative artifact parity is incomplete"
}
if ($functional.external_sdk.gate -ne "test_external_client_package" -or
    @($functional.external_sdk.calls).Count -ne 2) {
    throw "PRV.4 independent SDK evidence is incomplete"
}

if ($quality.schema -ne "ure.phase_prv4.quality-validation/1.0" -or
    $quality.status -ne "AutomatedPassed" -or
    $quality.run.resolution -ne "1280x720" -or
    $quality.run.requested_samples -ne 500 -or
    $quality.run.completed_samples -ne 500 -or
    $quality.run.plane_count -lt 20 -or
    $quality.run.partial_bytes -ne 4096 -or
    -not $quality.image.metrics.finite -or
    $quality.image.metrics.nonzero_pixel_fraction -lt 0.99 -or
    $quality.image.metrics.negative_component_fraction -ne 0) {
    throw "PRV.4 720p/500-spp typed quality evidence is incomplete"
}
$qualityHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (
    Join-Path $RepoRoot "docs/reports/phase_prv4_quality_validation_v1.json")).Hash.ToLowerInvariant()
if ($visual.schema -ne "ure.phase_prv4.visual-review/1.0" -or
    $visual.status -ne "PassedWithinDeclaredBoundary" -or
    $visual.quality_report_sha256 -ne $qualityHash -or
    $visual.review.resolution -ne "1280x720" -or
    $visual.review.samples -ne 500 -or
    $visual.review.png_sha256 -ne $quality.artifacts.png_sha256 -or
    $visual.review.verdict -ne "PassWithVisiblePreDenoiseVariance") {
    throw "PRV.4 visual review is missing, overstated, or detached from evidence"
}

Write-Output "PRV.4 static audit passed: frozen Core, typed measurement/output extension, diagnostics, official OpenEXR, Direct/Worker/CLI/SDK E2E, and honest 500-spp visual evidence"
