param(
    [Parameter(Mandatory = $true)][string]$Cli,
    [Parameter(Mandatory = $true)][string]$Runtime,
    [Parameter(Mandatory = $true)][string]$Worker,
    [Parameter(Mandatory = $true)][string]$Q3,
    [Parameter(Mandatory = $true)][string]$Q4
)

$ErrorActionPreference = "Stop"
$root = Join-Path ([System.IO.Path]::GetTempPath()) (
    "ultrarender-prv2-cli-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $root | Out-Null

function Invoke-Tool([string[]]$Arguments, [string]$Transport) {
    $all = $Arguments + @(
        "--transport", $Transport,
        "--runtime", $Runtime,
        "--worker", $Worker)
    $text = & $Cli @all 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "CLI scene-tool command failed: $($all -join ' ')`n$text"
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
        throw "CLI Direct/Worker scene-tool parity failed: $Name"
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
        throw "CLI $Name failure is unclassified or unredacted: $transport`n$text"
    }
}

try {
    $q3Direct = Join-Path $root "q3-direct"
    $q3Worker = Join-Path $root "q3-worker"
    Copy-Item -LiteralPath $Q3 -Destination $q3Direct -Recurse
    Copy-Item -LiteralPath $Q3 -Destination $q3Worker -Recurse
    $q4Scene = Join-Path $Q4 "procedural_scene.urescene"

    Assert-Parity `
        (Invoke-Tool @("info", $q4Scene) "direct") `
        (Invoke-Tool @("info", $q4Scene) "worker") "info"
    Assert-Parity `
        (Invoke-Tool @("inspect", $q4Scene) "direct") `
        (Invoke-Tool @("inspect", $q4Scene) "worker") "inspect"
    $realizeDirect = Invoke-Tool @("realize", $q4Scene) "direct"
    $realizeWorker = Invoke-Tool @("realize", $q4Scene) "worker"
    Assert-Parity $realizeDirect $realizeWorker "realize"
    if ($realizeDirect.Text -notmatch '"ure.scene.procedural"' -or
        $realizeDirect.Text -notmatch '"Executed"') {
        throw "CLI realization omitted the procedural disposition"
    }

    $directBuild = Join-Path $root "direct-build.urescene"
    $workerBuild = Join-Path $root "worker-build.urescene"
    Assert-Parity `
        (Invoke-Tool @("build", $q4Scene, "-o", $directBuild) "direct") `
        (Invoke-Tool @("build", $q4Scene, "-o", $workerBuild) "worker") "build"

    $directMigrate = Join-Path $q3Direct "migrated.urescene"
    $workerMigrate = Join-Path $q3Worker "migrated.urescene"
    Assert-Parity `
        (Invoke-Tool @("migrate", (Join-Path $q3Direct "full_scene.urescene"), "-o", $directMigrate) "direct") `
        (Invoke-Tool @("migrate", (Join-Path $q3Worker "full_scene.urescene"), "-o", $workerMigrate) "worker") "migrate"

    $directPackage = Join-Path $root "direct.urepkg"
    $workerPackage = Join-Path $root "worker.urepkg"
    Assert-Parity `
        (Invoke-Tool @("pack", $directMigrate, "-o", $directPackage) "direct") `
        (Invoke-Tool @("pack", $workerMigrate, "-o", $workerPackage) "worker") "pack"
    Remove-Item -LiteralPath $q3Direct, $q3Worker -Recurse -Force

    Assert-Parity `
        (Invoke-Tool @("validate", $directPackage) "direct") `
        (Invoke-Tool @("validate", $workerPackage) "worker") "validate"
    Assert-Parity `
        (Invoke-Tool @("unpack", $directPackage, "-o", (Join-Path $root "unpack-direct")) "direct") `
        (Invoke-Tool @("unpack", $workerPackage, "-o", (Join-Path $root "unpack-worker")) "worker") "unpack"

    foreach ($transport in @("direct", "worker")) {
        $missing = Join-Path $root "private-$transport" "missing.urescene"
        Assert-Failure @("validate", $missing) $transport 620 "missing input"

        $ambiguous = Join-Path (Split-Path -Parent $Q3) `
            "pb5_public_boundary/ambiguous_scenes.urepkg"
        Assert-Failure @("validate", $ambiguous) $transport 618 `
            "ambiguous package"
    }

    $unsupportedRoot = Join-Path $root "unsupported-author"
    Copy-Item -LiteralPath $Q4 -Destination $unsupportedRoot -Recurse
    $unsupportedPath = Join-Path $unsupportedRoot "unsupported.ure"
    $unsupported = Get-Content -Raw -LiteralPath `
        (Join-Path $Q4 "procedural_scene.ure") | ConvertFrom-Json -Depth 100
    $unsupported.document.features = @([ordered]@{
        dependencies = @()
        minimum_version = [ordered]@{ major = 1; minor = 0 }
        name = "ure.future.unsupported"
        parameters = [ordered]@{}
        provider = "ure"
        requirement = "required"
    })
    [System.IO.File]::WriteAllText(
        $unsupportedPath,
        ($unsupported | ConvertTo-Json -Depth 100) + "`n",
        [System.Text.UTF8Encoding]::new($false))

    $missingResourceRoot = Join-Path $root "missing-resource-author"
    Copy-Item -LiteralPath $Q3 -Destination $missingResourceRoot -Recurse
    Remove-Item -LiteralPath `
        (Join-Path $missingResourceRoot "textures/albedo.ppm") -Force
    foreach ($transport in @("direct", "worker")) {
        Assert-Failure @("validate", $unsupportedPath) $transport 601 `
            "unsupported feature"
        Assert-Failure @(
            "validate", (Join-Path $missingResourceRoot "full_scene.urescene")) `
            $transport 608 "missing resource"
    }

    Write-Output "PRV.2 CLI scene-tool Direct/Worker operation and negative-diagnostic gate passed"
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
