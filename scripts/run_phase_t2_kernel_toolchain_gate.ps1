param(
    [string]$OutputDir = "output/phase_t2",
    [string]$ToolRoot = ".build/toolchains",
    [switch]$Offline
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$SlangVersion = "2026.14"
$SlangArchive = "slang-$SlangVersion-windows-x86_64.zip"
$SlangUrl = "https://github.com/shader-slang/slang/releases/download/v$SlangVersion/$SlangArchive"
$SlangSha256 = "36029c50ef0c82f2616ffb02e0ed27d642cb44a2a297d531cc2ad333b85b85b6"
$Entries = @(
    "spectral_conversion",
    "mueller_transport",
    "queue_compaction",
    "bsdf_sampling",
    "wave_propagation",
    "traversal_query"
)

function Require-Success {
    param([string]$Label)
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Remove-TrailingNull {
    param([string]$Path)
    $resolved = Resolve-Path $Path
    $bytes = [IO.File]::ReadAllBytes($resolved)
    if ($bytes.Length -gt 0 -and $bytes[-1] -eq 0) {
        [IO.File]::WriteAllBytes($resolved, $bytes[0..($bytes.Length - 2)])
    }
}

function Require-Text {
    param([string]$Path, [string]$Pattern, [string]$Label)
    & rg -q $Pattern $Path
    if ($LASTEXITCODE -ne 0) {
        throw "$Label missing in $Path"
    }
}

function Invoke-SlangCompile {
    param(
        [string]$Entry,
        [string]$Target,
        [string]$Profile,
        [string]$Output,
        [string]$Reflection,
        [string[]]$Extra = @()
    )
    $arguments = @(
        $script:Source,
        "-I", $script:SourceDir,
        "-entry", $Entry,
        "-target", $Target,
        "-profile", $Profile,
        "-O3",
        "-warnings-as-errors", "all",
        "-reflection-json", $Reflection,
        "-o", $Output
    ) + $Extra
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $script:Slangc @arguments
    Require-Success "$Entry $Target compilation"
    $timer.Stop()
    return $timer.Elapsed.TotalMilliseconds
}

Push-Location $RepoRoot
try {
    $repoRootPath = [IO.Path]::GetFullPath($RepoRoot)
    $toolRootPath = [IO.Path]::GetFullPath($ToolRoot)
    if (-not $toolRootPath.StartsWith(
            $repoRootPath + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "ToolRoot must remain inside the repository"
    }
    $archivePath = Join-Path $toolRootPath $SlangArchive
    $slangRoot = Join-Path $toolRootPath "slang-$SlangVersion"
    $Slangc = Join-Path $slangRoot "bin/slangc.exe"
    if (-not (Test-Path $Slangc)) {
        if ($Offline) {
            throw "Pinned Slang $SlangVersion is unavailable in offline mode"
        }
        New-Item -ItemType Directory -Force $toolRootPath | Out-Null
        if (-not (Test-Path $archivePath)) {
            Invoke-WebRequest -Uri $SlangUrl -OutFile $archivePath
        }
        $archiveHash = (Get-FileHash $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($archiveHash -ne $SlangSha256) {
            throw "Pinned Slang archive SHA-256 mismatch"
        }
        Expand-Archive -LiteralPath $archivePath -DestinationPath $slangRoot
    }
    $archiveHash = (Get-FileHash $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($archiveHash -ne $SlangSha256) {
        throw "Pinned Slang archive SHA-256 mismatch"
    }

    $Ptxas = Get-Command ptxas.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty Source
    if (-not $Ptxas) {
        $Ptxas = Join-Path $env:CUDA_PATH "bin/ptxas.exe"
    }
    if (-not (Test-Path $Ptxas)) {
        throw "CUDA ptxas is required for the T.2 CUDA code-generation gate"
    }
    $Nvcc = Join-Path (Split-Path (Split-Path $Ptxas)) "bin/nvcc.exe"
    if (-not (Test-Path $Nvcc)) {
        throw "CUDA nvcc is required for the T.2 occupancy probe"
    }

    $SourceDir = [IO.Path]::GetFullPath("tests/portable_kernel")
    $Source = Join-Path $SourceDir "phase_t2_prototypes.slang"
    $outputPath = [IO.Path]::GetFullPath($OutputDir)
    if (-not $outputPath.StartsWith(
            $repoRootPath + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "OutputDir must remain inside the repository"
    }
    if (Test-Path $outputPath) {
        Remove-Item -LiteralPath $outputPath -Recurse -Force
    }
    New-Item -ItemType Directory -Force $outputPath | Out-Null

    $artifactRecords = [Collections.Generic.List[object]]::new()
    $registerRecords = [Collections.Generic.List[object]]::new()
    foreach ($entry in $Entries) {
        $targets = @(
            @{
                name = "cuda"
                target = "ptx"
                profile = "sm_6_6"
                extension = "ptx"
                extra = @()
            },
            @{
                name = "spirv"
                target = "spirv"
                profile = "glsl_460+spvGroupNonUniform+spvGroupNonUniformBallot"
                extension = "spv"
                extra = @("-emit-spirv-directly", "-fvk-use-dx-layout")
            },
            @{
                name = "dxil"
                target = "dxil"
                profile = "sm_6_6"
                extension = "dxil"
                extra = @()
            }
        )
        foreach ($target in $targets) {
            $artifact = Join-Path $outputPath "$entry.$($target.extension)"
            $reflection = "$artifact.reflection.json"
            $compileMs = Invoke-SlangCompile `
                $entry $target.target $target.profile `
                $artifact $reflection $target.extra
            if ($target.name -eq "cuda") {
                Remove-TrailingNull $artifact
            }
            $repeat = "$artifact.repeat"
            $repeatReflection = "$reflection.repeat"
            [void](Invoke-SlangCompile `
                $entry $target.target $target.profile `
                $repeat $repeatReflection $target.extra)
            if ($target.name -eq "cuda") {
                Remove-TrailingNull $repeat
            }
            $hash = (Get-FileHash $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
            $repeatHash = (Get-FileHash $repeat -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($hash -ne $repeatHash) {
                throw "$entry $($target.name) compilation is not reproducible"
            }
            $reflectionHash = (
                Get-FileHash $reflection -Algorithm SHA256).Hash.ToLowerInvariant()
            $repeatReflectionHash = (
                Get-FileHash $repeatReflection -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($reflectionHash -ne $repeatReflectionHash) {
                throw "$entry $($target.name) reflection is not reproducible"
            }
            Remove-Item $repeat, $repeatReflection

            $reflectionData = Get-Content $reflection -Raw | ConvertFrom-Json
            $params = $reflectionData.parameters |
                Where-Object { $_.name -eq "params" }
            if (-not $params) {
                throw "$entry $($target.name) reflection lacks params"
            }
            $fields = $params.type.elementType.fields
            $baseAddress = $fields |
                Where-Object { $_.name -eq "baseAddress" }
            if ($baseAddress.type.scalarType -ne "uint64" -or
                $baseAddress.binding.offset -ne 8) {
                throw "$entry $($target.name) reflection violates 64-bit layout"
            }
            $artifactRecords.Add([ordered]@{
                entry = $entry
                target = $target.name
                bytes = (Get-Item $artifact).Length
                sha256 = $hash
                compile_ms = [Math]::Round($compileMs, 3)
                reflection = [IO.Path]::GetFileName($reflection)
            })
        }

        $ptx = Join-Path $outputPath "$entry.ptx"
        $cubin = Join-Path $outputPath "$entry.cubin"
        $ptxasOutput = (& $Ptxas -arch=sm_120 -v $ptx -o $cubin 2>&1 | Out-String)
        Require-Success "$entry ptxas"
        $registerMatch = [regex]::Match($ptxasOutput, "Used ([0-9]+) registers")
        $spillStoreMatch = [regex]::Match($ptxasOutput, "([0-9]+) bytes spill stores")
        $spillLoadMatch = [regex]::Match($ptxasOutput, "([0-9]+) bytes spill loads")
        if (-not $registerMatch.Success) {
            throw "$entry ptxas output lacks register usage"
        }
        $registerRecords.Add([ordered]@{
            entry = $entry
            registers = [int]$registerMatch.Groups[1].Value
            spill_store_bytes = [int]$spillStoreMatch.Groups[1].Value
            spill_load_bytes = [int]$spillLoadMatch.Groups[1].Value
        })
    }

    $occupancySource = [IO.Path]::GetFullPath(
        "tools/phase_t2_cuda_occupancy.cu")
    $occupancyExe = Join-Path $outputPath "phase_t2_cuda_occupancy.exe"
    & $Nvcc -std=c++20 -O2 -Xcompiler=/wd4819 `
        $occupancySource -lcuda -o $occupancyExe
    Require-Success "CUDA occupancy probe build"
    $occupancyArguments = [Collections.Generic.List[string]]::new()
    foreach ($entry in $Entries) {
        $occupancyArguments.Add((Join-Path $outputPath "$entry.cubin"))
        $occupancyArguments.Add($entry)
    }
    $occupancyLines = & $occupancyExe @occupancyArguments
    Require-Success "CUDA occupancy probe"
    $occupancyRecords = [Collections.Generic.List[object]]::new()
    foreach ($line in $occupancyLines) {
        $columns = $line -split ","
        if ($columns.Count -ne 6) {
            throw "CUDA occupancy probe emitted malformed output"
        }
        $occupancy = [double]::Parse(
            $columns[4],
            [Globalization.CultureInfo]::InvariantCulture)
        if ($occupancy -lt 0.5) {
            throw "$($columns[0]) CUDA occupancy is below 50 percent"
        }
        if ($columns[5] -ne "1") {
            throw "$($columns[0]) CUDA numerical validation failed"
        }
        $occupancyRecords.Add([ordered]@{
            entry = $columns[0]
            registers = [int]$columns[1]
            static_shared_bytes = [int]$columns[2]
            active_blocks_per_sm = [int]$columns[3]
            occupancy = $occupancy
            numerically_validated = $true
        })
    }

    $queuePtx = Join-Path $outputPath "queue_compaction.ptx"
    Require-Text $queuePtx "atom\.global\.add\.u32" "CUDA atomic lowering"
    Require-Text $queuePtx "(vote|activemask|popc)" "CUDA subgroup lowering"
    Require-Text $queuePtx "\.address_size 64" "CUDA 64-bit addressing"

    $spirvAsm = Join-Path $outputPath "queue_compaction.spvasm"
    & $Slangc $Source -I $SourceDir -entry queue_compaction `
        -target spirv-asm `
        -profile "glsl_460+spvGroupNonUniform+spvGroupNonUniformBallot" `
        -emit-spirv-directly -fvk-use-dx-layout -O3 -g2 `
        -warnings-as-errors all -o $spirvAsm
    Require-Success "SPIR-V debug assembly"
    Require-Text $spirvAsm "OpCapability Int64" "SPIR-V Int64 capability"
    Require-Text $spirvAsm "OpCapability GroupNonUniform" "SPIR-V subgroup capability"
    Require-Text $spirvAsm "OpAtomicIAdd" "SPIR-V atomic lowering"
    Require-Text $spirvAsm "DebugLine" "SPIR-V source mapping"

    $spectralSpirvAsm = Join-Path $outputPath "spectral_conversion.spvasm"
    & $Slangc $Source -I $SourceDir -entry spectral_conversion `
        -target spirv-asm -profile "glsl_460" `
        -emit-spirv-directly -fvk-use-dx-layout -O3 `
        -warnings-as-errors all -o $spectralSpirvAsm
    Require-Success "SPIR-V specialization assembly"
    Require-Text $spectralSpirvAsm "OpSpecConstant" "SPIR-V specialization constant"
    Require-Text $spectralSpirvAsm "OpMemberDecorate.*2 Offset 8" "SPIR-V 64-bit layout"

    $dxilAsm = Join-Path $outputPath "queue_compaction.dxilasm"
    & $Slangc $Source -I $SourceDir -entry queue_compaction `
        -target dxil-asm -profile sm_6_6 -O3 -g2 `
        -warnings-as-errors all -o $dxilAsm
    Require-Success "DXIL debug assembly"
    Remove-TrailingNull $dxilAsm
    Require-Text $dxilAsm "Wave level operations" "DXIL subgroup declaration"
    Require-Text $dxilAsm "64-Bit integer" "DXIL Int64 declaration"
    Require-Text $dxilAsm "atomicBinOp" "DXIL atomic lowering"
    Require-Text $dxilAsm "DIFile" "DXIL source mapping"

    $debugPtx = Join-Path $outputPath "queue_compaction.debug.ptx"
    & $Slangc $Source -I $SourceDir -entry queue_compaction `
        -target ptx -profile sm_6_6 -O3 -g2 `
        -warnings-as-errors all -o $debugPtx
    Require-Success "CUDA debug PTX"
    Remove-TrailingNull $debugPtx
    Require-Text $debugPtx "\.loc" "CUDA source mapping"

    $slangVersionText = (& $Slangc -version 2>&1 | Out-String).Trim()
    $ptxasVersionText = (& $Ptxas --version | Out-String).Trim()
    $report = [ordered]@{
        schema = "ure.phase_t.kernel_toolchain_feasibility.v1"
        decision = "slang_single_source_multi_target"
        slang_version = $slangVersionText
        slang_archive_sha256 = $SlangSha256
        ptxas_version = $ptxasVersionText
        source_sha256 = (Get-FileHash $Source -Algorithm SHA256).Hash.ToLowerInvariant()
        semantics_sha256 = (
            Get-FileHash (Join-Path $SourceDir "phase_t2_semantics.slang") `
                -Algorithm SHA256).Hash.ToLowerInvariant()
        entries = $Entries
        targets = @("cuda_ptx_sm120", "spirv_1_3", "dxil_sm_6_6")
        artifacts = $artifactRecords
        cuda_resources = $registerRecords
        cuda_occupancy = $occupancyRecords
        contracts = [ordered]@{
            deterministic_offline_compile = $true
            reflection_layout = $true
            address_bits = 64
            subgroup = $true
            atomics = $true
            specialization = $true
            debug_source_mapping = $true
            cuda_numerical_execution = $true
            warnings_as_errors = $true
        }
    }
    $report |
        ConvertTo-Json -Depth 8 |
        Set-Content (Join-Path $outputPath "phase_t2_report.json") -Encoding utf8
    Write-Host "Phase T.2 kernel toolchain gate passed: $outputPath"
} finally {
    Pop-Location
}
