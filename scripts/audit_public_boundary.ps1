param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$LedgerPath = "",
    [string]$RegistryPath = "",
    [string]$CompatibilityPath = "",
    [string]$LegacySurfacePath = "",
    [string]$DumpbinPath = "",
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-InputPath {
    param([string]$Value, [string]$DefaultRelative)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return Join-Path $RepoRoot $DefaultRelative
    }
    if ([System.IO.Path]::IsPathRooted($Value)) {
        return $Value
    }
    return Join-Path $RepoRoot $Value
}

function Read-JsonFile {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required JSON file is missing: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -Depth 100
}

function Assert-UniqueNonempty {
    param($Values, [string]$Label)
    $items = @($Values | ForEach-Object { [string]$_ })
    if ($items.Count -eq 0 -or $items -contains "" -or
        $items.Count -ne @($items | Sort-Object -Unique).Count) {
        throw "$Label values must be nonempty and unique"
    }
}

function Assert-NoFloatingPoint {
    param($Value, [string]$Path)
    if ($null -eq $Value) {
        return
    }
    if ($Value -is [double] -or $Value -is [single] -or $Value -is [decimal]) {
        throw "Registry contains a floating-point value at $Path"
    }
    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        foreach ($property in $Value.PSObject.Properties) {
            Assert-NoFloatingPoint $property.Value "$Path.$($property.Name)"
        }
        return
    }
    if ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string]) {
        $index = 0
        foreach ($item in $Value) {
            Assert-NoFloatingPoint $item "$Path[$index]"
            $index++
        }
    }
}

function Assert-Anchor {
    param($Anchor, [string[]]$ForbiddenRoots, [string]$Label)
    $relative = ([string]$Anchor.path).Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($relative)) {
        throw "$Label contains an empty anchor path"
    }
    foreach ($root in $ForbiddenRoots) {
        $prefix = ([string]$root).Trim('/').Replace('\', '/') + "/"
        if ($relative.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "$Label attempts to inspect forbidden root $root"
        }
    }
    $full = Join-Path $RepoRoot $relative
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "$Label references missing anchor $relative"
    }
    $pattern = [string]$Anchor.pattern
    if ([string]::IsNullOrWhiteSpace($pattern) -or
        -not [regex]::IsMatch((Get-Content -LiteralPath $full -Raw), $pattern)) {
        throw "$Label pattern is absent in ${relative}: $pattern"
    }
}

function Find-Dumpbin {
    if (-not [string]::IsNullOrWhiteSpace($DumpbinPath)) {
        if (-not (Test-Path -LiteralPath $DumpbinPath -PathType Leaf)) {
            throw "dumpbin was not found at $DumpbinPath"
        }
        return (Resolve-Path -LiteralPath $DumpbinPath).Path
    }
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $roots = @(
        "D:/Microsoft Visual Studio/VC/Tools/MSVC",
        "C:/Program Files/Microsoft Visual Studio"
    )
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }
        $candidate = Get-ChildItem -LiteralPath $root -Filter dumpbin.exe -File -Recurse |
            Where-Object { $_.FullName -match 'Hostx64[\\/]x64' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }
    throw "MSVC dumpbin.exe is required for the PB.0 export audit"
}

$ledgerPathResolved = Resolve-InputPath $LedgerPath "contracts/public_interaction_surface_ledger.json"
$registryPathResolved = Resolve-InputPath $RegistryPath "contracts/registry/public_contract_registry.json"
$compatibilityPathResolved = Resolve-InputPath $CompatibilityPath "contracts/registry/registry_compatibility.json"
$legacyPathResolved = Resolve-InputPath $LegacySurfacePath "tests/fixtures/contracts/registry/legacy_surface.json"

$ledger = Read-JsonFile $ledgerPathResolved
$registry = Read-JsonFile $registryPathResolved
$compatibility = Read-JsonFile $compatibilityPathResolved
$legacy = Read-JsonFile $legacyPathResolved

if ($ledger.schema -ne "ure.pb.public-interaction-surface-ledger/1.0") {
    throw "Unexpected public interaction surface ledger schema"
}
if ($registry.schema -ne "ure.public.contract-registry-source/1.0" -or
    $registry.publication_state -ne "Stable" -or
    $registry.version -ne "1.0.0") {
    throw "Unexpected public contract registry identity"
}
if ($compatibility.schema -ne "ure.public.registry-compatibility/2.0" -or
    $compatibility.release_version -ne $registry.version) {
    throw "Registry compatibility metadata does not match the registry"
}
if ($legacy.schema -ne "ure.pb.legacy-surface/1.0" -or
    $legacy.classification -ne "LegacyExperimental") {
    throw "Unexpected legacy surface identity"
}

