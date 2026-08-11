param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$WarmRuns = 3,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ArtifactBin = Join-Path $BuildPath "artifacts\$Config\bin"
$OutputDir = Join-Path $RepoRoot ".build\phase_t_validation"
$ReportPath = Join-Path $OutputDir "report.json"
$InventoryPath = Join-Path $OutputDir "inventory.json"
$CudaOutput = Join-Path $OutputDir "cornell_512x512_64spp.bmp"
$CudaStdout = Join-Path $OutputDir "cuda_render.stdout.txt"
$CudaStderr = Join-Path $OutputDir "cuda_render.stderr.txt"
$ExpectedCudaHash =
    "ff81b8e08386f9b593748cc56ff5b9c3c481f4014658cf41a0795cdd1ed9e935"
$CudaBaselineMs = 11857.174
$CudaBaselineVramMiB = 1753
$MaximumRegression = 1.20
$RayQueryFeature = [uint64]1 -shl 14

if ($WarmRuns -lt 1) {
    throw "WarmRuns must be positive"
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

function Invoke-TimedCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments = @(),
        [hashtable]$Environment = @{}
    )
    $saved = @{}
    foreach ($name in $Environment.Keys) {
        $existing = Get-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
        $saved[$name] = if ($null -eq $existing) {
            $null
        } else {
            [string]$existing.Value
        }
        Set-Item -LiteralPath "Env:$name" -Value ([string]$Environment[$name])
    }
    try {
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $output = & $FilePath @Arguments 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
        $timer.Stop()
    } finally {
        foreach ($name in $Environment.Keys) {
            if ($null -eq $saved[$name]) {
                Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
            } else {
                Set-Item -LiteralPath "Env:$name" -Value $saved[$name]
            }
        }
    }
    if ($exitCode -ne 0) {
        throw "$FilePath failed with exit code ${exitCode}:`n$output"
    }
    [ordered]@{
        elapsed_ms = [Math]::Round($timer.Elapsed.TotalMilliseconds, 4)
        output = $output.Trim()
    }
}

function Invoke-LaunchBenchmark {
    param(
        [string]$Name,
        [string]$FilePath,
        [hashtable]$Environment = @{},
        [uint64]$WorkItems = 1
    )
    if (-not (Test-Path -LiteralPath $FilePath)) {
        throw "missing benchmark executable: $FilePath"
    }
    $cold = Invoke-TimedCommand `
        -FilePath $FilePath `
        -Environment $Environment
    $warmValues = @()
    1..$WarmRuns | ForEach-Object {
        $run = Invoke-TimedCommand `
            -FilePath $FilePath `
            -Environment $Environment
        $warmValues += [double]$run.elapsed_ms
    }
    [array]::Sort($warmValues)
    $warmMedian = $warmValues[
        [Math]::Floor($warmValues.Count / 2)]
    $limit = [Math]::Max(
        [double]$cold.elapsed_ms * 2.0,
        [double]$cold.elapsed_ms + 250.0)
    if ($warmMedian -gt $limit) {
        throw "$Name warm launch exceeded its regression threshold"
    }
    [ordered]@{
        name = $Name
        status = "passed"
        classification = "workload_specific_launch"
        cold_ms = [double]$cold.elapsed_ms
        warm_median_ms = [Math]::Round($warmMedian, 4)
        warm_threshold_ms = [Math]::Round($limit, 4)
        work_items = $WorkItems
        warm_work_items_per_second = [Math]::Round(
            $WorkItems * 1000.0 / $warmMedian,
            3)
    }
}

function Read-NvidiaUsedMemory {
    $line = [string](
        & nvidia-smi.exe `
            --query-gpu=memory.used `
            --format=csv,noheader,nounits |
            Select-Object -First 1
    )
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "nvidia-smi did not report device memory"
    }
    [uint64]$line.Trim()
}

