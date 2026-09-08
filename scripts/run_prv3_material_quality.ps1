param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "build_modular_x64",
    [string]$ReportPath = "docs/reports/phase_prv3_quality_validation_v1.json",
    [string]$ArtifactDir = "build_modular_x64/artifacts/Release/evidence/prv3_quality",
    [ValidateRange(128, 512)][int]$Samples = 500,
    [string[]]$FixtureNames = @()
)

$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-TextSha256([string]$Text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    return [Convert]::ToHexString(
        [System.Security.Cryptography.SHA256]::HashData($bytes)
    ).ToLowerInvariant()
}

function Invoke-Isolated(
    [string]$Executable,
    [string[]]$Arguments,
    [string]$WorkingDirectory) {
    Push-Location $WorkingDirectory
    try {
        $text = & $Executable @Arguments 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        throw "PRV.3 quality command failed ($exitCode): $Executable $($Arguments -join ' ')`n$text"
    }
    return $text
}

function Convert-KeyValues([string]$Text) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches(
        $Text, '(?m)^(?<key>[a-z_]+)=(?<value>.*)\r?$')) {
        $values[$match.Groups['key'].Value] =
            $match.Groups['value'].Value.Trim()
    }
    return $values
}

function Invoke-LoggedRunner(
    [string[]]$Arguments,
    [string]$Prefix,
    [string]$InputIdentity) {
    $stdoutPath = "$Prefix.runner.stdout.log"
    $stderrPath = "$Prefix.runner.stderr.log"
    $exitPath = "$Prefix.runner.exit.txt"
    $pidPath = "$Prefix.runner.pid.txt"
    $requestPath = "$Prefix.runner.request.json"
    $identityPath = "$Prefix.runner.input.txt"
    $reuse = (Test-Path -LiteralPath $stdoutPath -PathType Leaf) -and
        (Test-Path -LiteralPath $exitPath -PathType Leaf) -and
        (Test-Path -LiteralPath $identityPath -PathType Leaf) -and
        ((Get-Content -Raw -LiteralPath $exitPath).Trim() -eq "0") -and
        ((Get-Content -Raw -LiteralPath $identityPath).Trim() -eq $InputIdentity)
    if ($reuse) {
        $cached = Convert-KeyValues (Get-Content -Raw -LiteralPath $stdoutPath)
        if ([uint64]$cached.completed_samples -eq $Samples -and
            (Test-Path -LiteralPath ([string]$cached.final_raw) -PathType Leaf)) {
            return $cached
        }
    }
    $identityMatches = (Test-Path -LiteralPath $identityPath -PathType Leaf) -and
        ((Get-Content -Raw -LiteralPath $identityPath).Trim() -eq $InputIdentity)
    $wrapperProcess = $null
    if ($identityMatches -and (Test-Path -LiteralPath $pidPath -PathType Leaf) -and
        -not (Test-Path -LiteralPath $exitPath -PathType Leaf)) {
        $wrapperPid = [int](Get-Content -Raw -LiteralPath $pidPath)
        $wrapperProcess = Get-Process -Id $wrapperPid -ErrorAction SilentlyContinue
    }
    if (-not $wrapperProcess) {
        Remove-Item -LiteralPath $stdoutPath, $stderrPath, $exitPath,
            $pidPath, $requestPath -Force -ErrorAction SilentlyContinue
        [System.IO.File]::WriteAllText(
            $identityPath, $InputIdentity + "`n",
            [System.Text.UTF8Encoding]::new($false))
        $request = [ordered]@{
            executable = $runner
            working_directory = $isolation
            arguments = $Arguments
            stdout = $stdoutPath
            stderr = $stderrPath
            exit_code = $exitPath
        }
        [System.IO.File]::WriteAllText(
            $requestPath,
            (($request | ConvertTo-Json -Depth 20).Replace("`r`n", "`n")) + "`n",
            [System.Text.UTF8Encoding]::new($false))
        $start = [System.Diagnostics.ProcessStartInfo]::new()
        $start.FileName = (Get-Command pwsh -ErrorAction Stop).Source
        $start.WorkingDirectory = $isolation
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        foreach ($argument in @(
            "-NoProfile", "-File",
            (Join-Path $RepoRoot "scripts/invoke_logged_process.ps1"),
            "-Request", $requestPath)) {
            [void]$start.ArgumentList.Add($argument)
        }
        $wrapperProcess = [System.Diagnostics.Process]::Start($start)
        [System.IO.File]::WriteAllText(
            $pidPath, [string]$wrapperProcess.Id + "`n",
            [System.Text.UTF8Encoding]::new($false))
    }
    $wrapperProcess.WaitForExit()
    if (-not (Test-Path -LiteralPath $exitPath -PathType Leaf)) {
        throw "PRV.3 quality runner wrapper omitted its exit status"
    }
    $exitCode = [int](Get-Content -Raw -LiteralPath $exitPath)
    $stdout = if (Test-Path -LiteralPath $stdoutPath) {
        Get-Content -Raw -LiteralPath $stdoutPath
    } else { "" }
    $stderr = if (Test-Path -LiteralPath $stderrPath) {
        Get-Content -Raw -LiteralPath $stderrPath
    } else { "" }
    if ($exitCode -ne 0) {
        throw "PRV.3 quality runner failed ($exitCode)`n$stderr`n$stdout"
    }
    return Convert-KeyValues $stdout
}

