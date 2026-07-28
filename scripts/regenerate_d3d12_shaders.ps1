param(
    [string]$OutputDir = "shaders/d3d12/generated",
    [string]$IntermediateDir = ".build/phase_t9_hlsl",
    [string]$SlangRoot = ".build/toolchains/slang-2026.14",
    [string]$Dxc = "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Require-Success {
    param([string]$Label)
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Normalize-ParameterGroup {
    param([string]$Text)
    $pattern =
        "(?s)struct\s+(?<type>(?:SLANG_ParameterGroup_)?\w+)" +
        "\s*\{(?<fields>.*?)\};\s*.*?cbuffer\s+(?<block>\w+)" +
        "\s*:\s*register\((?<register>b\d+)\)\s*\{\s*" +
        "\k<type>\s+(?<instance>\w+)\s*;\s*\}"
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) {
        return $Text
    }
    $replacement =
        "cbuffer $($match.Groups["block"].Value) : " +
        "register($($match.Groups["register"].Value))`r`n" +
        "{$($match.Groups["fields"].Value)`r`n}"
    $Text =
        $Text.Remove($match.Index, $match.Length).
            Insert($match.Index, $replacement)
    return $Text.Replace(
        $match.Groups["instance"].Value + ".",
        "")
}

function Normalize-Hlsl {
    param(
        [string]$Path,
        [string]$RootSignature,
        [hashtable]$RegisterMap
    )
    $resolved = Resolve-Path $Path
    $text = Normalize-ParameterGroup (
        [IO.File]::ReadAllText($resolved))
    foreach ($source in $RegisterMap.Keys) {
        $text = $text.Replace(
            "register($source)",
            "register(__ur_$source)")
    }
    foreach ($source in $RegisterMap.Keys) {
        $text = $text.Replace(
            "register(__ur_$source)",
            "register($($RegisterMap[$source]))")
    }
    $text = $text.Replace('[shader("compute")]', "")
    $text = [regex]::Replace(
        $text,
        "(?=\[numthreads\()",
        "[RootSignature(`"$RootSignature`")]`r`n",
        1)
    [IO.File]::WriteAllText(
        $resolved,
        $text,
        [Text.UTF8Encoding]::new($false))
}

Push-Location $RepoRoot
try {
    $Slangc = Join-Path $RepoRoot "$SlangRoot/bin/slangc.exe"
    if (-not (Test-Path -LiteralPath $Slangc)) {
        throw "Pinned Slang compiler is missing: $Slangc"
    }
    if (-not (Test-Path -LiteralPath $Dxc)) {
        throw "Pinned DXC compiler is missing: $Dxc"
    }
    New-Item -ItemType Directory -Force `
        $OutputDir, $IntermediateDir |
        Out-Null
    $OutputDir = [IO.Path]::GetFullPath($OutputDir)
    $IntermediateDir =
        [IO.Path]::GetFullPath($IntermediateDir)
    $foundationSource =
        Join-Path $RepoRoot "shaders/d3d12/phase_t9_foundation.slang"
    $accelerationSource =
        Join-Path $RepoRoot "shaders/vulkan/phase_t8_acceleration.slang"
    $entries = @(
        [ordered]@{
            name = "foundation"
            source = $foundationSource
            profile = "sm_6_0"
            root = "DescriptorTable(CBV(b0)), DescriptorTable(UAV(u0))"
            registers = @{}
        },
        [ordered]@{
            name = "image_write"
            source = $foundationSource
            profile = "sm_6_0"
            root = "DescriptorTable(UAV(u0))"
            registers = @{"u1" = "u0"}
        },
        [ordered]@{
            name = "image_sample"
            source = $foundationSource
            profile = "sm_6_0"
            root = "DescriptorTable(SRV(t0)), DescriptorTable(Sampler(s0)), DescriptorTable(UAV(u0))"
            registers = @{"u2" = "u0"}
        },
        [ordered]@{
            name = "compute_bvh"
            source = $accelerationSource
            profile = "sm_6_0"
            root = "DescriptorTable(CBV(b0)), DescriptorTable(SRV(t0)), DescriptorTable(SRV(t1)), DescriptorTable(SRV(t2)), DescriptorTable(SRV(t3)), DescriptorTable(SRV(t4)), DescriptorTable(SRV(t5)), DescriptorTable(SRV(t6)), DescriptorTable(UAV(u0)), DescriptorTable(UAV(u1))"
            registers = @{
                "t1" = "t0"
                "t2" = "t1"
                "t3" = "t2"
                "t4" = "t3"
                "t5" = "t4"
                "t6" = "t5"
                "t7" = "t6"
            }
        },
        [ordered]@{
            name = "ray_query_native"
            source = $accelerationSource
            profile = "sm_6_5"
            root = "DescriptorTable(SRV(t0)), DescriptorTable(CBV(b0)), DescriptorTable(SRV(t1)), DescriptorTable(SRV(t2)), DescriptorTable(SRV(t3)), DescriptorTable(SRV(t4)), DescriptorTable(SRV(t5)), DescriptorTable(SRV(t6)), DescriptorTable(SRV(t7)), DescriptorTable(UAV(u0)), DescriptorTable(UAV(u1))"
            registers = @{}
        }
    )
    foreach ($entry in $entries) {
        $hlsl = Join-Path $IntermediateDir "$($entry.name).hlsl"
        $reflection = Join-Path $OutputDir "$($entry.name).json"
        $dxil = Join-Path $OutputDir "$($entry.name).dxil"
        & $Slangc $entry.source `
            -I (Split-Path $entry.source) `
            -I (Join-Path $RepoRoot "shaders/shared") `
            -entry $entry.name `
            -target hlsl `
            -profile $entry.profile `
            -O3 `
            -warnings-as-errors all `
            -reflection-json $reflection `
            -o $hlsl
        Require-Success "$($entry.name) Slang HLSL compilation"
        Normalize-Hlsl `
            $hlsl $entry.root $entry.registers
        Push-Location $IntermediateDir
        try {
            & $Dxc "$($entry.name).hlsl" `
                -E $entry.name `
                -T "cs_$($entry.profile.Substring(3))" `
                -O3 `
                -WX `
                -Fo $dxil
            Require-Success "$($entry.name) DXIL compilation"
            & $Dxc "$($entry.name).hlsl" `
                -E $entry.name `
                -T "cs_$($entry.profile.Substring(3))" `
                -O3 `
                -Zi `
                -Qembed_debug `
                -Fd "$($entry.name).pdb" `
                -WX `
                -Fo "$($entry.name).debug.dxil"
            Require-Success "$($entry.name) debug DXIL compilation"
        } finally {
            Pop-Location
        }
    }
} finally {
    Pop-Location
}
