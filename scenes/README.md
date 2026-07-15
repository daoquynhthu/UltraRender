# UltraRender Scene Fixtures

Document status: current fixture inventory

These checked-in glTF files are maintained inputs for CLI smoke tests and selected end-to-end checks. They are not golden-image references by themselves and do not demonstrate support for every material, integrator or native-scene feature. The matching scripts under `tools/` are their reproducible sources. Run a generator from any directory; it writes to this `scenes/` directory relative to its own file location.

| Fixture | Generator | Coverage |
|---------|-----------|----------|
| `cornell_box.gltf` | `tools/gen_cornell_box.py` | Cornell enclosure, diffuse color transfer, area emission, box geometry, and an explicit camera |
| `showcase.gltf` | `tools/gen_showcase.py` | Mixed diffuse and metallic materials, multiple primitives, enclosed lighting, and an explicit camera |
| `test_plane.gltf` | `tools/gen_test_scenes.py` | Minimal single-mesh geometry and material loading without an authored camera |
| `test_plane_sphere.gltf` | `tools/gen_test_scenes.py` | Minimal multi-mesh scene with diffuse and dielectric materials |

Regenerate all fixtures from the repository root with:

```powershell
python tools/gen_cornell_box.py
python tools/gen_showcase.py
python tools/gen_test_scenes.py
```

Generators must remain deterministic. A generated fixture should be byte-identical to its checked-in counterpart. Changes to a generator and its output belong in the same reviewed change, and future image-reference tests should record the renderer configuration separately from the scene asset.

Native `.ure`, `.urescene` and `.urepkg` validation assets live under `tests/assets/native_scene/`; their authoritative coverage inventory is `tests/assets/native_scene/q12_validation/fixture_manifest.json`.
