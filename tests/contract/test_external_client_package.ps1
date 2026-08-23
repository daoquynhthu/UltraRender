param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$SdkStage,
    [Parameter(Mandatory = $true)][string]$RuntimeStage,
    [Parameter(Mandatory = $true)][string]$CMake,
    [Parameter(Mandatory = $true)][string]$Ninja
)

$ErrorActionPreference = "Stop"

function Assert-Package([string]$Root, [string]$Kind) {
    $manifestPath = Join-Path $Root "package_manifest.json"
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.package_kind -ne $Kind -or $manifest.publication_state -ne "Stable" -or
        $manifest.distribution_state -ne "DeclaredNotDistributed" -or
        $manifest.contract_version -ne "1.0") {
        throw "$Kind package manifest classification is invalid"
    }
    foreach ($entry in $manifest.files) {
        $path = Join-Path $Root $entry.path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Length -ne $entry.bytes -or
            (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() -ne $entry.sha256) {
            throw "$Kind package content mismatch: $($entry.path)"
        }
    }
}

Assert-Package $SdkStage "SDK"
Assert-Package $RuntimeStage "Runtime"
$sdkManifest = Get-Content -Raw -LiteralPath (Join-Path $SdkStage "package_manifest.json") | ConvertFrom-Json
if ($sdkManifest.stable_component -ne "Core ABI 1.x and Worker Protocol 1.x" -or
    $sdkManifest.exact_build_components -notcontains "Preview Client 0.3" -or
    $sdkManifest.exact_build_components -notcontains "Generated C++ protocol headers") {
    throw "SDK stable and exact-build component classifications are invalid"
}
foreach ($path in @(
    (Join-Path $SdkStage "bin/ultrarender_mock_worker.exe"),
    (Join-Path $SdkStage "share/ultrarender/docs/Public_API_Integration.md"),
    (Join-Path $SdkStage "share/ultrarender/docs/Public_API_Support_Policy.md"),
    (Join-Path $SdkStage "share/licenses/ultrarender/LICENSE"),
    (Join-Path $SdkStage "third_party/flatbuffers/LICENSE.txt"),
    (Join-Path $SdkStage "include/ultrarender/protocol/ure_product_v0_generated.h"),
    (Join-Path $SdkStage "include/ultrarender/protocol/protocol_codegen_manifest.json"),
    (Join-Path $SdkStage "include/ure/client/client.hpp"),
    (Join-Path $SdkStage "lib/ure_client.lib"),
    (Join-Path $SdkStage "lib/cmake/UltraRender/UltraRenderConfig.cmake"),
    (Join-Path $SdkStage "share/ultrarender/examples/preview_client/CMakeLists.txt"),
    (Join-Path $RuntimeStage "share/ultrarender/schemas/ure_worker_v1.fbs"),
    (Join-Path $RuntimeStage "share/ultrarender/docs/PB8_Stable_Compatibility_Report.md"),
    (Join-Path $RuntimeStage "share/licenses/ultrarender/LICENSE"),
    (Join-Path $RuntimeStage "share/licenses/flatbuffers/LICENSE.txt")
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required package boundary artifact is missing: $path"
    }
}
$protocolRoot = Join-Path $SdkStage "include/ultrarender/protocol"
$protocolManifest = Get-Content -Raw -LiteralPath (Join-Path $protocolRoot "protocol_codegen_manifest.json") | ConvertFrom-Json
if ($protocolManifest.generator -ne "flatc version 25.12.19" -or
    $protocolManifest.compatibility -ne "ExactBuild" -or
    $protocolManifest.headers.Count -ne 5) {
    throw "Pre-generated protocol header identity is invalid"
}
foreach ($entry in $protocolManifest.headers) {
    $path = Join-Path $protocolRoot $entry.path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Pre-generated protocol header mismatch: $($entry.path)"
    }
}
if ((Test-Path -LiteralPath (Join-Path $SdkStage "bin/ultrarender_runtime_1.dll")) -or
    (Test-Path -LiteralPath (Join-Path $RuntimeStage "include")) -or
    (Test-Path -LiteralPath (Join-Path $RuntimeStage "bin/ultrarender_mock_worker.exe"))) {
    throw "SDK/runtime package independence is violated"
}
if (Test-Path -LiteralPath $BuildDir) {
    $resolvedBuild = [IO.Path]::GetFullPath($BuildDir)
    if ([IO.Path]::GetFileName($resolvedBuild) -ne "pb8_external_client_build" -or
        $resolvedBuild -in @([IO.Path]::GetFullPath($SourceDir), [IO.Path]::GetFullPath($SdkStage), [IO.Path]::GetFullPath($RuntimeStage))) {
        throw "Refusing to remove an unexpected external-client build directory: $resolvedBuild"
    }
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$coreBuild = Join-Path $BuildDir "core"
$previewBuild = Join-Path $BuildDir "preview"
& $CMake -S $SourceDir -B $coreBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DURE_SDK_ROOT=$SdkStage" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    throw "External client configure failed"
}
& $CMake --build $coreBuild --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "External client build failed"
}
$previewSource = Join-Path $SdkStage "share/ultrarender/examples/preview_client"
$previewFiles = Get-ChildItem -LiteralPath $previewSource -File -Recurse
$flatcReferences = $previewFiles | Select-String -Pattern "flatc" -SimpleMatch
if ($null -ne $flatcReferences) {
    throw "The ordinary Preview integration example must not invoke flatc"
}
& $CMake -S $previewSource -B $previewBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DUltraRender_DIR=$(Join-Path $SdkStage 'lib/cmake/UltraRender')" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    throw "Preview reference client configure failed"
}
& $CMake --build $previewBuild --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Preview reference client build failed"
}

