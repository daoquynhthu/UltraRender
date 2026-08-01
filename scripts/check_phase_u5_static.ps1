$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing U.5 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing U.5 contract '$pattern' in $Path"
        }
    }
}

function Reject-Text {
    param(
        [string[]]$Paths,
        [string[]]$Patterns
    )
    foreach ($path in $Paths) {
        $text = Get-Content -Raw -LiteralPath (
            Join-Path $root $path)
        foreach ($pattern in $Patterns) {
            if ($text.Contains($pattern)) {
                throw "Forbidden U.5 dependency '$pattern' in $path"
            }
        }
    }
}

Require-Text "libs/ure_hydra/src/render_delegate.cpp" @(
    "HdPrimTypeTokens->camera",
    "HdPrimTypeTokens->renderBuffer",
    "HdURERenderPass",
    "GetDefaultAovDescriptor",
    "renderReady"
)
Require-Text "libs/ure_hydra/src/render_pass.cpp" @(
    "RenderSession::create",
    "render_pass()",
    "update_camera",
    "GetAovBindings",
    "RecordRenderProgress",
    "RecordRenderError"
)
Require-Text "libs/ure_hydra/src/scene_snapshot.cpp" @(
    "BuildSceneSnapshot",
    "GetRootPaths",
    "GetExcludePaths",
    "GetInverse().GetTranspose()",
    "native traversal currently evaluates both triangle sides"
)
Require-Text "tests/hydra/test_hydra_progressive_render.cpp" @(
    "ure:maxSpp",
    "renderSpp",
    "camera mutation",
    "energy > 0.0f"
)
Require-Text "scripts/run_phase_u5_hydra_render_gate.ps1" @(
    "test_hydra_progressive_render",
    "--component Hydra",
    "PXR_PLUGINPATH_NAME"
)

Reject-Text @(
    "libs/ure_hydra/src/render_pass.cpp",
    "libs/ure_hydra/src/scene_snapshot.cpp",
    "libs/ure_hydra/src/render_buffer.cpp"
) @(
    "OpenGL",
    "glfw",
    "CreateWindow",
    "vkCreateInstance",
    "D3D12CreateDevice"
)

Write-Host "Phase U.5 static audit passed."
