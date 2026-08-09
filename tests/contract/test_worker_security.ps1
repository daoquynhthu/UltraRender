param(
    [Parameter(Mandatory = $true)][string]$WorkerExecutable,
    [Parameter(Mandatory = $true)][string]$ConformanceExecutable,
    [Parameter(Mandatory = $true)][string]$ConformanceRuntime,
    [Parameter(Mandatory = $true)][string]$RuntimeStage
)

$ErrorActionPreference = "Stop"

function Find-Dumpbin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($root in @("D:/Microsoft Visual Studio/VC/Tools/MSVC", "C:/Program Files/Microsoft Visual Studio")) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }
        $candidate = Get-ChildItem -LiteralPath $root -Filter dumpbin.exe -File -Recurse |
            Where-Object { $_.FullName -match 'Hostx64[\\/]x64' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }
    throw "MSVC dumpbin.exe is required"
}

$dumpbin = Find-Dumpbin
foreach ($executable in @($WorkerExecutable, $ConformanceExecutable)) {
    $dependencies = @(& $dumpbin /nologo /dependents $executable 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin dependency inspection failed for $executable"
    }
    $dependencyText = $dependencies -join "`n"
    if ($dependencyText -match '(?i)(ws2_32|wininet|winhttp|urlmon|ultrarender_runtime_candidate)\.dll') {
        throw "Worker acquired a network stack or linked the product runtime: $executable"
    }
}

$productName = Split-Path -Leaf $WorkerExecutable
$conformanceName = Split-Path -Leaf $ConformanceExecutable
$conformanceRuntimeName = Split-Path -Leaf $ConformanceRuntime
if (-not (Test-Path -LiteralPath (Join-Path $RuntimeStage "bin/$productName") -PathType Leaf)) {
    throw "Product worker is absent from the candidate runtime stage"
}
if (Test-Path -LiteralPath (Join-Path $RuntimeStage "bin/$conformanceName") -PathType Leaf) {
    throw "Conformance-only worker was included in the candidate runtime stage"
}
if (Test-Path -LiteralPath (Join-Path $RuntimeStage "bin/$conformanceRuntimeName") -PathType Leaf) {
    throw "Conformance-only runtime was included in the candidate runtime stage"
}

Write-Output "Worker imports and package boundary are local-only"
