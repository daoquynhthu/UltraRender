$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Assert-NoMatch {
    param([string[]]$Paths, [string]$Pattern, [string]$Label)
    $matches = & rg -n --glob "*.h" --glob "*.hpp" --glob "*.cpp" --glob "*.py" $Pattern @Paths 2>$null
    if ($LASTEXITCODE -eq 0) {
        $matches | Write-Host
        throw "$Label"
    }
    if ($LASTEXITCODE -ne 1) { throw "rg failed while checking $Label" }
}

function Assert-Contains {
    param([string]$Path, [string]$Pattern, [string]$Label)
    & rg -q $Pattern (Join-Path $RepoRoot $Path)
    if ($LASTEXITCODE -ne 0) { throw $Label }
}

Push-Location $RepoRoot
try {
    $neutralRoots = @(
        "libs/ure_types",
        "libs/ure_sceneio",
        "libs/ure_config",
        "pyure"
    )
    Assert-NoMatch $neutralRoots '#include[[:space:]]*[<"]cuda|cuda(TextureObject|Array|Stream|Event|Error)_t|CU(deviceptr|context|stream|event)' "backend-neutral modules expose CUDA SDK types"
    Assert-NoMatch @(
        "libs/ure_types/include/ure/render_config.hpp",
        "libs/ure_types/include/ure/scene_ir.hpp",
        "libs/ure_core/include/ure/ure_c_api.h"
    ) 'cuda_runtime|cuda(TextureObject|Array|Stream|Event|Error)_t|CU(deviceptr|context|stream|event)' "public configuration, SceneIR, or C ABI exposes CUDA SDK types"
    Assert-NoMatch @("libs/ure_core/include/ure/ure_c_api.h") 'Gpu(Context|Scene|MaterialData|Texture)' "C ABI exposes CUDA-era implementation structs"

    $allowedCudaHeaders = @(
        "libs/ure_core/include/ure/gpu_context.hpp",
        "libs/ure_core/include/ure/gpu_hardware.hpp",
        "libs/ure_core/include/ure/gpu_structs.hpp",
        "libs/ure_diag/include/ure/check_cuda.hpp"
    )
    $actualCudaHeaders = @(
        & rg -l '#include[[:space:]]*[<"]cuda_runtime\.h' libs -g "*.h" -g "*.hpp" |
            ForEach-Object { $_ -replace '\\', '/' } |
            Sort-Object
    )
    $expectedCudaHeaders = @($allowedCudaHeaders | Sort-Object)
    if (($actualCudaHeaders -join "`n") -ne ($expectedCudaHeaders -join "`n")) {
        "Expected public CUDA include allowlist:" | Write-Host
        $expectedCudaHeaders | Write-Host
        "Actual public CUDA include set:" | Write-Host
        $actualCudaHeaders | Write-Host
        throw "public CUDA include allowlist changed"
    }

    $ledger = "docs/Phase_T_Portable_GPU_Runtime.md"
    foreach ($id in @(
        "T0-BLD", "T0-DEV", "T0-API", "T0-ABI", "T0-CTX",
        "T0-RES", "T0-EXE", "T0-KRN", "T0-MGPU", "T0-WAVE",
        "T0-ACC", "T0-DIAG", "T0-SCN", "T0-TEST"
    )) {
        Assert-Contains $ledger $id "Phase T coupling ledger is missing $id"
    }
    Assert-Contains $ledger "Contract owner" "Phase T ledger lacks contract ownership"
    Assert-Contains $ledger "Migration batch" "Phase T ledger lacks migration batches"
    Assert-Contains "PLAN.md" "当前游标: T\.1" "PLAN cursor did not advance to T.1"
    Assert-Contains "PLAN.md" "T\.0 已冻结.*当前游标为 T\.1" "PLAN lacks the T.0 closure and T.1 gate"
    Write-Host "Phase T static audit passed"
} finally {
    Pop-Location
}