function Invoke-QualityRender(
    [System.Collections.IDictionary]$Fixture) {
    $scene = Join-Path $fixtureRoot $Fixture.file
    $id = "$($Fixture.name)-1280x720-spp$Samples-$($Fixture.transport)"
    $prefix = Join-Path $artifactFullPath $id
    $inputIdentity = Get-TextSha256 (@(
        "ure.phase_prv3.quality-run/1.0"
        Get-Sha256 $runtime
        Get-Sha256 $worker
        Get-Sha256 $runner
        Get-Sha256 $validator
        Get-Sha256 $qualityScript
        Get-Sha256 $wrapperScript
        Get-Sha256 $comparator
        $systemDeviceIdentity
        Get-Sha256 $scene
        [string]$Samples
        [string]$Fixture.transport
        [string]$Fixture.eligible
    ) -join "`n")
    $cachePath = "$prefix.run.json"
    $cachedFinal = "$prefix.spp$Samples.pfm"
    if ((Test-Path -LiteralPath $cachePath -PathType Leaf) -and
        (Test-Path -LiteralPath $cachedFinal -PathType Leaf) -and
        (Test-Path -LiteralPath "$prefix.png" -PathType Leaf) -and
        (Test-Path -LiteralPath "$prefix.metrics.json" -PathType Leaf) -and
        (Test-Path -LiteralPath "$prefix.runner.stdout.log" -PathType Leaf)) {
        $cached = Get-Content -Raw -LiteralPath $cachePath |
            ConvertFrom-Json -Depth 100
        $logged = Convert-KeyValues (
            Get-Content -Raw -LiteralPath "$prefix.runner.stdout.log")
        $rawPaths = @([string]$logged.low_raw, [string]$logged.mid_raw,
                      [string]$logged.final_raw)
        $rawHashes = @([string]$cached.raw_authority.low_sha256,
                       [string]$cached.raw_authority.mid_sha256,
                       [string]$cached.raw_authority.final_sha256)
        $artifactsValid = $cached.execution_input_identity -eq $inputIdentity -and
            $cached.device.identity -eq [string]$logged.device_identity
        for ($index = 0; $index -lt $rawPaths.Count; ++$index) {
            $artifactsValid = $artifactsValid -and
                (Test-Path -LiteralPath $rawPaths[$index] -PathType Leaf) -and
                ((Get-Sha256 $rawPaths[$index]) -eq $rawHashes[$index])
        }
        if ($artifactsValid) {
            [void](Invoke-Isolated $python @(
                $validator, "--low", $rawPaths[0], "--mid", $rawPaths[1],
                "--final", $rawPaths[2], "--png", "$prefix.png",
                "--report", "$prefix.metrics.json"
            ) $isolation)
            $revalidated = Get-Content -Raw -LiteralPath "$prefix.metrics.json" |
                ConvertFrom-Json -Depth 100
            if ($revalidated.derived_view.sha256 -eq
                    $cached.derived_png.sha256 -and
                $revalidated.raw_authority.final_sha256 -eq
                    $cached.raw_authority.final_sha256) {
                return $cached
            }
        }
    }
    $run = Invoke-LoggedRunner @(
        $Fixture.transport, $runtime, $worker, $scene, [string]$Samples, $prefix
    ) $prefix $inputIdentity
    foreach ($key in @(
        "accepted_samples", "completed_samples", "frame", "elapsed_ms",
        "eligible_integrator_modes", "qualified_integrator_modes",
        "executed_integrator_modes", "build_identity", "snapshot_identity",
        "objective_identity", "plan_identity", "frame_content_identity",
        "device_identity", "backend", "provider", "device_name", "adapter_id",
        "driver_identity", "compiler_identity", "total_memory_bytes",
        "available_memory_bytes", "selected_memory_budget_bytes", "low_raw",
        "mid_raw", "final_raw")) {
        if ([string]::IsNullOrWhiteSpace([string]$run[$key])) {
            throw "PRV.3 quality $id omitted $key"
        }
    }
    $eligible = [uint64]$run.eligible_integrator_modes
    $qualified = [uint64]$run.qualified_integrator_modes
    $executed = [uint64]$run.executed_integrator_modes
    if ($run.frame -ne "1280x720" -or
        [uint64]$run.accepted_samples -ne $Samples -or
        [uint64]$run.completed_samples -ne $Samples -or
        $eligible -ne [uint64]$Fixture.eligible -or
        $qualified -eq 0 -or $executed -eq 0 -or
        ($qualified -band $eligible) -ne $qualified -or
        ($executed -band $qualified) -ne $executed) {
        throw "PRV.3 quality $id has invalid work or integrator evidence"
    }
    $png = "$prefix.png"
    $metricsPath = "$prefix.metrics.json"
    [void](Invoke-Isolated $python @(
        $validator, "--low", $run.low_raw, "--mid", $run.mid_raw,
        "--final", $run.final_raw, "--png", $png, "--report", $metricsPath
    ) $isolation)
    $image = Get-Content -Raw -LiteralPath $metricsPath |
        ConvertFrom-Json -Depth 100
    $result = [ordered]@{
        id = $id
        execution_input_identity = $inputIdentity
        capability = $Fixture.capability
        transport = $Fixture.transport
        scene = "tests/assets/product_e2e/prv3/$($Fixture.file)"
        scene_sha256 = Get-Sha256 $scene
        requested_samples = $Samples
        accepted_samples = [uint64]$run.accepted_samples
        completed_samples = [uint64]$run.completed_samples
        resolution = $run.frame
        elapsed_ms = [uint64]$run.elapsed_ms
        eligible_integrator_modes = $eligible
        qualified_integrator_modes = $qualified
        executed_integrator_modes = $executed
        identities = [ordered]@{
            build = $run.build_identity
            snapshot = $run.snapshot_identity
            objective = $run.objective_identity
            plan = $run.plan_identity
            frame_content = $run.frame_content_identity
        }
        device = [ordered]@{
            identity = $run.device_identity
            backend = [uint32]$run.backend
            provider = [uint32]$run.provider
            name = $run.device_name
            adapter_id = $run.adapter_id
            driver_identity = $run.driver_identity
            compiler_identity = $run.compiler_identity
            total_memory_bytes = [uint64]$run.total_memory_bytes
            available_memory_bytes = [uint64]$run.available_memory_bytes
            selected_memory_budget_bytes = [uint64]$run.selected_memory_budget_bytes
        }
        raw_authority = [ordered]@{
            format = $image.raw_authority.format
            orientation = $image.raw_authority.orientation
            color_space = $image.raw_authority.color_space
            low_sha256 = $image.raw_authority.low_sha256
            mid_sha256 = $image.raw_authority.mid_sha256
            final_sha256 = $image.raw_authority.final_sha256
        }
        derived_png = [ordered]@{
            file = "$id.png"
            sha256 = $image.derived_view.sha256
            view_transform = $image.derived_view.view_transform
        }
        metrics = $image.metrics
        convergence = $image.convergence
    }
    [System.IO.File]::WriteAllText(
        $cachePath,
        (($result | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")) + "`n",
        [System.Text.UTF8Encoding]::new($false))
    return $result
}

$buildPath = Resolve-RepositoryPath $BuildDir
$reportFullPath = Resolve-RepositoryPath $ReportPath
$artifactFullPath = Resolve-RepositoryPath $ArtifactDir
$fixtureRoot = Join-Path $RepoRoot "tests/assets/product_e2e/prv3"
$runtime = Join-Path $buildPath "artifacts/Release/bin/ultrarender_runtime_1.dll"
$worker = Join-Path $buildPath "artifacts/Release/bin/ultrarender_worker_1.exe"
$runner = Join-Path $buildPath "artifacts/Release/bin/product_scenario_runner.exe"
$validator = Join-Path $RepoRoot "scripts/validate_product_image.py"
$qualityScript = [System.IO.Path]::GetFullPath($PSCommandPath)
$wrapperScript = Join-Path $RepoRoot "scripts/invoke_logged_process.ps1"
$comparator = Join-Path $RepoRoot "scripts/compare_product_images.py"
$systemDeviceIdentity = (& nvidia-smi --query-gpu=uuid,driver_version `
    --format=csv,noheader 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or
    [string]::IsNullOrWhiteSpace($systemDeviceIdentity)) {
    throw "PRV.3 quality evidence requires a readable NVIDIA device and driver identity"
}
$systemDeviceIdentity = Get-TextSha256 $systemDeviceIdentity
$python = (Get-Command python -ErrorAction Stop).Source
$isolation = Join-Path $buildPath "prv3_quality_isolated"
New-Item -ItemType Directory -Force -Path $artifactFullPath, $isolation |
    Out-Null

$fixtures = @(
    [ordered]@{ name = "material-matrix"; file = "material_matrix_1280x720.urescene"; capability = "texture/spectral/mix/layer"; transport = "direct"; eligible = 55 },
    [ordered]@{ name = "glass"; file = "glass_1280x720.urescene"; capability = "dielectric/glass"; transport = "worker"; eligible = 255 },
    [ordered]@{ name = "mie-volume"; file = "mie_volume_1280x720.urescene"; capability = "volume/Mie"; transport = "direct"; eligible = 51 },
    [ordered]@{ name = "diffractive"; file = "diffractive_1280x720.urescene"; capability = "radiometric diffractive material"; transport = "worker"; eligible = 1 },
    [ordered]@{ name = "fluorescent"; file = "fluorescent_1280x720.urescene"; capability = "fluorescent material"; transport = "direct"; eligible = 1 }
)
foreach ($path in @($runtime, $worker, $runner, $validator, $qualityScript,
                    $wrapperScript, $comparator, $python)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PRV.3 quality input is missing: $path"
    }
}

$knownNames = @($fixtures | ForEach-Object { $_.name })
foreach ($name in $FixtureNames) {
    if ($name -notin $knownNames) {
        throw "Unknown PRV.3 quality fixture: $name"
    }
}
$selectedFixtures = if ($FixtureNames.Count -eq 0) {
    $fixtures
} else {
    @($fixtures | Where-Object { $_.name -in $FixtureNames })
}
[void]@($selectedFixtures | ForEach-Object { Invoke-QualityRender $_ })
$missingCaches = @($fixtures | Where-Object {
    $id = "$($_.name)-1280x720-spp$Samples-$($_.transport)"
    -not (Test-Path -LiteralPath (Join-Path $artifactFullPath "$id.run.json") -PathType Leaf)
})
if ($missingCaches.Count -ne 0) {
    Write-Output "PRV.3 quality subset passed; remaining: $($missingCaches.name -join ', ')"
    return
}
$runs = @($fixtures | ForEach-Object { Invoke-QualityRender $_ })
$deviceIdentities = @($runs | ForEach-Object { $_.device.identity } |
    Sort-Object -Unique)
if ($deviceIdentities.Count -ne 1) {
    throw "PRV.3 quality runs selected different execution devices"
}

$report = [ordered]@{
    schema = "ure.phase_prv3.quality-validation/1.0"
    status = "AutomatedPassed"
    product_release_declared = $false
    evidence_tier = "ProductQuality"
    production_profile = "exact-build ProductJob 0.4 default automatic production profile"
    artifact_retention = "Authoritative raw PFM and derived PNG remain in the configured local evidence directory; this report binds their hashes."
    source = [ordered]@{
        fixture_manifest = "tests/assets/product_e2e/prv3/fixture_manifest.json"
        fixture_manifest_sha256 = Get-Sha256 (Join-Path $fixtureRoot "fixture_manifest.json")
        runtime_sha256 = Get-Sha256 $runtime
        worker_sha256 = Get-Sha256 $worker
        scenario_runner_sha256 = Get-Sha256 $runner
        image_validator_sha256 = Get-Sha256 $validator
        orchestration_sha256 = Get-Sha256 $qualityScript
        process_wrapper_sha256 = Get-Sha256 $wrapperScript
        image_comparator_sha256 = Get-Sha256 $comparator
        system_device_identity = $systemDeviceIdentity
    }
    runs = $runs
    visual_review = [ordered]@{
        required = $true
        record = "docs/reports/phase_prv3_visual_review_v1.json"
    }
    semantic_digest = ""
}
$report.semantic_digest = Get-TextSha256 (
    $report | ConvertTo-Json -Depth 100 -Compress)
$json = ($report | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")
New-Item -ItemType Directory -Force -Path (
    Split-Path -Parent $reportFullPath) | Out-Null
[System.IO.File]::WriteAllText(
    $reportFullPath, $json + "`n",
    [System.Text.UTF8Encoding]::new($false))

Write-Output "PRV.3 material quality evidence passed: five 1280x720 scenes at $Samples spp"