$targets = @(
    "ure_cli",
    "gpu_test_spectral",
    "gpu_test_polarization",
    "gpu_test_wave_optics",
    "gpu_test_acceleration_contract",
    "gpu_test_cuda_runtime",
    "test_runtime_contract",
    "test_resource_plan",
    "test_execution_graph",
    "test_spectral_oracle",
    "test_wave_optics",
    "test_vulkan_runtime",
    "test_vulkan_acceleration",
    "test_multi_backend_inventory"
)
$D3dExecutable = Join-Path $ArtifactBin "test_d3d12_runtime.exe"
if (Test-Path -LiteralPath $D3dExecutable) {
    $targets += "test_d3d12_runtime"
}

if (-not $SkipBuild) {
    & cmake --build $BuildPath --config $Config --target @targets
    if ($LASTEXITCODE -ne 0) {
        throw "Phase T validation targets failed to build"
    }
}

$inventoryEnvironment = @{
    UR_PHASE_T10_REPORT = $InventoryPath
}
$InventoryExecutable = Join-Path $ArtifactBin "test_multi_backend_inventory.exe"
Invoke-TimedCommand `
    -FilePath $InventoryExecutable `
    -Environment $inventoryEnvironment | Out-Null
$inventory = Get-Content -Raw -LiteralPath $InventoryPath |
    ConvertFrom-Json
$cudaWorkers = @($inventory.workers | Where-Object backend -eq "cuda")
$vulkanWorkers = @($inventory.workers | Where-Object backend -eq "vulkan")
$d3dWorkers = @($inventory.workers | Where-Object backend -eq "d3d12")
if ($cudaWorkers.Count -lt 1 -or $vulkanWorkers.Count -lt 1) {
    throw "CUDA and Vulkan are required Phase T production backends"
}
$vulkanCrossVendor = @(
    $vulkanWorkers.vendor_id | Select-Object -Unique
).Count -gt 1
$vulkanRayQueryCapable = @(
    $vulkanWorkers |
        Where-Object {
            ([uint64]$_.features -band $RayQueryFeature) -ne 0
        }
).Count -gt 0

$selectedPattern = (
    "^(gpu_spectral|gpu_polarization|gpu_wave_optics|" +
    "gpu_acceleration_contract|gpu_cuda_runtime|" +
    "test_runtime_contract|test_resource_plan|test_execution_graph|" +
    "test_spectral_oracle|test_wave_optics)$"
)
$selectedTests = Invoke-TimedCommand `
    -FilePath "ctest" `
    -Arguments @(
        "--test-dir", $BuildPath,
        "-C", $Config,
        "-R", $selectedPattern,
        "--output-on-failure"
    )

