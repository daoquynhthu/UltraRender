param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$baselines = @(
    @{ Phase = 2; Commit = "aaf8c1e"; Capabilities = @("Bootstrap") },
    @{ Phase = 3; Commit = "cdd0746"; Capabilities = @("Bootstrap", "Lifecycle") },
    @{ Phase = 4; Commit = "193f440"; Capabilities = @("Bootstrap", "Lifecycle", "FrameLease") },
    @{ Phase = 5; Commit = "d1b08b8"; Capabilities = @("Bootstrap", "Lifecycle", "FrameLease", "NativeScene", "RenderSession") },
    @{ Phase = 6; Commit = "5592aac"; Capabilities = @("Bootstrap", "Lifecycle", "FrameLease", "NativeScene", "RenderSession", "SceneTransaction") }
)

$vswhere = "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere.exe is required"
}
$install = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
$devcmd = Join-Path $install "Common7/Tools/VsDevCmd.bat"
$compilerLine = (& cmd.exe /d /s /c "`"$devcmd`" -arch=amd64 -host_arch=amd64 >nul && cl 2>&1" | Select-Object -First 1)
$sdkVersion = (Get-ChildItem -LiteralPath "C:/Program Files (x86)/Windows Kits/10/Include" -Directory |
    Where-Object Name -Match '^10\.0\.\d+\.\d+$' |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1).Name
if (-not $sdkVersion) {
    throw "Windows 10/11 SDK identity could not be determined"
}

$root = Join-Path $RepoRoot "tests/fixtures/contracts/old_clients"
$template = Join-Path $root "candidate_client.c"
$temp = Join-Path $env:TEMP "ultrarender_pb7_candidate_clients"
if (Test-Path -LiteralPath $temp) {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
New-Item -ItemType Directory -Path $temp | Out-Null

foreach ($baseline in $baselines) {
    $fullCommit = (& git -C $RepoRoot rev-parse $baseline.Commit).Trim()
    $directory = Join-Path $root "candidate_0_1_pb$($baseline.Phase)/windows_x64"
    $include = Join-Path $temp "pb$($baseline.Phase)/include/ultrarender"
    New-Item -ItemType Directory -Force -Path $directory, $include | Out-Null
    $loader = (& git -C $RepoRoot show "$fullCommit`:contracts/generated/include/ultrarender/ure_loader.h") -join "`n"
    $registry = (& git -C $RepoRoot show "$fullCommit`:contracts/generated/include/ultrarender/ure_registry.h") -join "`n"
    [IO.File]::WriteAllText((Join-Path $include "ure_loader.h"), $loader + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $include "ure_registry.h"), $registry + "`n", [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $template -Destination (Join-Path $directory "candidate_client.c") -Force
    $binary = Join-Path $directory "candidate_client.exe"
    $object = Join-Path $temp "pb$($baseline.Phase)/candidate_client.obj"
    $command = "`"$devcmd`" -arch=amd64 -host_arch=amd64 >nul && cl /nologo /TC /std:c11 /W4 /WX /O2 /Brepro /DURE_CLIENT_PHASE=$($baseline.Phase) /I`"$(Split-Path $include -Parent)`" /Fo`"$object`" /Fe`"$binary`" `"$template`" /link /INCREMENTAL:NO"
    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile PB.$($baseline.Phase) client fixture"
    }
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $directory "candidate_client.c")).Hash.ToLowerInvariant()
    $binaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $binary).Hash.ToLowerInvariant()
    $sdkHashInput = [Text.Encoding]::UTF8.GetBytes($loader + "`n" + $registry + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    $sdkHash = ([BitConverter]::ToString($sha.ComputeHash($sdkHashInput))).Replace("-", "").ToLowerInvariant()
    $manifest = [ordered]@{
        schema = "ure.pb.candidate-client-fixture/1.0"
        publication_state = "Candidate"
        candidate = "0.1"
        phase = "PB.$($baseline.Phase)"
        baseline_commit = $fullCommit
        platform = "windows-x64-msvc-c11"
        source = "candidate_client.c"
        source_sha256 = $sourceHash
        binary = "candidate_client.exe"
        binary_bytes = (Get-Item -LiteralPath $binary).Length
        binary_sha256 = $binaryHash
        sdk_header_sha256 = $sdkHash
        compiler = $compilerLine.Trim()
        windows_sdk = $sdkVersion
        compile_flags = @("/TC", "/std:c11", "/W4", "/WX", "/O2", "/Brepro", "/INCREMENTAL:NO")
        expected_capabilities = $baseline.Capabilities
        expected_behavior = "Bootstrap and phase-known interface prefixes succeed against the current candidate; unknown optional interface discovery is unavailable."
        compatibility_promise = "None before PB.8"
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $directory "manifest.json") -Encoding utf8NoBOM
}

Remove-Item -LiteralPath $temp -Recurse -Force
