param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HT.0 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HT.0 marker is missing in ${Path}: $pattern"
        }
    }
}

function Reject-Text {
    param([string]$Path, [string[]]$Patterns)
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $Path) -Raw
    foreach ($pattern in $Patterns) {
        if ([regex]::IsMatch($text, $pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            throw "HT.0 contract violates its boundary in ${Path}: $pattern"
        }
    }
}

Require-Text "libs/ure_transport/include/ure/transport/technique_graph.hpp" @(
    "struct TechniqueDescriptor", "parameter_identity", "struct TechniqueGraph",
    "struct TechniqueResourceDescriptor", "cost_estimate_known",
    "scratch_bound_known", "backend_capability_identity",
    "requires_shared_spectral_primary_sample",
    "enum class TechniqueEdgeKind", "validate_technique_graph",
    "compute_technique_graph_identity")
Require-Text "libs/ure_transport/include/ure/transport/legacy_technique_preset.hpp" @(
    "LegacyTechniquePreset", "LegacyExecutionRoute", "LegacyRejectionClass",
    "Mathematical", "Resource", "Unimplemented",
    "compile_legacy_technique_preset", "legacy_preset_equivalent")
Require-Text "libs/ure_transport/src/legacy_technique_preset.cpp" @(
    "WavefrontPathTracing", "PathGuiding", "RestirDirect", "RestirPathReuse",
    "SpecularManifold", "BidirectionalPathTracing", "VertexConnectionMerging",
    "PrimarySampleSpaceMlt", "parameter_identity")
Require-Text "libs/ure_core/src/gpu_engine_impl.cpp" @(
    "compile_legacy_technique_preset", "legacy_preset_equivalent",
    "preset.route.resolved_mode", "preset.route.restir_direct",
    "preset.route.restir_path")
Require-Text "tests/host/test_technique_graph.cpp" @(
    "test_all_legacy_presets_are_described", "test_descriptor_semantics",
    "test_graph_validation_rejects_corruption",
    "test_structured_route_rejections",
    "test_independent_flags_map_to_one_route")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "legacy_technique_preset.cpp", "technique_graph.cpp",
    "technique_graph_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "compile_legacy_technique_preset")
Require-Text "libs/ure_transport/CMakeLists.txt" @(
    "src/legacy_technique_preset.cpp", "src/technique_graph.cpp")

foreach ($header in @(
    "libs/ure_transport/include/ure/transport/technique_graph.hpp",
    "libs/ure_transport/include/ure/transport/legacy_technique_preset.hpp")) {
    Reject-Text $header @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/')
}
Reject-Text "libs/ure_transport/include/ure/transport/technique_graph.hpp" @("IntegratorMode")
Reject-Text "libs/ure_transport/src/technique_graph.cpp" @("IntegratorMode")

$ledgerPath = Join-Path $RepoRoot "docs/research/ht0/legacy_mode_switch_ledger.json"
$ledger = Get-Content -LiteralPath $ledgerPath -Raw | ConvertFrom-Json
$expected = @{}
foreach ($entry in $ledger.entries) {
    if ($expected.ContainsKey($entry.path)) {
        throw "HT.0 mode ledger contains a duplicate path: $($entry.path)"
    }
    $expected[$entry.path] = [int]$entry.occurrences
}
$actual = @{}
$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $RepoRoot "libs"), (Join-Path $RepoRoot "apps") -File -Recurse | Where-Object {
    $_.Extension -in @(".cpp", ".cu", ".cuh", ".hpp", ".h") -and
    $_.FullName -notmatch '[\\/]generated[\\/]'
}
foreach ($file in $sourceFiles) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    $count = [regex]::Matches($text, "IntegratorMode::").Count
    if ($count -gt 0) {
        $relative = [System.IO.Path]::GetRelativePath($RepoRoot, $file.FullName).Replace("\", "/")
        $actual[$relative] = $count
    }
}
foreach ($path in $actual.Keys) {
    if (-not $expected.ContainsKey($path)) {
        throw "HT.0 found a new unclassified mode-only decision site: $path"
    }
    if ($actual[$path] -ne $expected[$path]) {
        throw "HT.0 IntegratorMode occurrence drift in ${path}: expected $($expected[$path]), actual $($actual[$path])"
    }
}
foreach ($path in $expected.Keys) {
    if (-not $actual.ContainsKey($path)) {
        throw "HT.0 mode ledger path disappeared without migration: $path"
    }
}

$matrixPath = Join-Path $RepoRoot "docs/research/ht0/legacy_rejection_matrix.json"
$matrix = Get-Content -LiteralPath $matrixPath -Raw | ConvertFrom-Json
if ($matrix.rejections.Count -ne 16) {
    throw "HT.0 rejection matrix must classify all 16 legacy rejection codes"
}
$rejectionHeader = Get-Content -LiteralPath (Join-Path $RepoRoot "libs/ure_transport/include/ure/transport/legacy_technique_preset.hpp") -Raw
$rejectionSource = Get-Content -LiteralPath (Join-Path $RepoRoot "libs/ure_transport/src/legacy_technique_preset.cpp") -Raw
foreach ($entry in $matrix.rejections) {
    if ($rejectionHeader -notmatch "\b$([regex]::Escape($entry.code))\b") {
        throw "HT.0 rejection code is absent from the contract: $($entry.code)"
    }
    if ($rejectionSource -notmatch "LegacyRejectionCode::$([regex]::Escape($entry.code))") {
        throw "HT.0 rejection code has no executable classifier: $($entry.code)"
    }
    if ($entry.class -notin @("Mathematical", "Resource", "Unimplemented")) {
        throw "HT.0 rejection has an invalid class: $($entry.code)"
    }
}

Write-Host "HT.0 technique graph static audit passed: $($expected.Count) frozen legacy decision files, $($matrix.rejections.Count) structured rejection codes"