Assert-NoFloatingPoint $registry "registry"
Assert-UniqueNonempty $registry.namespaces.id "Registry namespace ID"
Assert-UniqueNonempty $registry.identity_kinds "Registry identity kind"
$ranges = @($registry.namespaces | Sort-Object { [uint64]$_.first })
$previousLast = [uint64]0
foreach ($range in $ranges) {
    $first = [uint64]$range.first
    $last = [uint64]$range.last
    if ($first -eq 0 -or $first -gt $last -or $last -ge [uint64]4294967295 -or
        $first -le $previousLast) {
        throw "Registry namespace ranges overlap or use a reserved endpoint: $($range.id)"
    }
    $previousLast = $last
}
Assert-UniqueNonempty $registry.entries.registry_id "Registry entry ID"
Assert-UniqueNonempty $registry.entries.canonical_name "Registry canonical name"
Assert-UniqueNonempty $registry.entries.c_name "Registry C name"
$registryEntryById = @{}
foreach ($entry in @($registry.entries)) {
    $range = @($registry.namespaces | Where-Object { $_.id -eq $entry.namespace })
    if ($range.Count -ne 1 -or [uint64]$entry.registry_id -lt [uint64]$range[0].first -or
        [uint64]$entry.registry_id -gt [uint64]$range[0].last -or $entry.stability -ne $range[0].stability) {
        throw "Registry entry $($entry.canonical_name) is outside its namespace"
    }
    $registryEntryById[[string]$entry.registry_id] = $entry
}
foreach ($entry in @($registry.entries)) {
    if (@($entry.dependencies).Count -gt 0) {
        Assert-UniqueNonempty @($entry.dependencies | ForEach-Object { [string]$_ }) "Registry dependency for $($entry.canonical_name)"
    }
    foreach ($dependency in @($entry.dependencies)) {
        if (-not $registryEntryById.ContainsKey([string]$dependency) -or [uint64]$dependency -eq [uint64]$entry.registry_id) {
            throw "Registry entry $($entry.canonical_name) has an invalid dependency"
        }
        if ([bool]$entry.default_enabled -and -not [bool]$registryEntryById[[string]$dependency].default_enabled) {
            throw "Registry default dependency closure failed for $($entry.canonical_name)"
        }
    }
}
if (@($registry.entries | Where-Object { $_.kind -eq "Capability" -and $_.maturity -eq "Research" -and [bool]$_.default_enabled }).Count -ne 0) {
    throw "Research capabilities must not be enabled by default"
}
$tombstoneIds = @($registry.tombstones.registry_id | ForEach-Object { [string]$_ })
if ($tombstoneIds.Count -ne @($tombstoneIds | Sort-Object -Unique).Count -or
    @($tombstoneIds | Where-Object { $registryEntryById.ContainsKey($_) }).Count -ne 0) {
    throw "Registry tombstones are duplicate or reused"
}
if ($compatibility.pre_release_baseline.version -ne "0.1.0" -or
    $compatibility.pre_release_baseline.registry_digest -ne "0e56eea2d03b2528ceefe2f686de3b63510d956738ee19cf107835abb297f554" -or
    @($compatibility.changes).Count -ne 149 -or
    @($compatibility.changes | Where-Object { $_.phase -eq "PB.3" }).Count -ne 53 -or
    @($compatibility.changes | Where-Object { $_.phase -eq "PB.4" }).Count -ne 20 -or
    @($compatibility.changes | Where-Object { $_.phase -eq "PB.5" }).Count -ne 34 -or
    @($compatibility.changes | Where-Object { $_.phase -eq "PB.6" }).Count -ne 21 -or
    @($compatibility.changes | Where-Object { $_.phase -in @("PB.3", "PB.4", "PB.5", "PB.6") -and $_.change_class -ne "AdditiveCandidate" }).Count -ne 0 -or
    @($compatibility.changes | Where-Object { $_.phase -eq "PB.8" -and $_.change_class -eq "BreakingCandidate" }).Count -ne 21 -or
    @($compatibility.changes | Where-Object { $_.phase -notin @("PB.3", "PB.4", "PB.5", "PB.6", "PB.8") }).Count -ne 0) {
    throw "PB compatibility history is incomplete"
}
$expectedTombstones = @([uint64]42, [uint64]43, [uint64]303, [uint64]406, [uint64]601, [uint64]602, [uint64]808, [uint64]980, [uint64]981, [uint64]982, [uint64]983)
$registryTombstones = @($registry.tombstones | ForEach-Object { [uint64]$_.registry_id })
$compatibilityTombstones = @($compatibility.tombstones | ForEach-Object { [uint64]$_ })
if ($registryTombstones.Count -ne $expectedTombstones.Count -or
    $compatibilityTombstones.Count -ne $expectedTombstones.Count -or
    (Compare-Object $expectedTombstones $registryTombstones) -or
    (Compare-Object $expectedTombstones $compatibilityTombstones)) {
    throw "PB.8 registry tombstones do not match the frozen pre-release demotions"
}

