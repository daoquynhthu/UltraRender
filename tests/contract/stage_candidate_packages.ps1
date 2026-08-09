param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$SdkStage,
    [Parameter(Mandatory = $true)][string]$RuntimeStage,
    [Parameter(Mandatory = $true)][string]$RuntimeDll,
    [Parameter(Mandatory = $true)][string]$WorkerExecutable,
    [Parameter(Mandatory = $true)][string]$MockWorkerExecutable
)

$ErrorActionPreference = "Stop"

function Reset-Directory([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Path | Out-Null
}

function Copy-Tree([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
}

function Write-PackageManifest([string]$Root, [string]$Kind) {
    $files = Get-ChildItem -LiteralPath $Root -File -Recurse |
        Where-Object { $_.Name -ne "package_manifest.json" } |
        Sort-Object { $_.FullName.Substring($Root.Length).Replace('\', '/') }
    $entries = foreach ($file in $files) {
        [ordered]@{
            path = $file.FullName.Substring($Root.Length).TrimStart('\').Replace('\', '/')
            bytes = $file.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
        }
    }
    $manifest = [ordered]@{
        schema = "ure.pb.candidate-package/1.0"
        publication_state = "Candidate"
        candidate_version = "0.1.0"
        compatibility_promise = "None before PB.8"
        package_kind = $Kind
        platform = "windows-x64-msvc-c11"
        files = @($entries)
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Root "package_manifest.json") -Encoding utf8NoBOM
}

Reset-Directory $SdkStage
Reset-Directory $RuntimeStage

Copy-Tree (Join-Path $RepoRoot "contracts/generated/include") (Join-Path $SdkStage "include")
Copy-Tree (Join-Path $RepoRoot "contracts/generated/schemas") (Join-Path $SdkStage "share/ultrarender/schemas")
Copy-Tree (Join-Path $RepoRoot "contracts/generated/registry") (Join-Path $SdkStage "share/ultrarender/registry")
Copy-Tree (Join-Path $RepoRoot "contracts/generated/golden_messages") (Join-Path $SdkStage "share/ultrarender/golden_messages")
Copy-Tree (Join-Path $RepoRoot "contracts/reports") (Join-Path $SdkStage "share/ultrarender/reports")
Copy-Tree (Join-Path $RepoRoot "third_party/flatbuffers/include") (Join-Path $SdkStage "third_party/flatbuffers/include")
New-Item -ItemType Directory -Force -Path (Join-Path $SdkStage "third_party/flatbuffers") | Out-Null
Copy-Item -LiteralPath (Join-Path $RepoRoot "third_party/flatbuffers/LICENSE.txt") -Destination (Join-Path $SdkStage "third_party/flatbuffers/LICENSE.txt")
New-Item -ItemType Directory -Force -Path (Join-Path $SdkStage "bin"), (Join-Path $SdkStage "share/ultrarender/docs") | Out-Null
Copy-Item -LiteralPath $MockWorkerExecutable -Destination (Join-Path $SdkStage "bin")
Copy-Item -LiteralPath (Join-Path $RepoRoot "contracts/generated/mock_scenarios.json") -Destination (Join-Path $SdkStage "share/ultrarender")
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs/Public_API_Candidate_Integration.md") -Destination (Join-Path $SdkStage "share/ultrarender/docs")

$fixtureRoot = Join-Path $SdkStage "share/ultrarender/fixtures"
foreach ($name in @("q4_procedural_scene", "pb5_public_boundary", "pb6_scene_transaction_full")) {
    Copy-Tree (Join-Path $RepoRoot "tests/assets/native_scene/$name") (Join-Path $fixtureRoot $name)
}

New-Item -ItemType Directory -Force -Path (Join-Path $RuntimeStage "bin"), (Join-Path $RuntimeStage "share/ultrarender/abi"), (Join-Path $RuntimeStage "share/ultrarender/registry"), (Join-Path $RuntimeStage "share/ultrarender/docs"), (Join-Path $RuntimeStage "share/licenses/flatbuffers") | Out-Null
Copy-Item -LiteralPath $RuntimeDll -Destination (Join-Path $RuntimeStage "bin")
Copy-Item -LiteralPath $WorkerExecutable -Destination (Join-Path $RuntimeStage "bin")
Copy-Item -LiteralPath (Join-Path $RepoRoot "contracts/abi/windows_x64_candidate.json") -Destination (Join-Path $RuntimeStage "share/ultrarender/abi")
Copy-Item -LiteralPath (Join-Path $RepoRoot "contracts/generated/runtime_manifest_candidate.json") -Destination (Join-Path $RuntimeStage "share/ultrarender")
Copy-Item -LiteralPath (Join-Path $RepoRoot "contracts/generated/registry/public_contract_registry.canonical.json") -Destination (Join-Path $RuntimeStage "share/ultrarender/registry")
Copy-Tree (Join-Path $RepoRoot "contracts/generated/schemas") (Join-Path $RuntimeStage "share/ultrarender/schemas")
Copy-Tree (Join-Path $RepoRoot "contracts/reports") (Join-Path $RuntimeStage "share/ultrarender/reports")
Copy-Item -LiteralPath (Join-Path $RepoRoot "third_party/flatbuffers/LICENSE.txt") -Destination (Join-Path $RuntimeStage "share/licenses/flatbuffers/LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs/PB7_Compatibility_Report.md") -Destination (Join-Path $RuntimeStage "share/ultrarender/docs")

Write-PackageManifest $SdkStage "SDK"
Write-PackageManifest $RuntimeStage "Runtime"
