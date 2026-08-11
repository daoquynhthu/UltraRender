param(
    [Parameter(Mandatory = $true)][string]$Codegen,
    [Parameter(Mandatory = $true)][string]$Flatc,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = "Stop"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("ure_pb1_negative_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temp | Out-Null
$registrySource = Join-Path $RepoRoot "contracts/registry/public_contract_registry.json"
$compatibility = Join-Path $RepoRoot "contracts/registry/registry_compatibility.json"
$schemas = Join-Path $RepoRoot "contracts/schemas"

function Write-Json {
    param($Value, [string]$Path)
    Set-Content -LiteralPath $Path -Value ($Value | ConvertTo-Json -Depth 100) -Encoding utf8
}

function Invoke-CodegenFailure {
    param([string]$Registry, [string]$SchemaRoot, [string]$Pattern, [string]$Label, [string]$Compatibility = $compatibility)
    $output = & $Codegen lint --registry $Registry --compatibility $Compatibility --schemas $SchemaRoot 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        throw "$Label unexpectedly passed"
    }
    if ($output -notmatch $Pattern) {
        throw "$Label failed for the wrong reason: $output"
    }
}

try {
    $canonicalDigest = (& $Codegen lint --registry $registrySource --compatibility $compatibility --schemas $schemas).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Canonical registry lint failed"
    }
    $rewrittenRegistry = Join-Path $temp "rewritten_registry.json"
    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    Write-Json $registry $rewrittenRegistry
    $rewrittenDigest = (& $Codegen lint --registry $rewrittenRegistry --compatibility $compatibility --schemas $schemas).Trim()
    if ($LASTEXITCODE -ne 0 -or $rewrittenDigest -ne $canonicalDigest) {
        throw "Registry digest depends on source formatting or path"
    }

    $compatibilityValue = Get-Content -LiteralPath $compatibility -Raw | ConvertFrom-Json -Depth 100
    $compatibilityValue.changes[1].registry_id = $compatibilityValue.changes[0].registry_id
    $badCompatibility = Join-Path $temp "duplicate_compatibility_change.json"
    Write-Json $compatibilityValue $badCompatibility
    Invoke-CodegenFailure $registrySource $schemas "Invalid registry compatibility change" "Duplicate compatibility change" $badCompatibility

    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry.entries[1].registry_id = $registry.entries[0].registry_id
    $path = Join-Path $temp "duplicate_id.json"
    Write-Json $registry $path
    Invoke-CodegenFailure $path $schemas "Invalid or duplicate registry entry" "Duplicate registry ID"

    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry.entries[0].registry_id = 1048576
    $path = Join-Path $temp "wrong_range.json"
    Write-Json $registry $path
    Invoke-CodegenFailure $path $schemas "outside its namespace" "Wrong namespace range"

    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry.tombstones = @([pscustomobject]@{registry_id = 1})
    $path = Join-Path $temp "reused_tombstone.json"
    Write-Json $registry $path
    Invoke-CodegenFailure $path $schemas "tombstone" "Reused tombstone"

    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry.entries[0].dependencies = @(999999)
    $path = Join-Path $temp "missing_dependency.json"
    Write-Json $registry $path
    Invoke-CodegenFailure $path $schemas "Invalid dependency" "Missing dependency"

    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry.entries[0].dependencies = @(2)
    $registry.entries[1].dependencies = @(1)
    $path = Join-Path $temp "dependency_cycle.json"
    Write-Json $registry $path
    Invoke-CodegenFailure $path $schemas "dependency cycle" "Dependency cycle"

    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry.entries[0].since = "2.0.0"
    $path = Join-Path $temp "future_version.json"
    Write-Json $registry $path
    Invoke-CodegenFailure $path $schemas "Invalid or duplicate registry entry" "Future version"

    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    ($registry.entries | Where-Object canonical_name -eq "ure.experimental.capability.telemetry").default_enabled = $true
    $path = Join-Path $temp "default_dependency.json"
    Write-Json $registry $path
    Invoke-CodegenFailure $path $schemas "Enabled-by-default entry is only compiled" "Default state closure"

    $registry = Get-Content -LiteralPath $registrySource -Raw | ConvertFrom-Json -Depth 100
    $registry.entries[0] | Add-Member -NotePropertyName unknown_field -NotePropertyValue 1
    $path = Join-Path $temp "unknown_field.json"
    Write-Json $registry $path
    Invoke-CodegenFailure $path $schemas "missing or unknown fields" "Unknown registry field"

    $duplicateKey = Join-Path $temp "duplicate_key.json"
    $text = Get-Content -LiteralPath $registrySource -Raw
    $text = $text.Replace('"schema": "ure.public.contract-registry-source/1.0",', '"schema": "ure.public.contract-registry-source/1.0", "schema": "duplicate",')
    Set-Content -LiteralPath $duplicateKey -Value $text -Encoding utf8
    Invoke-CodegenFailure $duplicateKey $schemas "Duplicate JSON key" "Duplicate JSON key"

    $badSchemas = Join-Path $temp "bad_schemas"
    Copy-Item -LiteralPath $schemas -Destination $badSchemas -Recurse
    $workerSchema = Join-Path $badSchemas "ure_worker_v1.fbs"
    $text = Get-Content -LiteralPath $workerSchema -Raw
    $text = $text.Replace('protocol_major:ushort (id: 0);', 'protocol_major:ushort;')
    Set-Content -LiteralPath $workerSchema -Value $text -Encoding utf8
    Invoke-CodegenFailure $registrySource $badSchemas "explicit ID" "Missing schema field ID"

    $drift = Join-Path $temp "generated_drift"
    Copy-Item -LiteralPath (Join-Path $RepoRoot "contracts/generated") -Destination $drift -Recurse
    Add-Content -LiteralPath (Join-Path $drift "include/ultrarender/ure_loader.h") -Value "drift"
    $output = & $Codegen compare --registry $registrySource --compatibility $compatibility --schemas $schemas --expected $drift 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or $output -notmatch "Generated content drift") {
        throw "Generated drift fixture did not fail correctly: $output"
    }

    $breakingSchemas = Join-Path $temp "breaking_schemas"
    Copy-Item -LiteralPath $schemas -Destination $breakingSchemas -Recurse
    $workerSchema = Join-Path $breakingSchemas "ure_worker_v1.fbs"
    $text = Get-Content -LiteralPath $workerSchema -Raw
    $text = $text.Replace('protocol_major:ushort (id: 0);', 'protocol_major:uint (id: 0);')
    Set-Content -LiteralPath $workerSchema -Value $text -Encoding utf8
    & $Flatc --conform (Join-Path $schemas "baseline/1.0/ure_worker_v1.fbs") $workerSchema 2>$null
    if ($LASTEXITCODE -eq 0) {
        throw "Breaking schema unexpectedly conformed"
    }
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