Assert-UniqueNonempty $registry.public_header_policy.extensions "Public header extension"
Assert-UniqueNonempty $registry.public_header_policy.forbidden_patterns.id "Forbidden public header pattern ID"
$publicHeaderFileCount = 0
foreach ($rootDescriptor in @($registry.public_header_policy.roots)) {
    $rootValue = [string]$rootDescriptor.path
    $fullRoot = if ([System.IO.Path]::IsPathRooted($rootValue)) {
        $rootValue
    } else {
        Join-Path $RepoRoot $rootValue
    }
    if (-not (Test-Path -LiteralPath $fullRoot -PathType Container)) {
        throw "Required public header root is missing: $rootValue"
    }
    $publicHeaders = @(
        Get-ChildItem -LiteralPath $fullRoot -File -Recurse |
            Where-Object { $registry.public_header_policy.extensions -contains $_.Extension }
    )
    $publicHeaderFileCount += $publicHeaders.Count
    foreach ($header in $publicHeaders) {
        $text = Get-Content -LiteralPath $header.FullName -Raw
        foreach ($forbidden in @($registry.public_header_policy.forbidden_patterns)) {
            if ([regex]::IsMatch($text, [string]$forbidden.pattern)) {
                throw "Public header contains forbidden token $($forbidden.id): $($header.FullName)"
            }
        }
    }
}
$contractCMake = Join-Path $RepoRoot "libs/ure_contract/CMakeLists.txt"
if ([bool]$registry.public_header_policy.forbid_automatic_windows_exports -and
    (Test-Path -LiteralPath $contractCMake -PathType Leaf) -and
    [regex]::IsMatch((Get-Content -LiteralPath $contractCMake -Raw), 'WINDOWS_EXPORT_ALL_SYMBOLS\s+ON')) {
    throw "Public runtime target enables automatic Windows exports"
}

$entries = @($ledger.entries)
Assert-UniqueNonempty $entries.id "Interaction surface ID"
Assert-UniqueNonempty $ledger.required_surfaces "Required interaction surface"
$actualIds = @($entries.id | ForEach-Object { [string]$_ } | Sort-Object)
$requiredIds = @($ledger.required_surfaces | ForEach-Object { [string]$_ } | Sort-Object)
if (Compare-Object $actualIds $requiredIds) {
    throw "Interaction ledger does not exactly cover required_surfaces"
}

$allowedDispositions = @($ledger.allowed_dispositions)
$allowedStability = @($ledger.allowed_contract_stability)
$allowedMaturity = @($ledger.allowed_evidence_maturity)
$allowedRuntimeStates = @($ledger.allowed_runtime_states)
$allowedImplementation = @($ledger.allowed_implementation_status)
$forbiddenRoots = @($ledger.discovery.inspection_forbidden_roots | ForEach-Object { [string]$_ })
$authorityOwners = @{}
$entryById = @{}
foreach ($entry in $entries) {
    $entryById[[string]$entry.id] = $entry
}

