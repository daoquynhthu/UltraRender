$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Validator = Join-Path $PSScriptRoot "validate_phase_r_industrial_report.ps1"
$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("ure-rp7-" + [guid]::NewGuid().ToString("N"))

function Write-Fixture {
    param([string]$Path, [string]$Profile)
    $hash = "a" * 64
    $suites = @("integrator_smoke", "light_sampling", "path_guiding", "restir_pt", "specular_manifold", "bidirectional", "mlt", "volume_mie") |
        ForEach-Object { [ordered]@{ name = $_; status = "passed"; artifact_sha256 = $hash } }
    $report = [ordered]@{
        schema = "ure.phase_r.industrial_validation.v1"
        profile = $Profile.ToLowerInvariant()
        status = "passed"
        identity = [ordered]@{ git_commit = "b" * 40; dirty = $false }
        suites = $suites
        metrics = [ordered]@{
            samples_per_second = [ordered]@{ status = "collected"; value = 1 }
            spectral_color_error = [ordered]@{ status = "collected"; value = 0 }
            variance = [ordered]@{ status = "collected"; value = 0 }
            mse = [ordered]@{ status = "collected"; value = 0 }
            time_to_error_seconds = [ordered]@{ status = "collected"; value = 1 }
        }
        integrator_gates = @("path_guiding", "restir_pt", "specular_manifold", "bdpt", "vcm", "mlt") |
            ForEach-Object { [ordered]@{ mode = $_; positive_benefit_count = 1; boundary_failure_count = 1 } }
        farm = [ordered]@{ status = "not_collected" }
        nsight = [ordered]@{ status = "not_collected" }
    }
    if ($Profile -eq "Closure") {
        $report.farm = [ordered]@{
            schema = "ure.phase_r.farm_evidence.v1"; status = "collected"; run_id = "fixture"; expected_sample_count = 20
            shards = @(
                [ordered]@{ worker_id = "w0"; sample_begin = 0; sample_end = 10; artifact_sha256 = $hash },
                [ordered]@{ worker_id = "w1"; sample_begin = 10; sample_end = 20; artifact_sha256 = $hash }
            )
        }
        $report.nsight = [ordered]@{
            schema = "ure.phase_r.nsight_evidence.v1"; status = "collected"; tool_version = "fixture"; source_sha256 = $hash
            vram_source_sha256 = $hash; vram_measurement = "device_used_delta"; profiled_executable_sha256 = $hash
            peak_vram_bytes = 1; total_kernel_launches = 1
            kernels = @([ordered]@{ name = "fixture_kernel"; launches = 1; occupancy_pct = 50 })
        }
    }
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Assert-Fails {
    param([scriptblock]$Body, [string]$Name)
    try { & $Body; throw "expected failure: $Name" } catch {
        if ($_.Exception.Message -eq "expected failure: $Name") { throw }
    }
}

New-Item -ItemType Directory -Path $TempDir | Out-Null
try {
    $localPath = Join-Path $TempDir "local.json"
    Write-Fixture $localPath "LocalQuick"
    & $Validator -ReportPath $localPath -Profile LocalQuick

    $closurePath = Join-Path $TempDir "closure.json"
    Write-Fixture $closurePath "Closure"
    & $Validator -ReportPath $closurePath -Profile Closure

    $broken = Get-Content -Raw $closurePath | ConvertFrom-Json
    $broken.farm.shards[1].sample_begin = 9
    $broken | ConvertTo-Json -Depth 10 | Set-Content $closurePath
    Assert-Fails { & $Validator -ReportPath $closurePath -Profile Closure } "overlapping farm ranges"

    Write-Fixture $closurePath "Closure"
    $broken = Get-Content -Raw $closurePath | ConvertFrom-Json
    $broken.nsight.kernels[0].occupancy_pct = 101
    $broken | ConvertTo-Json -Depth 10 | Set-Content $closurePath
    Assert-Fails { & $Validator -ReportPath $closurePath -Profile Closure } "invalid occupancy"

    $csvPath = Join-Path $TempDir "ncu.csv"
    @(
        'Kernel Name,Metric Name,Metric Value',
        'shade_kernel,sm__warps_active.avg.pct_of_peak_sustained_active,62.5',
        'shade_kernel,sm__warps_active.avg.pct_of_peak_sustained_active,57.5'
    ) | Set-Content -LiteralPath $csvPath -Encoding utf8
    $vramPath = Join-Path $TempDir "vram.json"
    [ordered]@{
        schema = "ure.phase_r.vram_evidence.v1"; status = "collected"; measurement = "device_used_delta"
        executable_sha256 = "a" * 64; peak_vram_bytes = 4096
    } | ConvertTo-Json | Set-Content -LiteralPath $vramPath -Encoding utf8
    $nsightPath = Join-Path $TempDir "nsight.json"
    & (Join-Path $PSScriptRoot "convert_phase_r_nsight_csv.ps1") -CsvPath $csvPath -VramEvidencePath $vramPath -OutputPath $nsightPath -ToolVersion "fixture"
    $nsight = Get-Content -Raw $nsightPath | ConvertFrom-Json
    if ($nsight.total_kernel_launches -ne 2 -or $nsight.kernels[0].occupancy_pct -ne 60) {
        throw "Nsight conversion did not preserve launch and occupancy evidence"
    }

    @(
        '==PROF== Connected to process 1',
        '"ID","Kernel Name","sm__warps_active.avg.pct_of_peak_sustained_active"',
        '"","","%"',
        '"0","shade_kernel","42.5"',
        '"1","shade_kernel","57.5"',
        '==PROF== Disconnected from process 1'
    ) | Set-Content -LiteralPath $csvPath -Encoding utf8
    & (Join-Path $PSScriptRoot "convert_phase_r_nsight_csv.ps1") -CsvPath $csvPath -VramEvidencePath $vramPath -OutputPath $nsightPath -ToolVersion "wide-fixture"
    $nsight = Get-Content -Raw $nsightPath | ConvertFrom-Json
    if ($nsight.total_kernel_launches -ne 2 -or $nsight.kernels[0].occupancy_pct -ne 50) {
        throw "wide Nsight conversion did not preserve launch and occupancy evidence"
    }

    "a" | Set-Content -LiteralPath (Join-Path $TempDir "shard0.bin")
    "b" | Set-Content -LiteralPath (Join-Path $TempDir "shard1.bin")
    $manifestPath = Join-Path $TempDir "farm_manifest.json"
    [ordered]@{
        schema = "ure.phase_r.farm_manifest.v1"; run_id = "fixture"; expected_sample_count = 20
        shards = @(
            [ordered]@{ worker_id = "w0"; sample_begin = 0; sample_end = 10; artifact = "shard0.bin" },
            [ordered]@{ worker_id = "w1"; sample_begin = 10; sample_end = 20; artifact = "shard1.bin" }
        )
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath
    $farmPath = Join-Path $TempDir "farm.json"
    & (Join-Path $PSScriptRoot "build_phase_r_farm_evidence.ps1") -ManifestPath $manifestPath -OutputPath $farmPath
    $farm = Get-Content -Raw $farmPath | ConvertFrom-Json
    if ($farm.shards.Count -ne 2 -or $farm.shards[0].artifact_sha256 -notmatch '^[0-9a-f]{64}$') {
        throw "farm evidence conversion did not hash every shard"
    }

    Write-Host "Phase R industrial report contract tests passed"
} finally {
    Remove-Item -LiteralPath $TempDir -Recurse -Force
}