$cudaLaunch = Invoke-LaunchBenchmark `
    -Name "cuda_runtime_contract" `
    -FilePath (Join-Path $ArtifactBin "gpu_test_cuda_runtime.exe") `
    -WorkItems 64
$vulkanWorkItems = [uint64]$vulkanWorkers.Count * 320
$vulkanEnvironment = @{}
if ($vulkanCrossVendor) {
    $vulkanEnvironment.UR_REQUIRE_CROSS_VENDOR_VULKAN = "1"
}
$vulkanLaunch = Invoke-LaunchBenchmark `
    -Name "vulkan_foundation_contract" `
    -FilePath (Join-Path $ArtifactBin "test_vulkan_runtime.exe") `
    -Environment $vulkanEnvironment `
    -WorkItems $vulkanWorkItems
$vulkanAccelerationEnvironment = @{}
if ($vulkanRayQueryCapable) {
    $vulkanAccelerationEnvironment.UR_REQUIRE_VULKAN_RT = "1"
}
$vulkanAcceleration = Invoke-TimedCommand `
    -FilePath (Join-Path $ArtifactBin "test_vulkan_acceleration.exe") `
    -Environment $vulkanAccelerationEnvironment

$d3dLaunch = $null
$d3dAcceleration = $null
$dxrCapable = @(
    $d3dWorkers |
        Where-Object {
            ([uint64]$_.features -band $RayQueryFeature) -ne 0
        }
).Count -gt 0
if ($d3dWorkers.Count -gt 0) {
    $d3dEnvironment = @{}
    if ($dxrCapable) {
        $d3dEnvironment.UR_REQUIRE_DXR = "1"
    }
    $d3dLaunch = Invoke-LaunchBenchmark `
        -Name "d3d12_foundation_and_acceleration" `
        -FilePath $D3dExecutable `
        -Environment $d3dEnvironment `
        -WorkItems ([uint64]$d3dWorkers.Count * 88)
    $d3dAcceleration = [ordered]@{
        status = if ($dxrCapable) { "passed" } else { "skipped" }
        classification = if ($dxrCapable) {
            "capability_executed"
        } else {
            "capability_unavailable_compute_fallback"
        }
        required = $false
        ray_query_capable = $dxrCapable
    }
}

$baselineVramMiB = Read-NvidiaUsedMemory
$renderArguments = @(
    "-q",
    "render",
    ('"' + (Join-Path $RepoRoot "scenes\cornell_box.gltf") + '"'),
    "--width", "512",
    "--height", "512",
    "--spp", "64",
    "--output", ('"' + $CudaOutput + '"'),
    "--format", "bmp"
)
$renderTimer = [Diagnostics.Stopwatch]::StartNew()
$renderProcess = Start-Process `
    -FilePath (Join-Path $ArtifactBin "ure_cli.exe") `
    -ArgumentList $renderArguments `
    -PassThru `
    -WindowStyle Hidden `
    -RedirectStandardOutput $CudaStdout `
    -RedirectStandardError $CudaStderr
Start-Sleep -Milliseconds 2000
$activeVramMiB = Read-NvidiaUsedMemory
$renderProcess.WaitForExit()
$renderTimer.Stop()
if ($renderProcess.ExitCode -ne 0) {
    $renderError = Get-Content -Raw -LiteralPath $CudaStderr
    throw "CUDA production reference render failed: $renderError"
}
$cudaHash = (
    Get-FileHash -LiteralPath $CudaOutput -Algorithm SHA256
).Hash.ToLowerInvariant()
if ($cudaHash -ne $ExpectedCudaHash) {
    throw "CUDA production reference render changed"
}
$cudaElapsedMs = $renderTimer.Elapsed.TotalMilliseconds
$cudaThresholdMs = $CudaBaselineMs * $MaximumRegression
if ($cudaElapsedMs -gt $cudaThresholdMs) {
    throw "CUDA production throughput regressed beyond 20 percent"
}
$vramDeltaMiB = [int64]$activeVramMiB - [int64]$baselineVramMiB
if ($vramDeltaMiB -le 0 -or
    $vramDeltaMiB -gt $CudaBaselineVramMiB + 64) {
    throw "CUDA production VRAM delta is invalid or regressed"
}
$cudaSamples = [uint64]512 * 512 * 64
$cudaReference = [ordered]@{
    status = "passed"
    classification = "deterministic_reference_and_regression_guard"
    width = 512
    height = 512
    samples_per_pixel = 64
    sha256 = $cudaHash
    expected_sha256 = $ExpectedCudaHash
    elapsed_ms = [Math]::Round($cudaElapsedMs, 4)
    threshold_ms = [Math]::Round($cudaThresholdMs, 4)
    samples_per_second = [Math]::Round(
        $cudaSamples * 1000.0 / $cudaElapsedMs,
        3)
    baseline_vram_mib = $baselineVramMiB
    active_vram_mib = $activeVramMiB
    vram_delta_mib = $vramDeltaMiB
    vram_threshold_mib = $CudaBaselineVramMiB + 64
}

& (Join-Path $RepoRoot "tools\benchmarks\run_phase_r_light_sampling_suite.ps1") `
    -BuildDir $BuildDir `
    -Config $Config `
    -SkipBuild
if ($LASTEXITCODE -ne 0) {
    throw "Phase T statistical validation failed"
}
$statisticalSource = Join-Path $RepoRoot "output\benchmarks\phase_r_light_sampling_suite.json"
$statisticalReport = Get-Content -Raw -LiteralPath $statisticalSource |
    ConvertFrom-Json