$requiredTextFields = @(
    "id", "surface", "disposition", "target_disposition", "contract_stability",
    "evidence_maturity", "implementation_status", "input_role", "output_role",
    "translation_path", "bypass_status", "bypass_detail", "migration_phase",
    "terminal_gate", "loss_policy"
)
foreach ($entry in $entries) {
    foreach ($field in $requiredTextFields) {
        if ([string]::IsNullOrWhiteSpace([string]$entry.$field)) {
            throw "Interaction surface $($entry.id) is missing $field"
        }
    }
    if ($allowedDispositions -notcontains $entry.disposition -or
        $allowedDispositions -notcontains $entry.target_disposition) {
        throw "Interaction surface $($entry.id) has an invalid disposition"
    }
    if ($allowedStability -notcontains $entry.contract_stability -or
        $allowedMaturity -notcontains $entry.evidence_maturity -or
        $allowedImplementation -notcontains $entry.implementation_status) {
        throw "Interaction surface $($entry.id) has an invalid governance axis"
    }
    Assert-UniqueNonempty $entry.semantic_domains "Interaction surface $($entry.id) semantic domain"
    foreach ($state in @($entry.runtime_states)) {
        if ($allowedRuntimeStates -notcontains $state) {
            throw "Interaction surface $($entry.id) has invalid runtime state $state"
        }
    }
    foreach ($claim in @($entry.authority_claims)) {
        $domain = [string]$claim
        if ([string]::IsNullOrWhiteSpace($domain)) {
            throw "Interaction surface $($entry.id) has an empty authority claim"
        }
        if ($authorityOwners.ContainsKey($domain)) {
            throw "Duplicate authority for ${domain}: $($authorityOwners[$domain]) and $($entry.id)"
        }
        $authorityOwners[$domain] = [string]$entry.id
    }
    foreach ($reference in @($entry.authority_refs)) {
        if (-not $entryById.ContainsKey([string]$reference)) {
            throw "Interaction surface $($entry.id) references unknown authority $reference"
        }
    }
    if ($entry.disposition -eq "CanonicalAuthority" -and @($entry.authority_claims).Count -eq 0) {
        throw "Canonical authority $($entry.id) has no authority claim"
    }
    if ($entry.disposition -eq "PublicTransport" -and $entry.contract_stability -ne "Core") {
        throw "PB public transport $($entry.id) must use frozen Core stability"
    }
    if ($entry.disposition -eq "Adapter" -and
        (@($entry.authority_refs).Count -eq 0 -or [string]::IsNullOrWhiteSpace([string]$entry.loss_policy))) {
        throw "Adapter $($entry.id) lacks canonical authority or loss policy"
    }
    if ($entry.disposition -eq "LegacyMigration" -and
        ($entry.migration_phase -eq "None" -or $entry.terminal_gate -eq "None")) {
        throw "Legacy migration $($entry.id) lacks a migration phase or terminal gate"
    }
    if ($entry.disposition -eq "FrozenExcluded" -and
        ($entry.bypass_status -ne "Excluded" -or @($entry.runtime_states).Count -ne 0)) {
        throw "Frozen/excluded surface $($entry.id) is not fully excluded"
    }
    if ($entry.bypass_status -eq "MustConverge" -and $entry.terminal_gate -eq "None") {
        throw "Bypass $($entry.id) lacks a terminal convergence gate"
    }
    $inspectionForbidden = $entry.PSObject.Properties.Name -contains "inspection_forbidden" -and
        [bool]$entry.inspection_forbidden
    if ($inspectionForbidden -and @($entry.anchors).Count -ne 0) {
        throw "Inspection-forbidden surface $($entry.id) must not contain anchors"
    }
    if (-not $inspectionForbidden -and @($entry.anchors).Count -eq 0) {
        throw "Interaction surface $($entry.id) lacks source anchors"
    }
    foreach ($anchor in @($entry.anchors)) {
        Assert-Anchor $anchor $forbiddenRoots "Interaction surface $($entry.id)"
    }
}

foreach ($entry in $entries) {
    foreach ($reference in @($entry.authority_refs)) {
        $referenced = $entryById[[string]$reference]
        if ($referenced.disposition -notin @("CanonicalAuthority", "PublicTransport", "InternalContract")) {
            throw "Interaction surface $($entry.id) references non-authoritative surface $reference"
        }
    }
}

$installedModules = @(
    Get-ChildItem -LiteralPath (Join-Path $RepoRoot "libs") -Directory |
        Where-Object {
            $cmake = Join-Path $_.FullName "CMakeLists.txt"
            (Test-Path -LiteralPath $cmake -PathType Leaf) -and
            [regex]::IsMatch((Get-Content -LiteralPath $cmake -Raw), 'install\s*\(\s*DIRECTORY\s+include/')
        } |
        ForEach-Object Name |
        Sort-Object
)
$declaredInstalledModules = @($ledger.discovery.installed_header_modules | ForEach-Object { [string]$_ } | Sort-Object)
if (Compare-Object $installedModules $declaredInstalledModules) {
    throw "Installed public-header module discovery changed"
}
foreach ($relative in @($ledger.discovery.root_client_surfaces)) {
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $relative) -PathType Leaf)) {
        throw "Declared root client surface is missing: $relative"
    }
}

