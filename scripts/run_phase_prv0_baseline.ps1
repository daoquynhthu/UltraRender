param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "docs/reports/ure_preview_baseline_v1.json",
    [ValidateSet("NotRun", "Passed", "Failed")][string]$FullGateState = "NotRun",
    [switch]$RequireLiveImages
)

$ErrorActionPreference = "Stop"

function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $RepoRoot $Path
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-TextSha256([string]$Text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    return [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Read-Json([string]$Path) {
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json -Depth 100
}

function Get-TreeDigest([string]$ExcludedReport) {
    $relativeReport = [System.IO.Path]::GetRelativePath($RepoRoot, $ExcludedReport).Replace('\', '/')
    $files = @(& git -C $RepoRoot ls-files --cached --others --exclude-standard | Sort-Object)
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed while building the Preview source identity"
    }
    $records = [System.Collections.Generic.List[string]]::new()
    foreach ($relative in $files) {
        $normalized = $relative.Replace('\', '/')
        if ($normalized -eq $relativeReport) {
            continue
        }
        $full = Join-Path $RepoRoot $relative
        if (Test-Path -LiteralPath $full -PathType Leaf) {
            $records.Add("$normalized $((Get-Sha256 $full))")
        }
    }
    return Get-TextSha256 (($records -join "`n") + "`n")
}

function Get-BackendInventory([string]$CliPath) {
    if (-not (Test-Path -LiteralPath $CliPath -PathType Leaf)) {
        throw "Preview baseline requires the built ure_cli inventory client: $CliPath"
    }
    $lines = @(& $CliPath list-devices)
    if ($LASTEXITCODE -ne 0) {
        throw "ure_cli list-devices failed"
    }
    $items = [System.Collections.Generic.List[object]]::new()
    $current = $null
    foreach ($line in $lines) {
        if ($line -match '^\s+\[(?<ordinal>\d+)\]\s+(?<kind>\S+)\s+(?<name>.+)$') {
            if ($null -ne $current) {
                $items.Add([pscustomobject]$current)
            }
            $current = [ordered]@{
                kind = $Matches.kind
                name = $Matches.name.Trim()
                adapter_id = ""
                driver = ""
                compiler = ""
            }
        } elseif ($null -ne $current -and $line -match '^\s+id:\s+(?<value>.+)$') {
            $current.adapter_id = $Matches.value.Trim()
        } elseif ($null -ne $current -and $line -match '^\s+driver:\s+(?<value>.+)$') {
            $current.driver = $Matches.value.Trim()
        } elseif ($null -ne $current -and $line -match '^\s+compiler:\s+(?<value>.+)$') {
            $current.compiler = $Matches.value.Trim()
        }
    }
    if ($null -ne $current) {
        $items.Add([pscustomobject]$current)
    }
    if ($items.Count -lt 2) {
        throw "Backend inventory did not expose the maintained CUDA/Vulkan baseline"
    }
    foreach ($item in $items) {
        if ([string]::IsNullOrWhiteSpace($item.adapter_id) -or
            [string]::IsNullOrWhiteSpace($item.driver) -or
            [string]::IsNullOrWhiteSpace($item.compiler)) {
            throw "Backend inventory contains an incomplete adapter identity"
        }
    }
    return @($items | Sort-Object kind, adapter_id)
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$closurePath = Join-Path $RepoRoot "contracts/product_closure_ledger.json"
$semanticPath = Join-Path $RepoRoot "contracts/product_semantic_audit.json"
$scenarioPath = Join-Path $RepoRoot "contracts/product_e2e_scenarios.json"
$schemaPath = Join-Path $RepoRoot "contracts/reports/ure_preview_baseline_v1.schema.json"
$pbReportPath = Join-Path $RepoRoot "docs/reports/phase_pb_validation_v2.json"
$closure = Read-Json $closurePath
$semantic = Read-Json $semanticPath
$scenarios = Read-Json $scenarioPath
$pbReport = Read-Json $pbReportPath

& (Join-Path $RepoRoot "scripts/check_phase_prv0_static.ps1") -RepoRoot $RepoRoot | Out-Null

$testLines = @(& ctest --test-dir $buildPath -C Release -N)
if ($LASTEXITCODE -ne 0) {
    throw "ctest inventory failed"
}
$testNames = @($testLines | ForEach-Object {
    if ($_ -match '^\s*Test\s+#\d+:\s+(?<name>\S+)') { $Matches.name }
} | Sort-Object -Unique)
if ($testNames.Count -lt 100) {
    throw "Preview baseline expected at least 100 registered tests, found $($testNames.Count)"
}

$images = [System.Collections.Generic.List[object]]::new()
foreach ($image in $pbReport.image_e2e) {
    $livePath = Join-Path $buildPath ("pb8_external_client_build/rendered_images/" + $image.file)
    if ($RequireLiveImages -and -not (Test-Path -LiteralPath $livePath -PathType Leaf)) {
        throw "Live Core/Worker image is missing: $livePath"
    }
    if (Test-Path -LiteralPath $livePath -PathType Leaf) {
        $live = Get-Item -LiteralPath $livePath
        if ($live.Length -ne [int64]$image.bytes -or (Get-Sha256 $livePath) -ne $image.sha256) {
            throw "Live Core/Worker image drifted from the declared PB evidence: $($image.file)"
        }
    }
    $images.Add([ordered]@{
        calling_mode = [string]$image.calling_mode
        file = [string]$image.file
        bytes = [int64]$image.bytes
        sha256 = [string]$image.sha256
        evidence_scope = "Core ABI 1.0 / Worker Protocol 1.0 bounded render; not Preview product closure"
    })
}

$cliImagePath = Join-Path $buildPath "prv0_cli_evidence/output/prv0_cli.hdr"
if (-not (Test-Path -LiteralPath $cliImagePath -PathType Leaf)) {
    throw "Current CLI image evidence is missing: $cliImagePath"
}
$cliImage = Get-Item -LiteralPath $cliImagePath

$cachePath = Join-Path $buildPath "CMakeCache.txt"
$cache = Get-Content -Raw -LiteralPath $cachePath
$hydraConfigured = [regex]::IsMatch($cache, '(?m)^UR_ENABLE_HYDRA:BOOL=ON$')
$cliPath = Join-Path $buildPath "artifacts/Release/bin/ure_cli.exe"
$backends = Get-BackendInventory $cliPath

$levels = [ordered]@{}
foreach ($level in $closure.allowed_closure_levels) {
    $levels[[string]$level] = @($closure.entries | Where-Object closure_level -eq $level).Count
}
$commit = (& git -C $RepoRoot rev-parse HEAD).Trim().ToLowerInvariant()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') {
    throw "Unable to resolve the source commit"
}
$statusLines = @(& git -C $RepoRoot status --porcelain --untracked-files=all)

$report = [ordered]@{
    schema = "ure.preview.baseline.v1"
    preview_release_declared = $false
    source = [ordered]@{
        commit = $commit
        tree_sha256 = Get-TreeDigest $reportFullPath
        dirty = $statusLines.Count -ne 0
    }
    contracts = [ordered]@{
        closure_ledger_sha256 = Get-Sha256 $closurePath
        semantic_audit_sha256 = Get-Sha256 $semanticPath
        scenario_manifest_sha256 = Get-Sha256 $scenarioPath
        schema_sha256 = Get-Sha256 $schemaPath
    }
    closure = [ordered]@{
        entry_count = @($closure.entries).Count
        levels = $levels
        bypasses = @($closure.entries | Where-Object { $_.bypass_status -notin @("None", "NotApplicable") }).Count
        must_converge = @($closure.entries | Where-Object preview_disposition -eq "MustConverge").Count
    }
    semantic_audit = [ordered]@{
        entry_count = @($semantic.entries).Count
        accepted_but_ignored = @($semantic.entries | Where-Object current_behavior -eq "AcceptedButIgnored").Count
        semantic_debt = @($semantic.entries | Where-Object current_behavior -eq "ExecutedWithSemanticDebt").Count
        guarded_source_count = @($semantic.guarded_sources).Count
    }
    scenarios = [ordered]@{
        count = @($scenarios.scenarios).Count
        product_e2e_now = @($scenarios.scenarios | Where-Object current_closure -eq "ProductE2E").Count
        required_dimensions = @($scenarios.required_dimensions).Count
    }
    ctest = [ordered]@{
        registered = $testNames.Count
        inventory_sha256 = Get-TextSha256 (($testNames -join "`n") + "`n")
        full_gate_state = $FullGateState
    }
    image_evidence = @($images)
    cli_image_evidence = [ordered]@{
        calling_mode = "legacy_cli_direct_renderer"
        file = "prv0_cli.hdr"
        bytes = [int64]$cliImage.Length
        sha256 = Get-Sha256 $cliImagePath
        closure = "RendererIntegrated"
        limitation = "Product service bypass; evidence of current behavior only"
    }
    backends = @($backends)
    optional_hydra = [ordered]@{
        configured = $hydraConfigured
        closure = "RendererIntegratedProductBypass"
    }
    semantic_digest = ""
}
$report.semantic_digest = Get-TextSha256 ($report | ConvertTo-Json -Depth 100 -Compress)

$parent = Split-Path -Parent $reportFullPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
[System.IO.File]::WriteAllText($reportFullPath, $json + "`n", [System.Text.UTF8Encoding]::new($false))
& (Join-Path $RepoRoot "scripts/check_phase_prv0_static.ps1") -RepoRoot $RepoRoot -ReportPath $reportFullPath | Out-Null
Write-Output "PRV.0 baseline written: $ReportPath ($($testNames.Count) tests, $($backends.Count) adapters, $($images.Count + 1) current images)"
