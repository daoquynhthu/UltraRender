param([string]$BuildDir = "build_modular_x64")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$allowedRootMarkdown = @("AGENTS.md", "PLAN.md", "README.md", "STATUS.md")
$actualRootMarkdown = @(Get-ChildItem -LiteralPath $root -File -Filter "*.md" | ForEach-Object Name | Sort-Object)
$expectedRootMarkdown = @($allowedRootMarkdown | Sort-Object)
if (Compare-Object $actualRootMarkdown $expectedRootMarkdown) {
    throw "Root Markdown set must be exactly: $($expectedRootMarkdown -join ', ')"
}

$readme = Get-Content -LiteralPath (Join-Path $root "README.md") -Raw
$status = Get-Content -LiteralPath (Join-Path $root "STATUS.md") -Raw
$agents = Get-Content -LiteralPath (Join-Path $root "AGENTS.md") -Raw
$planCursor = Select-String -LiteralPath (Join-Path $root "PLAN.md") -Pattern '^当前游标:\s*(.+)$' | Select-Object -First 1
if (-not $planCursor) { throw "PLAN authoritative cursor is missing" }
$cursor = $planCursor.Matches[0].Groups[1].Value.Trim()
foreach ($entry in @(@("README.md", $readme), @("STATUS.md", $status), @("AGENTS.md", $agents))) {
    if ($entry[1] -notmatch [regex]::Escape($cursor)) { throw "$($entry[0]) does not name the PLAN cursor $cursor" }
}

foreach ($term in @("世界顶尖", "世界领先", "行业领先", "革命性", "渲染技术的无人区", "Rendering Reality, One Wavelength at a Time")) {
    if ($readme -match [regex]::Escape($term)) { throw "README contains promotional language: $term" }
}

$currentDocuments = @(
    "README.md", "STATUS.md", "docs/README.md", "docs/reference/Backend_API.md",
    "docs/Spectral_Semantics_Guide.md", "docs/Phase_Q_Native_Scene_Format.md",
    "docs/HO_0_Capability_Baseline.md",
    "docs/HO_1_Unified_Semantics.md",
    "docs/HO_2_Executable_Research_Substrate.md",
    "docs/HT_0_Legacy_Technique_Graph.md",
    "docs/HT_1_Support_Measure_Composition.md",
    "docs/HR_0_Measurement_Bundle.md",
    "docs/Phase_R_P6_Mie_Volume_Resources.md", "docs/Phase_W_Wave_Optics_Audit.md",
    "docs/Phase_W_W12_Validation.md"
)
foreach ($relative in $currentDocuments) {
    $text = Get-Content -LiteralPath (Join-Path $root $relative) -Raw
    foreach ($stale in @("当前唯一施工项是 Phase Q", "Q.5-Q.12", "include/gpu/", "build_modular/last")) {
        if ($text -match [regex]::Escape($stale)) { throw "$relative contains stale current-state text: $stale" }
    }
}

$archiveFiles = @(Get-ChildItem -LiteralPath (Join-Path $root "docs/archive") -File -Recurse -Filter "*.md")
foreach ($file in $archiveFiles) {
    if ((Get-Content -LiteralPath $file.FullName -Raw) -notmatch "(?i)(archive status|historical log|documentation archive)") {
        throw "Archived document lacks an archive marker: $($file.FullName)"
    }
}
$executionRecords = @(Get-ChildItem -LiteralPath (Join-Path $root "docs/superpowers") -File -Recurse -Filter "*.md")
foreach ($file in $executionRecords) {
    if ((Get-Content -LiteralPath $file.FullName -Raw) -notmatch "(?i)(Archive status:|Document status: Active)") {
        throw "Design/plan lacks an active or archive marker: $($file.FullName)"
    }
}

& (Join-Path $root "scripts/check_phase_ho0_baseline.ps1") -RepoRoot $root
& (Join-Path $root "scripts/check_phase_ho1_semantics.ps1") -RepoRoot $root
& (Join-Path $root "scripts/check_phase_ho2_research_substrate.ps1") -RepoRoot $root
& (Join-Path $root "scripts/check_phase_ht0_technique_graph.ps1") -RepoRoot $root
& (Join-Path $root "scripts/check_phase_ht1_support_measure_graph.ps1") -RepoRoot $root
& (Join-Path $root "scripts/check_phase_hr0_measurement_bundle.ps1") -RepoRoot $root

$markdownFiles = @(Get-ChildItem -LiteralPath $root -File -Recurse -Filter "*.md" | Where-Object {
    $_.FullName -notmatch '[\\/]third_party[\\/]' -and
    $_.FullName -notmatch '[\\/]\.build[\\/]' -and
    $_.FullName -notmatch '[\\/]build[^\\/]*[\\/]'
})
$linkPattern = [regex]'\[[^\]]+\]\((?!https?://|mailto:|#)(?<target>[^)#]+)(?:#[^)]*)?\)'
foreach ($file in $markdownFiles) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($match in $linkPattern.Matches($text)) {
        $target = [System.Uri]::UnescapeDataString($match.Groups["target"].Value.Trim('<', '>'))
        $resolved = [System.IO.Path]::GetFullPath((Join-Path $file.DirectoryName $target))
        if (-not (Test-Path -LiteralPath $resolved)) { throw "Broken Markdown link in $($file.FullName): $target" }
    }
}

$ctestOutput = & ctest --test-dir (Join-Path $root $BuildDir) -N 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "Unable to enumerate CTest inventory" }
$testMatch = [regex]::Match($ctestOutput, 'Total Tests:\s*(\d+)')
if (-not $testMatch.Success) { throw "CTest total was not found" }
$testCount = $testMatch.Groups[1].Value
foreach ($entry in @(@("README.md", $readme), @("STATUS.md", $status), @("AGENTS.md", $agents))) {
    if ($entry[1] -notmatch "\b$testCount\b") { throw "$($entry[0]) does not contain the live CTest count $testCount" }
}

Write-Host "Documentation consistency audit passed: cursor $cursor, $testCount CTest entries, $($markdownFiles.Count) Markdown files."