$runtime = Join-Path $RuntimeStage "bin/ultrarender_runtime_1.dll"
$worker = Join-Path $RuntimeStage "bin/ultrarender_worker_1.exe"
$fixtures = Join-Path $SdkStage "share/ultrarender/fixtures"
$images = Join-Path $BuildDir "rendered_images"
New-Item -ItemType Directory -Force -Path $images | Out-Null
& (Join-Path $coreBuild "ure_external_direct.exe") $runtime (Join-Path $fixtures "q4_procedural_scene/procedural_scene.urescene") (Join-Path $fixtures "q4_procedural_scene/procedural_scene.ure") (Join-Path $fixtures "pb5_public_boundary/single_scene.urepkg") (Join-Path $fixtures "pb5_public_boundary/ambiguous_scenes.urepkg") (Join-Path $images "direct_map.pfm") (Join-Path $images "direct_copy.pfm")
if ($LASTEXITCODE -ne 0) {
    throw "External direct client failed"
}
& (Join-Path $coreBuild "ure_external_transaction.exe") $runtime (Join-Path $fixtures "pb6_scene_transaction_full/base_scene.urescene") (Join-Path $images "transaction_replay.pfm") (Join-Path $images "transaction_replace.pfm")
if ($LASTEXITCODE -ne 0) {
    throw "External transaction client failed"
}
& (Join-Path $coreBuild "ure_external_worker.exe") $worker $runtime (Join-Path $fixtures "q4_procedural_scene/procedural_scene.urescene") (Join-Path $images "worker_first.pfm") (Join-Path $images "worker_restart.pfm")
if ($LASTEXITCODE -ne 0) {
    throw "External worker client failed"
}
& (Join-Path $previewBuild "ure_preview_direct.exe") $runtime (Join-Path $fixtures "q4_procedural_scene/procedural_scene.urescene") (Join-Path $images "preview_direct.pfm")
if ($LASTEXITCODE -ne 0) {
    throw "Preview Direct reference client failed"
}
& (Join-Path $previewBuild "ure_preview_worker.exe") $worker $runtime (Join-Path $fixtures "q4_procedural_scene/procedural_scene.urescene") (Join-Path $images "preview_worker.pfm")
if ($LASTEXITCODE -ne 0) {
    throw "Preview Worker reference client failed"
}

$expectedImages = @("direct_map.pfm", "direct_copy.pfm", "transaction_replay.pfm", "transaction_replace.pfm", "worker_first.pfm", "worker_restart.pfm", "preview_direct.pfm", "preview_worker.pfm")
foreach ($name in $expectedImages) {
    $path = Join-Path $images $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 32) {
        throw "External E2E did not produce a valid image artifact: $name"
    }
}

Write-Output "Independent Core 1.0 and exact-build Preview SDK image E2E passed ($($expectedImages.Count) images, no flatc invocation)"