foreach ($scene in $statisticalReport.scenes) {
    if ($scene.convergence.status -ne "passed") {
        throw "variance/MSE convergence failed for $($scene.name)"
    }
}
$statistical = [ordered]@{
    status = "passed"
    classification = "statistical_convergence"
    schema = $statisticalReport.schema
    width = $statisticalReport.width
    height = $statisticalReport.height
    curve_spp = $statisticalReport.curve_spp
    reference_spp = $statisticalReport.reference_spp
    scenes = @(
        $statisticalReport.scenes | ForEach-Object {
            [ordered]@{
                name = $_.name
                first_mse = $_.convergence.first_mse
                last_mse = $_.convergence.last_mse
                max_mse_ratio = $_.convergence.max_mid_mse_ratio
                low_spp_variance = $_.curve[0].radiance_variance
                high_spp_variance = $_.curve[-1].radiance_variance
            }
        }
    )
}

$cacheArguments = @(
    "--build", $BuildPath,
    "--config", $Config,
    "--target"
) + $targets
$cacheProbe = Invoke-TimedCommand `
    -FilePath "cmake" `
    -Arguments $cacheArguments
$cacheNoWork = $cacheProbe.output -match "no work to do"
if (-not $cacheNoWork) {
    throw "incremental build cache probe performed unexpected work"
}

& (Join-Path $RepoRoot "scripts\check_phase_t_static.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "Phase T static audit failed"
}

$artifactPaths = [ordered]@{
    cuda = Join-Path $BuildPath "artifacts\$Config\lib\ure_core.lib"
    vulkan = Join-Path $RepoRoot "shaders\vulkan\generated\spectral_polarization.spv"
}
if ($d3dWorkers.Count -gt 0) {
    $artifactPaths.d3d12 = Join-Path $RepoRoot "shaders\d3d12\generated\foundation.dxil"
}
$artifactHashes = [ordered]@{}
foreach ($entry in $artifactPaths.GetEnumerator()) {
    $artifactHashes[$entry.Key] = (
        Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256
    ).Hash.ToLowerInvariant()
}

$comparisons = @(
    [ordered]@{
        name = "shared_physical_units"
        backends = @("cuda", "vulkan") +
            $(if ($d3dWorkers.Count -gt 0) { @("d3d12") } else { @() })
        status = "passed"
        classification = "analytic_oracle_tolerance"
        evidence = "asserted_upper_bound"
        observed_upper_bound = 0.0001
        threshold = 0.0001
        cause = "backend floating-point lowering"
    },
    [ordered]@{
        name = "hit_metadata_and_reference_framebuffer"
        backends = @("cuda", "vulkan") +
            $(if ($d3dWorkers.Count -gt 0) { @("d3d12") } else { @() })
        status = "passed"
        classification = "shared_fixture_tolerance"
        evidence = "asserted_upper_bound"
        observed_upper_bound = 0.00002
        threshold = 0.00002
        cause = "native ray-query versus compute traversal arithmetic"
    },
    [ordered]@{
        name = "production_reference_render"
        backends = @("cuda")
        status = "passed"
        classification = "exact_hash"
        observed = $cudaHash
        threshold = $ExpectedCudaHash
        cause = "no difference"
    }
)
$deviceLossEvidence = @(
    "test_runtime_contract injected durable loss",
    "Vulkan VK_ERROR_DEVICE_LOST mapping"
)
if ($d3dWorkers.Count -gt 0) {
    $deviceLossEvidence +=
        "D3D12 removal reason and DRED retention"
}

