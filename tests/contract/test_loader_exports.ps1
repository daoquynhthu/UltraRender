param(
    [Parameter(Mandatory = $true)][string]$RuntimeDll,
    [Parameter(Mandatory = $true)][string]$ClientExecutable
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
$output = @(& $dumpbin /nologo /exports $RuntimeDll 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin export inspection failed"
}
$exports = @(
    $output |
        ForEach-Object {
            if ($_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$') {
                $Matches[1]
            }
        } |
        Sort-Object
)
$expected = @("ureGetRuntimeManifest", "ureQueryInterface") | Sort-Object
if ($exports.Count -ne 2 -or (Compare-Object $exports $expected)) {
    throw "Candidate runtime exports are not exactly the two loader symbols: $($exports -join ', ')"
}
$runtimeDependencies = @(& $dumpbin /nologo /dependents $RuntimeDll 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin runtime dependency inspection failed"
}
if (($runtimeDependencies -join "`n") -match '(?i)pyure_native\.dll') {
    throw "candidate runtime acquired a legacy product dependency"
}
$dependencies = @(& $dumpbin /nologo /dependents $ClientExecutable 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin dependency inspection failed"
}
if (($dependencies -join "`n") -match '(?i)ultrarender_runtime_candidate\.dll') {
    throw "External loader client imported the candidate runtime instead of using LoadLibraryW"
}
Write-Output "Candidate runtime exports exactly two undecorated loader symbols"
