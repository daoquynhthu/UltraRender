param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
$schema = Get-Content -LiteralPath (Join-Path $RepoRoot 'contracts/schemas/ure_scene_v1.fbs') -Raw
$loader = Get-Content -LiteralPath (Join-Path $RepoRoot 'contracts/generated/include/ultrarender/ure_loader.h') -Raw
$adapter = Get-Content -LiteralPath (Join-Path $RepoRoot 'libs/ure_contract/src/scene_transaction.cpp') -Raw
$worker = Get-Content -LiteralPath (Join-Path $RepoRoot 'apps/ure_worker/main.cpp') -Raw
$requiredSchema = @(
    'transaction_uuid:UuidValue',
    'base_revision:ulong',
    'operations:[SceneEditOperation]',
    'required_capabilities:[uint]',
    'world_from_camera:[double]',
    'retry_base_revision:ulong',
    'mesh_replace:PayloadReplaceEdit'
)
foreach ($pattern in $requiredSchema) {
    if ($schema -notmatch [regex]::Escape($pattern)) {
        throw "PB.6 schema is missing $pattern"
    }
}
if ($schema -match '(?im)^\s*(object|material|mesh)_index\s*:' -or
    $loader -match '(?i)(object|material|mesh)_index') {
    throw 'PB.6 public edit addressing contains an index authority'
}
foreach ($pattern in @('apply_transaction', 'ure_uuid_t transaction_id',
                       'URE_RESULT_REVISION_CONFLICT',
                       'URE_SCENE_UPDATE_FULL_RELOAD')) {
    if (($loader + $adapter) -notmatch $pattern) {
        throw "PB.6 runtime boundary is missing $pattern"
    }
}
if ($worker -notmatch 'URE_OPERATION_APPLY_SCENE_TRANSACTION' -or
    $worker -notmatch 'URE_PAYLOAD_SCENE_TRANSACTION') {
    throw 'Product worker does not route the generated transaction contract'
}
$fixture = Get-Content -LiteralPath (Join-Path $RepoRoot 'tests/assets/native_scene/pb6_scene_transaction_full/base_scene.ure') -Raw | ConvertFrom-Json
if ($fixture.document.format -ne 'ure.scene/2.0' -or
    @($fixture.scene_ir.instances | Where-Object { -not $_.uuid }).Count -ne 0 -or
    -not $fixture.scene_ir.camera.uuid) {
    throw 'PB.6 migration fixture does not preserve persistent UUIDs'
}
