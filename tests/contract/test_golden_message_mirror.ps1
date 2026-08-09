param(
    [Parameter(Mandatory = $true)][string]$Generated,
    [Parameter(Mandatory = $true)][string]$Fixture
)

$ErrorActionPreference = "Stop"
$generatedFiles = @(
    Get-ChildItem -LiteralPath $Generated -File | ForEach-Object {
        [pscustomobject]@{Name = $_.Name; Hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    } | Sort-Object Name)
$fixtureFiles = @(
    Get-ChildItem -LiteralPath $Fixture -File | ForEach-Object {
        [pscustomobject]@{Name = $_.Name; Hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    } | Sort-Object Name)
if (($generatedFiles | ConvertTo-Json -Compress) -ne ($fixtureFiles | ConvertTo-Json -Compress)) {
    throw "Golden message fixture mirror drifted from the generated package"
}
