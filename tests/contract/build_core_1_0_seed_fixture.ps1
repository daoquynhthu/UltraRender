param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = "Stop"
$baseline = "0de7e5cad21d4f372d5254c18bbbdfdbd6e1c408"
$root = Join-Path $RepoRoot "tests/fixtures/contracts/old_clients/core_1_0_seed_from_pb7/windows_x64"
$source = Join-Path $root "core_1_0_seed_client.c"
$binary = Join-Path $root "core_1_0_seed_client.exe"
$temp = Join-Path $env:TEMP "ultrarender_core_1_0_seed"
$vswhere = "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere.exe is required"
}
$install = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
$devcmd = Join-Path $install "Common7/Tools/VsDevCmd.bat"
$resolvedTemp = [IO.Path]::GetFullPath($temp)
if (Test-Path -LiteralPath $resolvedTemp) {
    if ([IO.Path]::GetFileName($resolvedTemp) -ne "ultrarender_core_1_0_seed") {
        throw "Refusing to remove unexpected seed build directory"
    }
    Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
}
$include = Join-Path $resolvedTemp "include/ultrarender"
New-Item -ItemType Directory -Force -Path $include | Out-Null
$loader = (& git -C $RepoRoot show "$baseline`:contracts/generated/include/ultrarender/ure_loader.h") -join "`n"
$registry = (& git -C $RepoRoot show "$baseline`:contracts/generated/include/ultrarender/ure_registry.h") -join "`n"
[IO.File]::WriteAllText((Join-Path $include "ure_loader.h"), $loader + "`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $include "ure_registry.h"), $registry + "`n", [Text.UTF8Encoding]::new($false))
$object = Join-Path $resolvedTemp "core_1_0_seed_client.obj"
$command = "`"$devcmd`" -arch=amd64 -host_arch=amd64 >nul && cl /nologo /TC /std:c11 /W4 /WX /O2 /Brepro /I`"$(Split-Path $include -Parent)`" /Fo`"$object`" /Fe`"$binary`" `"$source`" /link /INCREMENTAL:NO"
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Failed to compile Core 1.0 PB.7-layout seed"
}
$compiler = (& cmd.exe /d /s /c "`"$devcmd`" -arch=amd64 -host_arch=amd64 >nul && cl 2>&1" | Select-Object -First 1).Trim()
$sdkVersion = (Get-ChildItem -LiteralPath "C:/Program Files (x86)/Windows Kits/10/Include" -Directory |
    Where-Object Name -Match '^10\.0\.\d+\.\d+$' |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1).Name
$sha = [Security.Cryptography.SHA256]::Create()
$sdkDigest = ([BitConverter]::ToString($sha.ComputeHash(
    [Text.Encoding]::UTF8.GetBytes($loader + "`n" + $registry + "`n")))).Replace("-", "").ToLowerInvariant()
$manifest = [ordered]@{
    schema = "ure.pb.core-client-seed/1.0"
    publication_state = "StableSeed"
    core_abi = "1.0"
    source_layout = "Final PB.7 generated table and value prefixes"
    baseline_commit = $baseline
    platform = "windows-x64-msvc-c11"
    source = "core_1_0_seed_client.c"
    source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant()
    binary = "core_1_0_seed_client.exe"
    binary_bytes = (Get-Item -LiteralPath $binary).Length
    binary_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $binary).Hash.ToLowerInvariant()
    sdk_header_sha256 = $sdkDigest
    compiler = $compiler
    windows_sdk = $sdkVersion
    compile_flags = @("/TC", "/std:c11", "/W4", "/WX", "/O2", "/Brepro", "/INCREMENTAL:NO")
    expected_behavior = "The final PB.7 ABI prefixes negotiate Core 1.0 and load every frozen table without recompilation."
    compatibility_promise = "Retained as the oldest Core 1.x client seed for every future runtime_1 build."
}
$manifestJson = (($manifest | ConvertTo-Json -Depth 5) -replace "`r`n", "`n") + "`n"
[IO.File]::WriteAllText((Join-Path $root "manifest.json"), $manifestJson, [Text.UTF8Encoding]::new($false))
Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
