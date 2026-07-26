$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $RepoRoot "build_modular_x64"
$Cli = Join-Path $BuildDir "apps\ure_cli\ure_cli.exe"
$Scene = Join-Path $RepoRoot "scenes\cornell_box.gltf"
$OutputDir = Join-Path $RepoRoot ".build\phase_t6_gate"
$SmallOutput = Join-Path $OutputDir "cornell_64x64_8spp.bmp"
$LargeOutput = Join-Path $OutputDir "cornell_512x512_64spp.bmp"
$ExpectedSmallHash =
    "9e8e27f1bbc1c48384feaec755498dcee33effdcf20092d4abb2dec0bdae9d73"
$ExpectedLargeHash =
    "ff81b8e08386f9b593748cc56ff5b9c3c481f4014658cf41a0795cdd1ed9e935"
$T5SmallMedianMs = 247.0621
$T5LargeMs = 11857.174
$T5VramMiB = 1753
$MaximumRegression = 1.20

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

& cmake --build $BuildDir --config Release --target `
    gpu_test_cuda_runtime gpu_test_render gpu_test_volume `
    gpu_test_wave_optics ure_cli
if ($LASTEXITCODE -ne 0) {
    throw "T.6 CUDA backend targets failed to build"
}

& ctest --test-dir $BuildDir -C Release `
    -R "^(gpu_render|gpu_volume|gpu_wave_optics|gpu_cuda_runtime|test_runtime_contract|test_execution_graph)$" `
    --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "T.6 CUDA backend contract tests failed"
}

$SmallTimes = @()
$SmallHashes = @()
1..5 | ForEach-Object {
    $Timer = [Diagnostics.Stopwatch]::StartNew()
    & $Cli -q render $Scene --width 64 --height 64 --spp 8 `
        --output $SmallOutput --format bmp
    if ($LASTEXITCODE -ne 0) {
        throw "T.6 small reference render failed"
    }
    $Timer.Stop()
    $SmallTimes += $Timer.Elapsed.TotalMilliseconds
    $SmallHashes += (
        Get-FileHash -LiteralPath $SmallOutput -Algorithm SHA256
    ).Hash.ToLowerInvariant()
}
[array]::Sort($SmallTimes)
$SmallMedianMs = $SmallTimes[2]
if (($SmallHashes | Select-Object -Unique).Count -ne 1 -or
    $SmallHashes[0] -ne $ExpectedSmallHash) {
    throw "T.6 small reference render changed"
}
if ($SmallMedianMs -gt
    $T5SmallMedianMs * $MaximumRegression) {
    throw "T.6 small-render median regressed beyond 20 percent"
}

$ReadUsedVram = {
    $Line = [string](
        & nvidia-smi.exe `
            --query-gpu=memory.used `
            --format=csv,noheader,nounits |
            Select-Object -First 1
    )
    if ([string]::IsNullOrWhiteSpace($Line)) {
        throw "nvidia-smi did not report device memory"
    }
    return [uint64]$Line.Trim()
}
$BaselineVramMiB = & $ReadUsedVram
$Stdout = Join-Path $OutputDir "large.stdout.txt"
$Stderr = Join-Path $OutputDir "large.stderr.txt"
$Arguments = @(
    "-q",
    "render",
    ('"' + $Scene + '"'),
    "--width",
    "512",
    "--height",
    "512",
    "--spp",
    "64",
    "--output",
    ('"' + $LargeOutput + '"'),
    "--format",
    "bmp"
)
$LargeTimer = [Diagnostics.Stopwatch]::StartNew()
$Process = Start-Process `
    -FilePath $Cli `
    -ArgumentList $Arguments `
    -PassThru `
    -WindowStyle Hidden `
    -RedirectStandardOutput $Stdout `
    -RedirectStandardError $Stderr
Start-Sleep -Milliseconds 1500
$ActiveVramMiB = & $ReadUsedVram
$Process.WaitForExit()
$LargeTimer.Stop()
if ($Process.ExitCode -ne 0) {
    Get-Content -LiteralPath $Stderr
    throw "T.6 large reference render failed"
}
$LargeMs = $LargeTimer.Elapsed.TotalMilliseconds
$LargeHash = (
    Get-FileHash -LiteralPath $LargeOutput -Algorithm SHA256
).Hash.ToLowerInvariant()
$VramDeltaMiB = $ActiveVramMiB - $BaselineVramMiB
if ($LargeHash -ne $ExpectedLargeHash) {
    throw "T.6 large reference render changed"
}
if ($LargeMs -gt $T5LargeMs * $MaximumRegression) {
    throw "T.6 large-render time regressed beyond 20 percent"
}
if ($VramDeltaMiB -eq 0 -or
    $VramDeltaMiB -gt $T5VramMiB + 64) {
    throw "T.6 VRAM delta is invalid or regressed"
}

$SdkFreeGate = Join-Path $RepoRoot "scripts\run_phase_t5_execution_gate.ps1"
& $SdkFreeGate

$PortableBuild = Join-Path $RepoRoot ".build\phase_t6_root_sdk_free"
$PortableInstall = Join-Path $RepoRoot ".build\phase_t6_install"
$ConsumerSource = Join-Path $RepoRoot "tests\sdk_free\package_consumer"
$ConsumerBuild = Join-Path $RepoRoot ".build\phase_t6_package_consumer"
& cmake -S $RepoRoot -B $PortableBuild `
    -G "Visual Studio 17 2022" -A x64 `
    -DUR_ENABLE_CUDA=OFF `
    -DUR_ENABLE_VULKAN=OFF `
    -DUR_BUILD_TESTS=OFF `
    -DUR_BUILD_CLI=OFF `
    -DUR_BUILD_PHYSICS=OFF
if ($LASTEXITCODE -ne 0) {
    throw "T.6 CUDA-optional root configure failed"
}
$PortableCache = Get-Content -Raw (
    Join-Path $PortableBuild "CMakeCache.txt"
)
if ($PortableCache -match "CMAKE_CUDA_COMPILER") {
    throw "T.6 CUDA-optional root configured a CUDA compiler"
}
& cmake --build $PortableBuild --config Release --target `
    ure_runtime ure_sceneio ure_config
if ($LASTEXITCODE -ne 0) {
    throw "T.6 backend-neutral root targets failed to build"
}
& cmake --install $PortableBuild --config Release `
    --prefix $PortableInstall
if ($LASTEXITCODE -ne 0) {
    throw "T.6 SDK-free package install failed"
}
& cmake -S $ConsumerSource -B $ConsumerBuild `
    -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_PREFIX_PATH=$PortableInstall"
if ($LASTEXITCODE -ne 0) {
    throw "T.6 SDK-free package consumer configure failed"
}
& cmake --build $ConsumerBuild --config Release
if ($LASTEXITCODE -ne 0) {
    throw "T.6 SDK-free package consumer build failed"
}
& (Join-Path $ConsumerBuild "Release\ultrarender_sdk_free_consumer.exe")
if ($LASTEXITCODE -ne 0) {
    throw "T.6 SDK-free package consumer execution failed"
}

Write-Host ((
    "Phase T.6 CUDA backend gate passed: small median {0:N2} ms, " +
    "large {1:N2} ms, VRAM delta {2} MiB") -f
        $SmallMedianMs,
        $LargeMs,
        $VramDeltaMiB
)
