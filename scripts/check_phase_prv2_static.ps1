param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Read-Json([string]$Relative) {
    $path = Join-Path $RepoRoot $Relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required PRV.2 artifact is missing: $Relative"
    }
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -Depth 100
}

$matrix = Read-Json "contracts/prv2_feature_disposition_matrix.json"
$diagnostics = Read-Json "contracts/diagnostics/product_diagnostic_catalog_v0.json"
$registry = Read-Json "contracts/registry/public_contract_registry.json"
$freeze = Read-Json "contracts/stability/core_1_0_freeze_review.json"
$cliSource = Get-Content -Raw -LiteralPath (
    Join-Path $RepoRoot "apps/ure_cli/src/main.cpp")
$cliCMake = Get-Content -Raw -LiteralPath (
    Join-Path $RepoRoot "apps/ure_cli/CMakeLists.txt")
$nativeToolSource = Get-Content -Raw -LiteralPath (
    Join-Path $RepoRoot "apps/ure_native_tool/src/main.cpp")
$nativeToolCMake = Get-Content -Raw -LiteralPath (
    Join-Path $RepoRoot "apps/ure_native_tool/CMakeLists.txt")
$publicHeader = Get-Content -Raw -LiteralPath (
    Join-Path $RepoRoot "contracts/generated/include/ultrarender/ure_loader.h")
$registryHeader = Get-Content -Raw -LiteralPath (
    Join-Path $RepoRoot "contracts/generated/include/ultrarender/ure_registry.h")

if ($matrix.schema -ne "ure.preview.prv2-feature-disposition/1.0" -or
    $matrix.phase -ne "PRV.2" -or
    $matrix.ignored_semantics_allowed -ne $false) {
    throw "PRV.2 disposition matrix identity or ignored policy is invalid"
}
$expectedPhases = 3..12 | ForEach-Object { "Q.$_" }
$actualPhases = @($matrix.entries.source_phase | Sort-Object -Unique)
if ($matrix.entries.Count -ne 10 -or
    (Compare-Object $expectedPhases $actualPhases).Count -ne 0) {
    throw "PRV.2 disposition matrix does not cover Q.3-Q.12 exactly once"
}
$allowed = @($matrix.allowed_dispositions)
if ((Compare-Object @("Executed", "PreservedForTooling", "Rejected") (
        $allowed | Sort-Object)).Count -ne 0) {
    throw "PRV.2 disposition vocabulary drifted"
}
foreach ($entry in $matrix.entries) {
    if ([string]::IsNullOrWhiteSpace($entry.capability) -or
        [string]::IsNullOrWhiteSpace($entry.policy) -or
        @($entry.dispositions).Count -eq 0 -or
        @($entry.evidence).Count -eq 0) {
        throw "PRV.2 disposition entry is incomplete: $($entry.source_phase)"
    }
    foreach ($disposition in $entry.dispositions) {
        if ($disposition -notin $allowed -or $disposition -eq "Ignored") {
            throw "PRV.2 disposition is forbidden: $disposition"
        }
    }
    foreach ($evidence in $entry.evidence) {
        if ($evidence -match '(^|[/\\])gui([/\\]|$)' -or
            -not (Test-Path -LiteralPath (Join-Path $RepoRoot $evidence) -PathType Leaf)) {
            throw "PRV.2 disposition evidence is missing or forbidden: $evidence"
        }
    }
}

$prv2Details = @($diagnostics.details | Where-Object {
    $_.value -ge 600 -and $_.value -le 628
})
if ($prv2Details.Count -ne 29 -or
    @($prv2Details.value | Sort-Object -Unique).Count -ne 29 -or
    (Compare-Object (600..628) @($prv2Details.value | Sort-Object)).Count -ne 0) {
    throw "PRV.2 diagnostic detail range is incomplete or duplicated"
}
$knownResults = @($diagnostics.results.name)
foreach ($detail in $prv2Details) {
    if ([string]::IsNullOrWhiteSpace($detail.owner) -or
        [string]::IsNullOrWhiteSpace($detail.name) -or
        $detail.result -notin $knownResults) {
        throw "PRV.2 diagnostic catalog entry is incomplete: $($detail.value)"
    }
}
foreach ($required in @(
    @{ Value = 601; Result = "CapabilityUnavailable" },
    @{ Value = 608; Result = "MalformedData" },
    @{ Value = 612; Result = "MalformedData" },
    @{ Value = 614; Result = "BudgetExhausted" },
    @{ Value = 618; Result = "MalformedData" },
    @{ Value = 628; Result = "MalformedData" })) {
    if (@($prv2Details | Where-Object {
        $_.value -eq $required.Value -and $_.result -eq $required.Result
    }).Count -ne 1) {
        throw "PRV.2 diagnostic result mapping drifted: $($required.Value)"
    }
}

$sceneToolEntries = @($registry.entries | Where-Object {
    $_.canonical_name -match 'scene_tool'
})
if ($sceneToolEntries.Count -ne 13 -or
    @($sceneToolEntries | Where-Object {
        $_.stability -ne "UnstableExtension" -or
        $_.namespace -ne "unstable_experimental"
    }).Count -ne 0) {
    throw "Scene-tool registry entries escaped the exact-build extension boundary"
}
$coreIds = @($registry.entries | Where-Object stability -eq "Core" |
    ForEach-Object { [uint32]$_.registry_id } | Sort-Object)
$frozenCoreIds = @($freeze.groups.registry_ids | ForEach-Object {
    [uint32]$_
} | Sort-Object)
if ((Compare-Object $coreIds $frozenCoreIds).Count -ne 0) {
    throw "PRV.2 changed the frozen Core identity set"
}
foreach ($operation in @(
    "VALIDATE", "INSPECT", "BUILD", "MIGRATE", "PACK", "UNPACK", "REALIZE")) {
    if ($registryHeader -notmatch "URE_SCENE_TOOL_$operation" -or
        $publicHeader -notmatch 'ure_scene_tool_interface_t') {
        throw "Generated scene-tool operation is absent: $operation"
    }
}

if ($cliCMake -match 'ure_core|ure_sceneio' -or
    $cliSource -notmatch 'client\.scene_tool\(request\)' -or
    $cliSource -notmatch 'return scene_tool\(cli\)' -or
    $nativeToolCMake -match 'ure_client' -or
    $nativeToolSource -match 'SceneToolOperation|client\.scene_tool') {
    throw "CLI or native adapter tool retained a second scene-tool policy path"
}

Write-Output "PRV.2 static audit passed: Q.3-Q.12 disposition, 29 diagnostics, exact-build scene-tool extension, and frozen Core"
