param(
    [Parameter(Mandatory = $true)][string]$Cli,
    [Parameter(Mandatory = $true)][string]$Runtime,
    [Parameter(Mandatory = $true)][string]$Worker,
    [Parameter(Mandatory = $true)][string]$Q3,
    [Parameter(Mandatory = $true)][string]$Gltf
)

$ErrorActionPreference = "Stop"
$root = Join-Path ([System.IO.Path]::GetTempPath()) (
    "ultrarender-prv3-cli-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $root | Out-Null

function Invoke-Tool([string[]]$Arguments, [string]$Transport) {
    $all = $Arguments + @(
        "--transport", $Transport,
        "--runtime", $Runtime,
        "--worker", $Worker)
    $text = & $Cli @all 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "CLI material command failed: $($all -join ' ')`n$text"
    }
    return [pscustomobject]@{
        Text = $text
        Report = ($text | ConvertFrom-Json -Depth 100)
    }
}

function Assert-Parity([object]$Direct, [object]$WorkerResult, [string]$Name) {
    $left = $Direct.Report | ConvertTo-Json -Depth 100 -Compress
    $right = $WorkerResult.Report | ConvertTo-Json -Depth 100 -Compress
    if ($left -ne $right -or -not $Direct.Report.ok) {
        throw "CLI Direct/Worker material parity failed: $Name"
    }
}

function Assert-Failure(
    [string[]]$Arguments,
    [string]$Transport,
    [uint32]$Detail,
    [string]$Name) {
    $all = $Arguments + @(
        "--transport", $Transport,
        "--runtime", $Runtime,
        "--worker", $Worker)
    $text = & $Cli @all 2>&1 | Out-String
    if ($LASTEXITCODE -ne 1 -or
        $text -notmatch ('"detail": {0}' -f $Detail) -or
        $text -notmatch "detail=$Detail" -or
        $text.Contains($root)) {
        throw "CLI $Name failure is unclassified or unredacted: $Transport`n$text"
    }
}

try {
    $directAuthor = Join-Path $root "direct-author"
    $workerAuthor = Join-Path $root "worker-author"
    Copy-Item -LiteralPath $Q3 -Destination $directAuthor -Recurse
    Copy-Item -LiteralPath $Q3 -Destination $workerAuthor -Recurse
    $directScene = Join-Path $directAuthor "full_scene.urescene"
    $workerScene = Join-Path $workerAuthor "full_scene.urescene"
    $selector = "material/00000000"

    $directPreset = Join-Path $root "preset-direct.urescene"
    $workerPreset = Join-Path $root "preset-worker.urescene"
    $presetDirect = Invoke-Tool @(
        "material-preset", $directScene, "-o", $directPreset,
        "--material", $selector, "--preset", "clear_glass") "direct"
    $presetWorker = Invoke-Tool @(
        "material-preset", $workerScene, "-o", $workerPreset,
        "--material", $selector, "--preset", "clear_glass") "worker"
    Assert-Parity $presetDirect $presetWorker "preset"
    if ($presetDirect.Report.inventory.material_programs -lt 1 -or
        $presetDirect.Report.material_program_set_identity -notmatch '^[0-9a-f]{64}$') {
        throw "CLI preset omitted canonical material program evidence"
    }

    $directMtlx = Join-Path $root "direct.mtlx"
    $workerMtlx = Join-Path $root "worker.mtlx"
    Assert-Parity `
        (Invoke-Tool @(
            "material-export", $directPreset, "-o", $directMtlx,
            "--material", $selector) "direct") `
        (Invoke-Tool @(
            "material-export", $workerPreset, "-o", $workerMtlx,
            "--material", $selector) "worker") "MaterialX export"

    Assert-Parity `
        (Invoke-Tool @(
            "material-import", $directScene, $directMtlx,
            "-o", (Join-Path $root "import-direct.urescene"),
            "--material", $selector) "direct") `
        (Invoke-Tool @(
            "material-import", $workerScene, $workerMtlx,
            "-o", (Join-Path $root "import-worker.urescene"),
            "--material", $selector) "worker") "MaterialX import"

    Assert-Parity `
        (Invoke-Tool @(
            "build", $Gltf, "-o", (Join-Path $root "gltf-direct.urescene")) "direct") `
        (Invoke-Tool @(
            "build", $Gltf, "-o", (Join-Path $root "gltf-worker.urescene")) "worker") "glTF build"

    foreach ($transport in @("direct", "worker")) {
        Assert-Failure @(
            "material-preset", $directScene,
            "-o", (Join-Path $root "invalid-$transport.urescene"),
            "--material", $selector, "--preset", "missing-preset") `
            $transport 706 "unknown preset"
    }

    Write-Output "PRV.3 CLI material authoring and diagnostic parity gate passed"
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
