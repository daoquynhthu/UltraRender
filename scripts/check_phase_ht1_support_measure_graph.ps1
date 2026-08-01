param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HT.1 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HT.1 marker is missing in ${Path}: $pattern"
        }
    }
}

function Reject-Text {
    param([string]$Path, [string[]]$Patterns)
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $Path) -Raw
    foreach ($pattern in $Patterns) {
        if ([regex]::IsMatch(
                $text, $pattern,
                [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            throw "HT.1 SDK-free boundary violation in ${Path}: $pattern"
        }
    }
}

$header = "libs/ure_transport/include/ure/transport/support_measure_graph.hpp"

Require-Text $header @(
    "struct PathEventGrammar", "struct CompiledPathEventGrammar",
    "compile_path_event_grammar", "compile_support_partition_graph",
    "SupportHole", "OutsideTarget", "classify_path_support",
    "struct MeasureTransformDescriptor", "MisHeuristic",
    "compile_composition_plan", "evaluate_mis_weights",
    "EstimateLayer", "GeneralizedResampling",
    "struct GrisProvenance", "validate_gris_provenance",
    "struct MarkovChainReplicate", "aggregate_markov_replicates",
    "PackedMisProgram", "pack_mis_program")
Require-Text "libs/ure_transport/src/support_measure_graph.cpp" @(
    "epsilon_closure", "ProductState", "technique_mask",
    "SupportPartitionIssue::SupportHole",
    "SupportPartitionIssue::OutsideTarget",
    "maximum_product_states")
Require-Text "libs/ure_transport/src/composition.cpp" @(
    "MeasureTransformKind::SampleJacobian",
    "CompositionFamily::MultipleImportanceSampling",
    "CompositionFamily::GeneralizedResampling",
    "CompositionFamily::MarkovChainReplicate",
    "EstimateLayer::Preview", "candidate_weight_sum",
    "between_replicate_variance")
Require-Text "libs/ure_core/src/support_measure_device.cuh" @(
    "static __device__ bool evaluate_packed_mis_weights",
    "technique_mask", "sample_jacobians")
Require-Text "tests/host/test_support_measure_graph.cpp" @(
    "test_bounded_grammar_compilation",
    "test_exact_support_partitions",
    "test_hole_outside_and_budget_rejection",
    "test_measure_plan_and_analytic_mis",
    "test_preview_is_a_separate_output_layer",
    "test_gris_and_markov_provenance")
Require-Text "tests/gpu/test_support_measure_composition.cu" @(
    "composition_kernel", "compile_support_partition_graph",
    "compile_composition_plan", "pack_mis_program",
    "host.expectation - 7.0")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "support_measure_graph.cpp", "composition.cpp",
    "support_measure_graph_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "ure/transport/support_measure_graph.hpp",
    "kSupportMeasureGraphVersion")

Reject-Text $header @(
    '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
    '#include\s*[<"]d3d', '#include\s*[<"]windows',
    '#include\s*[<"]pxr/')

Write-Host "HT.1 Support/Measure Graph static audit passed: bounded grammar, exact support partitions, measure transforms, MIS/GRIS/MCMC layers and GPU composition gate present."
