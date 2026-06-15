# Phase L Completion Audit

Date: 2026-06-15

Scope: Phase L, "million-scale spectral domain / packet-resolution decoupling".

## Requirements And Evidence

| Requirement | Evidence | Status |
|-------------|----------|--------|
| `domain_bins` and `packet_lanes` are separate runtime concepts | `libs/ure_types/include/ure/render_config.hpp` exposes `spectral_domain_bins` and `spectral_packet_lanes`; `num_wavelengths` is documented as a legacy packet-lane alias | Complete |
| Million-scale spectral resources do not require million-lane GPU ray state | `SpectralRayModeSampled` stores one sampled wavelength and continuous PDF; `tests/gpu/test_spectral_pipeline.cu` includes 1M-domain oracle comparisons | Complete |
| Materials/SPD are resource-evaluated by wavelength, not only eager packet cache | `SpectralResource`/`HostSpectralResource` descriptors and `eval_spectral_resource()` are used by material loaders and expression graph evaluation | Complete |
| Explicit spectral textures are not expanded by `texel_count * domain_bins` or packet lanes | `GpuTexture::spectral_source_values` stores source sample grids; L.8 render tests verify source sample count remains independent of `spectral_domain_bins` | Complete |
| MaterialGraph no longer forces Texture/Add/Mix into packet-only flatten | `SpectralExpressionNode` and `eval_material_expression()` are covered by host and GPU MaterialGraph tests | Complete |
| Distributed and farm contracts carry spectral-domain metadata | `DistributedSpectralDomainShard`, `DistributedFrameShard`, file format v2 metadata, merge compatibility checks, and mismatch rejection tests exist | Complete |
| Low-end hardware has explicit budget/preset behavior and can reject impossible resident resources before GPU allocation | `SpectralRuntimePlan` includes sampler/cache/stream presets and resident byte estimates; GPU init calls `validate_explicit_spectral_resident_budget()` before CUDA allocations; render test covers oversized spectral texture rejection | Complete |
| High-end/farm path has scalable preset and shard vocabulary | Hardware tests cover high-end multi-stream preset and farm-shard preset; distributed contract supports sample, spectral-domain, and frame shard metadata | Complete |
| Regression guard prevents returning to fixed-channel/resource architecture | `scripts/check_phase_l_static.ps1` rejects old `GpuSpectrum`, old channel caps, domain-to-packet assignment, packet texture upload, and domain-sized GPU init allocation | Complete |
| Current proof includes runtime verification, not only static search | `build_x64.ps1`, CTest 21/21, Phase L static audit, glTF validate, and 1SPP 1M-domain HDR benchmark smoke have all passed | Complete |

## Explicit Non-Blockers

These are intentionally left for later performance/material phases and are not Phase L completion blockers:

- Real sparse virtual spectral texture runtime, basis compression, and resource prefetch.
- Systematic performance benchmark suite beyond the Phase L smoke fixture.
- MaterialX import/export, USD schema, and plugin ABI implementation. Phase L only requires their future contracts not be forced through fixed packet resources.
- Full farm scheduler. Phase L provides the shard metadata/file contract and presets required by a scheduler.

## Final Gate

Required final gate for this audit:

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config RelWithDebInfo
ctest --test-dir build_modular_x64 --output-on-failure
.\scripts\check_phase_l_static.ps1
& .\build_modular_x64\apps\ure_cli\ure_cli.exe validate .\scenes\benchmarks\phase_l_spectral_budget.gltf
& .\tools\benchmarks\run_phase_l_spectral_smoke.ps1 -BuildDir build_modular_x64 -Spp 1 -Width 32 -Height 32 -Output phase_l_spectral_budget_smoke.hdr
git diff --check
git diff --cached --check
```
