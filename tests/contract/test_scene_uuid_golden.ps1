param(
    [Parameter(Mandatory = $true)][string]$Golden,
    [Parameter(Mandatory = $true)][string]$Fixture,
    [Parameter(Mandatory = $true)][string]$AdditionalFixture
)

$ErrorActionPreference = 'Stop'
$goldenData = Get-Content -LiteralPath $Golden -Raw | ConvertFrom-Json
$fixtureData = Get-Content -LiteralPath $Fixture -Raw | ConvertFrom-Json
if ($goldenData.schema -ne 'ure.scene.uuid-golden/2.0' -or
    $goldenData.byte_order -ne 'RFC9562 network order' -or
    $goldenData.vectors.Count -lt 3) {
    throw 'UUID golden metadata is incomplete'
}
$objects = @{}
foreach ($mesh in $fixtureData.scene_ir.meshes) { $objects["mesh|$($mesh.id)"] = $mesh.uuid }
foreach ($instance in $fixtureData.scene_ir.instances) { $objects["instance|$($instance.id)"] = $instance.uuid }
$objects['camera|camera'] = $fixtureData.scene_ir.camera.uuid
foreach ($vector in $goldenData.vectors) {
    $hex = $vector.canonical_text.Replace('-', '')
    $declared = -join ($vector.canonical_bytes | ForEach-Object { '{0:x2}' -f $_ })
    if ($hex -cne $declared -or $hex.Substring(12, 1) -ne '8' -or
        ([Convert]::ToInt32($hex.Substring(16, 1), 16) -band 8) -eq 0) {
        throw "Invalid RFC 9562 UUID vector for $($vector.legacy_alias)"
    }
    $key = "$($vector.object_kind)|$($vector.legacy_alias)"
    if ($objects[$key] -cne $vector.canonical_text) {
        throw "Migrated fixture differs from UUID golden for $key"
    }
}
$additional = Get-Content -LiteralPath $AdditionalFixture -Raw | ConvertFrom-Json
$additionalUuids = @(
    @($additional.scene_ir.materials).uuid
    @($additional.scene_ir.meshes).uuid
    @($additional.scene_ir.images).uuid
    @($additional.scene_ir.textures).uuid
    @($additional.scene_ir.instances).uuid
    @($additional.scene_ir.spheres).uuid
    @($additional.scene_ir.quad_lights).uuid
    $additional.scene_ir.camera.uuid
    $additional.scene_ir.environment_uuid
) | Where-Object { $_ }
if ($additional.document.format -ne 'ure.scene/2.0' -or
    $additionalUuids.Count -eq 0 -or
    @($additionalUuids | Sort-Object -Unique).Count -ne $additionalUuids.Count) {
    throw 'Additional migrated fixture does not preserve unique UUID identities'
}
