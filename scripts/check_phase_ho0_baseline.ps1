param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"

function Read-JsonFile {
    param([string]$Path)
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -Depth 100
}

function Get-RelativeRepositoryPath {
    param([string]$Path)
    return [System.IO.Path]::GetRelativePath($RepoRoot, $Path).Replace('\', '/')
}

function Assert-SourceAssertion {
    param($Assertion, [string]$Label)
    $fullPath = Join-Path $RepoRoot $Assertion.path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "$Label references missing source: $($Assertion.path)"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    if (-not [regex]::IsMatch($text, [string]$Assertion.pattern)) {
        throw "$Label source pattern is absent in $($Assertion.path): $($Assertion.pattern)"
    }
}

function Assert-UniqueIds {
    param($Items, [string]$Label)
    $ids = @($Items | ForEach-Object { [string]$_.id })
    if ($ids.Count -ne @($ids | Sort-Object -Unique).Count -or $ids -contains "") {
        throw "$Label IDs must be nonempty and unique"
    }
}

$dataRoot = Join-Path $RepoRoot "docs/research/ho0"
$ledgerPath = Join-Path $dataRoot "capability_boundary_ledger.json"
$integratorPath = Join-Path $dataRoot "integrator_inventory.json"
$measurementPath = Join-Path $dataRoot "measurement_gap_matrix.json"
$statePath = Join-Path $dataRoot "state_ownership_map.json"
$schemaPath = Join-Path $dataRoot "research_capsule.schema.json"
$benchmarkPath = Join-Path $dataRoot "benchmark_family_manifest.json"

$ledger = Read-JsonFile $ledgerPath
$integrators = Read-JsonFile $integratorPath
$measurement = Read-JsonFile $measurementPath
$state = Read-JsonFile $statePath
$capsuleSchema = Read-JsonFile $schemaPath
$benchmarks = Read-JsonFile $benchmarkPath

if ($ledger.schema -ne "ure.research.capability-boundary-ledger/1.0") {
    throw "Unexpected capability ledger schema"
}
if ($integrators.schema -ne "ure.research.integrator-inventory/1.0") {
    throw "Unexpected integrator inventory schema"
}
if ($measurement.schema -ne "ure.research.measurement-gap-matrix/1.0") {
    throw "Unexpected measurement gap schema"
}
if ($state.schema -ne "ure.research.state-ownership-map/1.0") {
    throw "Unexpected state ownership schema"
}
if ($capsuleSchema.'$id' -ne "ure.research.capsule/1.0") {
    throw "Unexpected Research Capsule schema"
}
if ($benchmarks.schema -ne "ure.research.high-order-benchmark-family/1.0") {
    throw "Unexpected high-order benchmark schema"
}

Assert-UniqueIds $ledger.boundaries "Capability boundary"
Assert-UniqueIds $ledger.resolved_boundaries "Resolved capability boundary"
$allowedCategories = @($ledger.categories)
foreach ($boundary in $ledger.boundaries) {
    if ($allowedCategories -notcontains $boundary.category) {
        throw "Boundary $($boundary.id) has unknown category $($boundary.category)"
    }
    if ([string]::IsNullOrWhiteSpace($boundary.owner_phase) -or
        [string]::IsNullOrWhiteSpace($boundary.disposition) -or
        [string]::IsNullOrWhiteSpace($boundary.summary)) {
        throw "Boundary $($boundary.id) is missing owner, disposition, or summary"
    }
    if ([int]$boundary.expected_match_count -le 0 -or @($boundary.coverage).Count -eq 0 -or @($boundary.anchors).Count -eq 0) {
        throw "Boundary $($boundary.id) lacks executable coverage"
    }
    foreach ($anchor in $boundary.anchors) {
        Assert-SourceAssertion $anchor "Boundary $($boundary.id)"
    }
}
foreach ($boundary in $ledger.resolved_boundaries) {
    if ($allowedCategories -notcontains $boundary.category -or
        [string]::IsNullOrWhiteSpace($boundary.owner_phase) -or
        [string]::IsNullOrWhiteSpace($boundary.resolution) -or
        @($boundary.anchors).Count -eq 0) {
        throw "Resolved boundary $($boundary.id) lacks category, owner, resolution, or anchors"
    }
    foreach ($anchor in $boundary.anchors) {
        Assert-SourceAssertion $anchor "Resolved boundary $($boundary.id)"
    }
}

$extensions = @($ledger.scan.extensions)
$discovered = [System.Collections.Generic.List[object]]::new()
foreach ($root in $ledger.scan.roots) {
    $fullRoot = Join-Path $RepoRoot $root
    if (-not (Test-Path -LiteralPath $fullRoot -PathType Container)) {
        throw "Capability scan root is missing: $root"
    }
    $files = Get-ChildItem -LiteralPath $fullRoot -File -Recurse | Where-Object {
        $extensions -contains $_.Extension
    } | Sort-Object FullName
    foreach ($file in $files) {
        $relative = Get-RelativeRepositoryPath $file.FullName
        foreach ($match in @(Select-String -LiteralPath $file.FullName -Pattern $ledger.scan.pattern)) {
            $owners = [System.Collections.Generic.List[string]]::new()
            foreach ($boundary in $ledger.boundaries) {
                foreach ($rule in $boundary.coverage) {
                    if ([regex]::IsMatch($relative, [string]$rule.path_regex) -and
                        [regex]::IsMatch($match.Line, [string]$rule.line_regex)) {
                        $owners.Add([string]$boundary.id)
                        break
                    }
                }
            }
            $discovered.Add([pscustomobject]@{
                path = $relative
                line = [int]$match.LineNumber
                text = $match.Line.Trim()
                owners = @($owners)
            })
        }
    }
}

$unclassified = @($discovered | Where-Object { $_.owners.Count -eq 0 })
$multiplyClassified = @($discovered | Where-Object { $_.owners.Count -gt 1 })
if ($unclassified.Count -ne 0) {
    $sample = $unclassified | Select-Object -First 5 | ConvertTo-Json -Compress
    throw "Capability boundary scan has $($unclassified.Count) unclassified lines: $sample"
}
if ($multiplyClassified.Count -ne 0) {
    $sample = $multiplyClassified | Select-Object -First 5 | ConvertTo-Json -Compress
    throw "Capability boundary scan has $($multiplyClassified.Count) multiply classified lines: $sample"
}
if ($discovered.Count -ne [int]$ledger.scan.expected_total_matches) {
    throw "Capability boundary count changed: expected $($ledger.scan.expected_total_matches), found $($discovered.Count)"
}

$boundaryCounts = [ordered]@{}
foreach ($boundary in $ledger.boundaries) {
    $count = @($discovered | Where-Object { $_.owners[0] -eq $boundary.id }).Count
    if ($count -ne [int]$boundary.expected_match_count) {
        throw "Boundary $($boundary.id) count changed: expected $($boundary.expected_match_count), found $count"
    }
    $boundaryCounts[$boundary.id] = $count
}

Assert-UniqueIds $integrators.techniques "Integrator technique"
$expectedModes = @("Wavefront", "PathGuided", "RestirDI", "SpecularManifold", "MLT", "RestirPT", "BDPT", "VCM")
$actualModes = @($integrators.techniques | ForEach-Object { [string]$_.legacy_mode } | Sort-Object)
if (Compare-Object ($expectedModes | Sort-Object) $actualModes) {
    throw "Integrator inventory does not exactly cover IntegratorMode"
}
foreach ($assertion in $integrators.central_debt.source_assertions) {
    Assert-SourceAssertion $assertion "Integrator central debt"
}
$requiredTechniqueFields = @(
    "target_role", "observable", "sample_space", "support", "density_contract",
    "normalization", "correlation", "bias_class", "persistent_state",
    "differentiability", "combination_status"
)
foreach ($technique in $integrators.techniques) {
    foreach ($field in $requiredTechniqueFields) {
        if ([string]::IsNullOrWhiteSpace([string]$technique.$field)) {
            throw "Technique $($technique.id) is missing $field"
        }
    }
    foreach ($assertion in $technique.source_assertions) {
        Assert-SourceAssertion $assertion "Technique $($technique.id)"
    }
}

$planes = @($measurement.current_contract.planes)
if ($planes.Count -ne 6) {
    throw "Current public AOV inventory changed from six planes"
}
foreach ($plane in $planes) {
    $pattern = "(?m)^\s*$([regex]::Escape([string]$plane.name))\s*="
    $renderText = Get-Content -LiteralPath (Join-Path $RepoRoot "libs/ure_core/include/ure/render.hpp") -Raw
    if (-not [regex]::IsMatch($renderText, $pattern)) {
        throw "Current AOV plane is absent from AovType: $($plane.name)"
    }
}
foreach ($assertion in $measurement.current_contract.source_assertions) {
    Assert-SourceAssertion $assertion "Current measurement contract"
}
Assert-UniqueIds $measurement.gaps "Measurement gap"
if (@($measurement.gaps).Count -lt 10) {
    throw "Measurement gap matrix no longer covers the ten HO.0 information classes"
}
foreach ($gap in $measurement.gaps) {
    if (@($gap.consumers).Count -eq 0 -or [string]::IsNullOrWhiteSpace($gap.cost_class) -or
        [string]::IsNullOrWhiteSpace($gap.retention) -or [string]::IsNullOrWhiteSpace($gap.loss_if_omitted)) {
        throw "Measurement gap $($gap.id) lacks consumer, cost, retention, or loss semantics"
    }
    foreach ($assertion in $gap.source_assertions) {
        Assert-SourceAssertion $assertion "Measurement gap $($gap.id)"
    }
}

Assert-UniqueIds $state.nodes "State ownership node"
$allowedLayers = @($state.target_layers)
foreach ($node in $state.nodes) {
    if ($allowedLayers -notcontains $node.target_layer) {
        throw "State node $($node.id) has unknown target layer $($node.target_layer)"
    }
    if ([string]::IsNullOrWhiteSpace($node.authority) -or [string]::IsNullOrWhiteSpace($node.mutability) -or
        [string]::IsNullOrWhiteSpace($node.time_semantics) -or [string]::IsNullOrWhiteSpace($node.risk) -or
        [string]::IsNullOrWhiteSpace($node.owner_phase)) {
        throw "State node $($node.id) lacks ownership semantics"
    }
    foreach ($assertion in $node.source_assertions) {
        Assert-SourceAssertion $assertion "State node $($node.id)"
    }
}

$requiredBenchmarkCategories = @(
    "ordinary_diffuse", "sds_caustic", "participating_media", "polarization",
    "wave_and_fluorescence", "dynamic_coupling", "inverse_problem"
)
Assert-UniqueIds $benchmarks.families "Benchmark family"
$actualBenchmarkCategories = @($benchmarks.families | ForEach-Object { [string]$_.category } | Sort-Object)
if (Compare-Object ($requiredBenchmarkCategories | Sort-Object) $actualBenchmarkCategories) {
    throw "High-order benchmark manifest does not cover the seven required categories"
}
foreach ($family in $benchmarks.families) {
    if (@($family.metrics).Count -eq 0 -or [string]::IsNullOrWhiteSpace($family.reference_strategy) -or
        [string]::IsNullOrWhiteSpace($family.provenance)) {
        throw "Benchmark family $($family.id) lacks reference, metrics, or provenance"
    }
    foreach ($assertion in $family.source_assertions) {
        Assert-SourceAssertion $assertion "Benchmark family $($family.id)"
    }
}

$renderHeader = Get-Content -LiteralPath (Join-Path $RepoRoot "libs/ure_core/include/ure/render.hpp") -Raw
$typedSpectralPlanes = if ($renderHeader -match "AovType[\s\S]*?(Spectrum|Spectral|Stokes|Jones)") { 1 } else { 0 }
$metricValues = [ordered]@{
    discovered_boundary_lines = $discovered.Count
    unclassified_boundary_lines = $unclassified.Count
    multiply_classified_boundary_lines = $multiplyClassified.Count
    current_aov_planes = $planes.Count
    required_measurement_gap_classes = @($measurement.gaps).Count
    current_typed_spectral_stokes_planes = $typedSpectralPlanes
    integrator_techniques = @($integrators.techniques).Count
    state_ownership_nodes = @($state.nodes).Count
    benchmark_families = @($benchmarks.families).Count
}

$capsuleRequired = @(
    "schema", "id", "maturity", "question", "hypothesis", "inputs",
    "reproducibility", "baseline", "candidate", "metrics", "artifacts",
    "result", "known_failure_domain"
)
$capsuleFiles = @(Get-ChildItem -LiteralPath (Join-Path $dataRoot "capsules") -File -Filter "*.json" | Sort-Object Name)
if ($capsuleFiles.Count -lt 2) {
    throw "Research Capsule baseline requires positive and negative examples"
}
$outcomes = [System.Collections.Generic.HashSet[string]]::new()
foreach ($file in $capsuleFiles) {
    if (-not (Test-Json -LiteralPath $file.FullName -SchemaFile $schemaPath -ErrorAction Stop)) {
        throw "Capsule $($file.Name) does not satisfy Research Capsule v1"
    }
    $capsule = Read-JsonFile $file.FullName
    $propertyNames = @($capsule.PSObject.Properties.Name)
    foreach ($required in $capsuleRequired) {
        if ($propertyNames -notcontains $required) {
            throw "Capsule $($file.Name) is missing $required"
        }
    }
    if ($capsule.schema -ne "ure.research.capsule/1.0") {
        throw "Capsule $($file.Name) has an unknown schema"
    }
    if (@("Research", "Experimental", "Production") -notcontains $capsule.maturity) {
        throw "Capsule $($file.Name) has an invalid maturity"
    }
    [void]$outcomes.Add([string]$capsule.result.outcome)
    foreach ($assertion in $capsule.inputs.source_assertions) {
        Assert-SourceAssertion $assertion "Capsule $($file.Name)"
    }
    foreach ($metric in $capsule.metrics) {
        if (-not $metricValues.Contains([string]$metric.name)) {
            throw "Capsule $($file.Name) references unknown metric $($metric.name)"
        }
        if ([string]$metricValues[[string]$metric.name] -ne [string]$metric.expected) {
            throw "Capsule $($file.Name) metric $($metric.name) expected $($metric.expected), found $($metricValues[[string]$metric.name])"
        }
    }
}
if (-not $outcomes.Contains("positive") -or -not $outcomes.Contains("negative")) {
    throw "Research Capsule examples must include positive and negative results"
}

$dataFiles = @($ledgerPath, $integratorPath, $measurementPath, $statePath, $schemaPath, $benchmarkPath) + @($capsuleFiles.FullName)
$digests = [ordered]@{}
foreach ($file in $dataFiles) {
    $digests[(Get-RelativeRepositoryPath $file)] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
}
$gitHead = (& git -C $RepoRoot rev-parse HEAD 2>$null | Out-String).Trim()
$report = [ordered]@{
    schema = "ure.phase_ho.baseline/1.0"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    git_head = $gitHead
    metrics = $metricValues
    boundary_counts = $boundaryCounts
    artifacts = $digests
}

if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    $fullReportPath = if ([System.IO.Path]::IsPathRooted($ReportPath)) {
        $ReportPath
    } else {
        Join-Path $RepoRoot $ReportPath
    }
    $parent = Split-Path -Parent $fullReportPath
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $fullReportPath -Encoding utf8
}

Write-Host "HO.0 baseline audit passed: $($discovered.Count) classified boundaries, $($integrators.techniques.Count) techniques, $($measurement.gaps.Count) measurement gaps, $($state.nodes.Count) state nodes, $($benchmarks.families.Count) benchmark families."
