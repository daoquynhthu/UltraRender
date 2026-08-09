param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$SdkStage,
    [Parameter(Mandatory = $true)][string]$RuntimeStage,
    [Parameter(Mandatory = $true)][string]$Flatc,
    [Parameter(Mandatory = $true)][string]$CMake,
    [Parameter(Mandatory = $true)][string]$Ninja
)

$ErrorActionPreference = "Stop"

function Assert-Package([string]$Root, [string]$Kind) {
    $manifestPath = Join-Path $Root "package_manifest.json"
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.package_kind -ne $Kind -or $manifest.publication_state -ne "Candidate") {
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
foreach ($path in @(
    (Join-Path $SdkStage "bin/ultrarender_mock_worker.exe"),
    (Join-Path $SdkStage "share/ultrarender/docs/Public_API_Candidate_Integration.md"),
    (Join-Path $SdkStage "third_party/flatbuffers/LICENSE.txt"),
    (Join-Path $RuntimeStage "share/ultrarender/schemas/ure_worker_candidate.fbs"),
    (Join-Path $RuntimeStage "share/ultrarender/docs/PB7_Compatibility_Report.md"),
    (Join-Path $RuntimeStage "share/licenses/flatbuffers/LICENSE.txt")
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required package boundary artifact is missing: $path"
    }
}
if ((Test-Path -LiteralPath (Join-Path $SdkStage "bin/ultrarender_runtime_candidate.dll")) -or
    (Test-Path -LiteralPath (Join-Path $RuntimeStage "include")) -or
    (Test-Path -LiteralPath (Join-Path $RuntimeStage "bin/ultrarender_mock_worker.exe"))) {
    throw "SDK/runtime package independence is violated"
}
if (Test-Path -LiteralPath $BuildDir) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

& $CMake -S $SourceDir -B $BuildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DURE_SDK_ROOT=$SdkStage" "-DURE_FLATC=$Flatc" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    throw "External client configure failed"
}
& $CMake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "External client build failed"
}

$runtime = Join-Path $RuntimeStage "bin/ultrarender_runtime_candidate.dll"
$worker = Join-Path $RuntimeStage "bin/ure_worker.exe"
$fixtures = Join-Path $SdkStage "share/ultrarender/fixtures"
& (Join-Path $BuildDir "ure_external_direct.exe") $runtime (Join-Path $fixtures "q4_procedural_scene/procedural_scene.urescene") (Join-Path $fixtures "q4_procedural_scene/procedural_scene.ure") (Join-Path $fixtures "pb5_public_boundary/single_scene.urepkg") (Join-Path $fixtures "pb5_public_boundary/ambiguous_scenes.urepkg")
if ($LASTEXITCODE -ne 0) {
    throw "External direct client failed"
}
& (Join-Path $BuildDir "ure_external_transaction.exe") $runtime (Join-Path $fixtures "pb6_scene_transaction_full/base_scene.urescene")
if ($LASTEXITCODE -ne 0) {
    throw "External transaction client failed"
}
& (Join-Path $BuildDir "ure_external_worker.exe") $worker $runtime (Join-Path $fixtures "q4_procedural_scene/procedural_scene.urescene")
if ($LASTEXITCODE -ne 0) {
    throw "External worker client failed"
}

Write-Output "Independent Candidate SDK/runtime package E2E passed"