$headerPath = Join-Path $RepoRoot $legacy.header.path
$headerText = Get-Content -LiteralPath $headerPath -Raw
$headerLines = @(Get-Content -LiteralPath $headerPath).Count
$headerStructs = [regex]::Matches($headerText, '(?m)^typedef struct ure_[A-Za-z0-9_]+').Count
$headerEnums = [regex]::Matches($headerText, '(?m)^typedef enum ure_[A-Za-z0-9_]+').Count
if ((Get-Item -LiteralPath $headerPath).Length -ne [int64]$legacy.header.bytes -or
    $headerLines -ne [int]$legacy.header.lines -or
    $headerStructs -ne [int]$legacy.header.struct_count -or
    $headerEnums -ne [int]$legacy.header.enum_count) {
    throw "Legacy C header size/layout inventory changed"
}
Assert-UniqueNonempty $legacy.intended_c_exports "Legacy intended C export"
if (@($legacy.intended_c_exports).Count -ne [int]$legacy.header.function_count) {
    throw "Legacy C function inventory count is inconsistent"
}
foreach ($name in @($legacy.intended_c_exports)) {
    if (-not [regex]::IsMatch($headerText, "(?m)\b$([regex]::Escape([string]$name))\s*\(")) {
        throw "Legacy C header no longer declares $name"
    }
}
foreach ($name in @($legacy.versioned_families)) {
    if ($headerText -notmatch [regex]::Escape([string]$name)) {
        throw "Legacy version family disappeared: $name"
    }
}
foreach ($name in @($legacy.transient_pointer_functions)) {
    if (-not [regex]::IsMatch($headerText, "const float\*\s+$([regex]::Escape([string]$name))\s*\(")) {
        throw "Legacy transient pointer inventory changed: $name"
    }
}
foreach ($name in @($legacy.index_addressed_mutations)) {
    $match = [regex]::Match($headerText, "(?s)$([regex]::Escape([string]$name))\s*\((.*?)\);")
    if (-not $match.Success -or $match.Groups[1].Value -notmatch '\bsize_t\b') {
        throw "Legacy index mutation inventory changed: $name"
    }
}
foreach ($assertion in @($legacy.source_assertions)) {
    Assert-Anchor $assertion $forbiddenRoots "Legacy source assertion"
}

$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $RepoRoot $BuildDir
}
$dllPath = Join-Path $buildRoot $legacy.binary.relative_path_from_build_dir
if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
    throw "Legacy DLL is missing; build the Release baseline before PB.0 audit"
}
$dllInfo = Get-Item -LiteralPath $dllPath
$dllHash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
if ($dllInfo.Length -ne [int64]$legacy.binary.bytes -or $dllHash -ne $legacy.binary.sha256) {
    throw "Legacy DLL identity changed; refresh the baseline intentionally"
}
$legacyCoreCMake = Get-Content -LiteralPath (Join-Path $RepoRoot "libs/ure_core/CMakeLists.txt") -Raw
if (-not [bool]$legacy.binary.reproducible_link -or $legacy.binary.reproducibility_flag -ne "/Brepro" -or
    $legacy.binary.baseline_refresh_phase -ne "PB.6" -or
    -not [regex]::IsMatch($legacyCoreCMake, 'target_link_options\s*\(\s*pyure_native\s+PRIVATE\s+/Brepro\s*\)')) {
    throw "Legacy DLL reproducible-link policy is missing"
}
$dumpbin = Find-Dumpbin
$dumpOutput = & $dumpbin /nologo /exports $dllPath 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed for $dllPath"
}
$exportNames = @(
    $dumpOutput | ForEach-Object {
        if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') {
            $Matches[1]
        }
    }
)
$ureExports = @($exportNames | Where-Object { $_ -match '^ure_' } | Sort-Object)
$expectedUreExports = @($legacy.intended_c_exports | ForEach-Object { [string]$_ } | Sort-Object)
$cppExports = @($exportNames | Where-Object { $_ -match '^\?' })
$otherExports = @($exportNames | Where-Object { $_ -notmatch '^ure_' -and $_ -notmatch '^\?' })
if (Compare-Object $ureExports $expectedUreExports) {
    throw "Legacy DLL intended C exports changed"
}
if ($exportNames.Count -ne [int]$legacy.binary.export_count -or
    $ureExports.Count -ne [int]$legacy.binary.intended_c_export_count -or
    $cppExports.Count -ne [int]$legacy.binary.accidental_cpp_export_count -or
    $otherExports.Count -ne [int]$legacy.binary.accidental_other_export_count) {
    throw "Legacy DLL export classification changed"
}