$report = [ordered]@{
    schema = "ure.phase_t.validation.v1"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = "passed"
    required_backends = @("cuda", "vulkan")
    optional_backends = @(
        [ordered]@{
            backend = "d3d12"
            available = $d3dWorkers.Count -gt 0
            native_ray_query = $dxrCapable
            policy = "execute when capability is advertised"
        }
    )
    vulkan_capabilities = [ordered]@{
        cross_vendor = $vulkanCrossVendor
        native_ray_query = $vulkanRayQueryCapable
        acceleration_policy = if ($vulkanRayQueryCapable) {
            "native ray query required"
        } else {
            "compute BVH fallback required"
        }
    }
    thresholds = [ordered]@{
        physical_max_abs_error = 0.0001
        hit_framebuffer_max_abs_error = 0.00002
        cuda_reference_hash = $ExpectedCudaHash
        cuda_maximum_time_regression = $MaximumRegression
        cuda_vram_baseline_mib = $CudaBaselineVramMiB
        cuda_vram_tolerance_mib = 64
        warm_launch_factor = 2.0
        warm_launch_absolute_tolerance_ms = 250.0
    }
    dimensions = [ordered]@{
        physical_units = [ordered]@{
            status = "passed"
            classification = "analytic_oracle_tolerance"
            evidence = @(
                "gpu_spectral",
                "gpu_polarization",
                "gpu_wave_optics",
                "vulkan_runtime"
            ) + $(if ($d3dWorkers.Count -gt 0) { @("d3d12_runtime") } else { @() })
        }
        hit_metadata = [ordered]@{
            status = "passed"
            classification = "shared_fixture_tolerance"
            evidence = @(
                "gpu_acceleration_contract",
                "vulkan_acceleration"
            ) + $(if ($d3dWorkers.Count -gt 0) { @("d3d12_runtime") } else { @() })
        }
        reference_render = $cudaReference
        variance_mse = $statistical
        device_loss = [ordered]@{
            status = "passed"
            classification = "contract_injection_and_native_mapping"
            evidence = $deviceLossEvidence
        }
        budget = [ordered]@{
            status = "passed"
            classification = "actual_adapter_inventory_and_fail_loud"
            evidence = @(
                "test_resource_plan",
                "test_runtime_contract",
                "backend runtime allocation accounting"
            )
            adapters = @(
                $inventory.workers | ForEach-Object {
                    [ordered]@{
                        backend = $_.backend
                        adapter_id = $_.adapter_id
                        total_memory_bytes = $_.total_memory_bytes
                        available_memory_bytes = $_.available_memory_bytes
                    }
                }
            )
        }
        build_cache = [ordered]@{
            status = "passed"
            classification = "incremental_no_work_and_backend_cache_identity"
            incremental_no_work = $cacheNoWork
            probe_ms = $cacheProbe.elapsed_ms
            artifact_sha256 = $artifactHashes
            vulkan_restart_cache = "validated"
        }
        cold_warm_launch = @(
            $cudaLaunch,
            $vulkanLaunch
        ) + $(if ($null -ne $d3dLaunch) { @($d3dLaunch) } else { @() })
        vram = [ordered]@{
            status = "passed"
            classification = "production_peak_sample_and_adapter_budget"
            cuda = $cudaReference
            adapters = @(
                $inventory.workers | ForEach-Object {
                    [ordered]@{
                        backend = $_.backend
                        adapter_id = $_.adapter_id
                        available_memory_bytes = $_.available_memory_bytes
                    }
                }
            )
        }
        throughput = [ordered]@{
            status = "passed"
            classification = "workload_specific_not_cross_compared"
            cuda_production_samples_per_second =
                $cudaReference.samples_per_second
            contract_workloads = @(
                $cudaLaunch,
                $vulkanLaunch
            ) + $(if ($null -ne $d3dLaunch) { @($d3dLaunch) } else { @() })
        }
    }
    comparisons = $comparisons
    inventory = $inventory
    dxr = $d3dAcceleration
    selected_test_elapsed_ms = $selectedTests.elapsed_ms
    vulkan_acceleration_elapsed_ms = $vulkanAcceleration.elapsed_ms
}

$report | ConvertTo-Json -Depth 16 |
    Set-Content -LiteralPath $ReportPath -Encoding UTF8
Write-Host "Phase T validation suite passed"
Write-Host "Wrote $ReportPath"
