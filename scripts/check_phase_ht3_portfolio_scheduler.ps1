param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HT.3 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HT.3 marker is missing in ${Path}: $pattern"
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
            throw "HT.3 boundary violation in ${Path}: $pattern"
        }
    }
}

$header = "libs/ure_transport/include/ure/transport/portfolio.hpp"
$scheduler = "libs/ure_transport/src/portfolio_schedule.cpp"
$drift = "libs/ure_transport/src/portfolio_drift.cpp"
$shard = "libs/ure_transport/src/portfolio_shard.cpp"

Require-Text $header @(
    "struct PortfolioWorkDomain", "struct PortfolioBudget",
    "struct PortfolioPolicy", "struct PortfolioCandidate",
    "struct PortfolioCovarianceEdge", "struct PortfolioSchedule",
    "schedule_portfolio", "minimum_exploration_samples",
    "starvation_recovery_samples", "struct PortfolioDriftPolicy",
    "RepilotCandidate", "RepilotAll",
    "struct PortfolioScheduleShard", "validate_portfolio_shard_coverage")
Require-Text $scheduler @(
    "qualification_report_identity", "pilot_provenance_identity",
    "production_namespace_identity", "exploration_budget_fraction",
    "minimum_gain_per_nanosecond", "CorrelationModel::MarkovChain",
    "Covariance is not positive semidefinite|covariance is not positive semidefinite",
    "starvation_epoch_limit", "compute_schedule_identity")
Require-Text $drift @(
    "mean_z_threshold", "maximum_variance_ratio",
    "maximum_cost_ratio", "consecutive_breach_count",
    "global_repilot_fraction", "Non-monotonic portfolio drift epoch")
Require-Text $shard @(
    "executable_identity", "execution_semantics_identities",
    "sample_namespace_identity", "chain_namespace_identity",
    "exact_coverage", "DuplicateShard", "MissingCoverage", "Overlap")
Require-Text "libs/ure_reconstruction/include/ure/reconstruction/measurement.hpp" @(
    "portfolio_schedule_identity")
Require-Text "libs/ure_reconstruction/include/ure/reconstruction/portfolio_measurement.hpp" @(
    "make_portfolio_measurement_provenance")
Require-Text "libs/ure_reconstruction/src/checkpoint.cpp" @(
    "kLegacyMeasurementCheckpointVersion", "supported_checkpoint_version",
    "portfolio_schedule_identity", "metadata.v2")
Require-Text "libs/ure_reconstruction/src/measurement.cpp" @(
    "left.portfolio_schedule_identity")
Require-Text "tests/host/test_portfolio_scheduler.cpp" @(
    "test_cost_covariance_and_starvation_schedule",
    "test_drift_and_repilot_policy", "test_distributed_shard_coverage",
    "make_portfolio_measurement_provenance", "chain_covariance")
Require-Text "tests/host/test_measurement_bundle.cpp" @(
    "other-schedule", "legacy_artifact", "portfolio_schedule_identity")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "portfolio_schedule.cpp", "portfolio_drift.cpp",
    "portfolio_shard.cpp", "portfolio_measurement.cpp",
    "portfolio_scheduler_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "ure/transport/portfolio.hpp",
    "ure/reconstruction/portfolio_measurement.hpp",
    "kPortfolioContractVersion")

foreach ($path in @(
        $header, $scheduler, $drift, $shard,
        "libs/ure_reconstruction/include/ure/reconstruction/portfolio_measurement.hpp",
        "libs/ure_reconstruction/src/portfolio_measurement.cpp")) {
    Reject-Text $path @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/', '\bIntegratorMode\b')
}

Write-Host "HT.3 portfolio scheduler static audit passed: content-bound budget allocation, independent paired covariance, exploration/starvation protection, drift-triggered re-pilot, deterministic shards, exact coverage and MeasurementBundle schedule provenance are present."