$oldClientManifestPath = Join-Path $RepoRoot "tests/fixtures/contracts/old_clients/legacy_0_0/windows_x64/manifest.json"
$oldClientManifest = Read-JsonFile $oldClientManifestPath
if ($oldClientManifest.schema -ne "ure.pb.old-client-fixture/1.0" -or
    $oldClientManifest.classification -ne "LegacyExperimentalMigrationBaseline" -or
    $oldClientManifest.compatibility_promise -ne "None; migration evidence only") {
    throw "Unexpected legacy client fixture identity"
}
$oldClientRoot = Split-Path -Parent $oldClientManifestPath
$oldClientSource = Join-Path $oldClientRoot $oldClientManifest.source
$oldClientBinary = Join-Path $oldClientRoot $oldClientManifest.binary
if (-not (Test-Path -LiteralPath $oldClientSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $oldClientBinary -PathType Leaf)) {
    throw "Legacy client source or binary is missing"
}
$oldClientSourceHash = (Get-FileHash -LiteralPath $oldClientSource -Algorithm SHA256).Hash
$oldClientBinaryHash = (Get-FileHash -LiteralPath $oldClientBinary -Algorithm SHA256).Hash
if ($oldClientSourceHash -ne $oldClientManifest.source_sha256 -or
    $oldClientBinaryHash -ne $oldClientManifest.binary_sha256 -or
    (Get-Item -LiteralPath $oldClientBinary).Length -ne [int64]$oldClientManifest.binary_bytes -or
    $dllHash -ne $oldClientManifest.runtime_sha256) {
    throw "Legacy client fixture content identity changed"
}
& $oldClientBinary $dllPath
if ($LASTEXITCODE -ne 0) {
    throw "Legacy compiled C client failed against the baseline DLL with exit code $LASTEXITCODE"
}

$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Unable to resolve repository HEAD"
}
$report = [ordered]@{
    schema = "ure.pb.boundary-audit/1.0"
    source_head = $head
    registry_version = [string]$registry.version
    registry_source_sha256 = (Get-FileHash -LiteralPath $registryPathResolved -Algorithm SHA256).Hash
    registry_entry_count = @($registry.entries).Count
    compatibility_sha256 = (Get-FileHash -LiteralPath $compatibilityPathResolved -Algorithm SHA256).Hash
    interaction_ledger_sha256 = (Get-FileHash -LiteralPath $ledgerPathResolved -Algorithm SHA256).Hash
    interaction_surface_count = $entries.Count
    authority_domain_count = $authorityOwners.Count
    unresolved_classification_count = 0
    duplicate_authority_count = 0
    forbidden_inspection_count = 0
    forbidden_public_header_count = 0
    scanned_public_header_count = $publicHeaderFileCount
    installed_header_modules = $installedModules
    legacy = [ordered]@{
        header_function_count = @($legacy.intended_c_exports).Count
        header_struct_count = $headerStructs
        header_enum_count = $headerEnums
        dll_sha256 = $dllHash
        export_count = $exportNames.Count
        intended_c_export_count = $ureExports.Count
        accidental_cpp_export_count = $cppExports.Count
        accidental_other_export_count = $otherExports.Count
        client_fixture_sha256 = $oldClientBinaryHash
        client_fixture_exit_code = 0
    }
}
$json = $report | ConvertTo-Json -Depth 20
if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    $resolvedReport = if ([System.IO.Path]::IsPathRooted($ReportPath)) {
        $ReportPath
    } else {
        Join-Path $RepoRoot $ReportPath
    }
    $parent = Split-Path -Parent $resolvedReport
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $resolvedReport -Value $json -Encoding utf8
}

Write-Host "PB.0 public boundary audit passed: $($entries.Count) surfaces, $($authorityOwners.Count) authority domains, $($exportNames.Count) legacy DLL exports."
