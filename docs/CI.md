# Hosted Non-GPU CI

Last reviewed: 2026-08-11

The maintained GitHub Actions workflow validates the portable host and SDK-free portions of the repository. It is intentionally separate from the CUDA production-renderer gate and does not create a new public ABI or rendering-platform promise.

## Matrix

| Runner | Compiler | Scope |
|---|---|---|
| Ubuntu 24.04 | GCC 13 | CUDA-off root build, host tests, install consumer, strict SDK-free build |
| Ubuntu 24.04 | Clang 18 | CUDA-off root build, host tests, install consumer, strict SDK-free build |
| Windows 2025 | MSVC | CUDA-off root build, host tests, install consumer, strict SDK-free build |

Every lane builds Release with Ninja and performs three independent checks:

1. The root project builds `ure_public`, contract code generation, types, diagnostics, runtime, transport, research, reconstruction, configuration, scene I/O, and optional physics while CUDA and GPU backends are disabled. The 32 tests that do not link `ure_core` or `ure_cli` execute from this tree.
2. The root package is installed to a clean prefix. A separate consumer configures with `find_package(UltraRender)`, links the installed SDK-free components, and runs.
3. `tests/sdk_free` recompiles the runtime, transport, research, and reconstruction boundary independently and runs 15 tests with compiler warnings treated as errors.

The root CMake graph places final products under `build/root-<lane>/artifacts/Release/`: executables, runtime dynamic libraries, and runtime shaders in `bin`; static/import/Unix link libraries in `lib`; emitted debug-symbol databases in `symbols`; and PB.8 deliverable trees in `pb8_packages` when the CUDA-coupled public runtime is enabled. Each hosted lane asserts representative executable and library paths and rejects the former per-subdirectory executable location. CMake intermediates, generated sources, test evidence, install trees, and non-release staging areas remain outside this final-product tree.

CUDA, Vulkan, D3D12/DXR, OptiX, the CUDA-coupled product runtime/worker/CLI, GPU tests, and optional SDK-coupled Hydra targets are excluded. Their absence from this workflow is explicit and is not converted into a stub or silent fallback.

## Tool and cache policy

FlatBuffers code generation requires exactly `flatc` 25.12.19. CI downloads the official platform archive, verifies the pinned archive SHA-256, verifies the extracted executable SHA-256 even on cache hits, and checks the reported version. The extracted tool is cached by platform, version, and digest.

Linux lanes use `ccache`, isolated by compiler family. The primary key includes the CMake graph and workflow definition; a compiler-specific prefix permits safe reuse after source or build-graph changes because `ccache` independently hashes compiler inputs. Windows caches the pinned generator but performs a fresh MSVC object build; no unaudited compiler-wrapper cache is inserted into the Windows correctness lane.

CMake build trees, install prefixes, generated packages, and test outputs are never restored from cache. They contain absolute paths and configuration decisions and could otherwise hide stale dependency or packaging defects. Superseded runs on the same ref are cancelled, and CMake/CTest diagnostics are uploaded only for failing lanes.

## Local equivalents

The Windows root lane can be reproduced from a configured MSVC x64 shell with:

```powershell
cmake -S . -B build_ci_cpu -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DUR_ENABLE_CUDA=OFF -DUR_ENABLE_OPTIX=OFF `
  -DUR_ENABLE_VULKAN=OFF -DUR_ENABLE_D3D12=OFF `
  -DUR_ENABLE_HYDRA=OFF -DUR_BUILD_CLI=OFF `
  -DUR_BUILD_TESTS=ON -DUR_BUILD_PHYSICS=ON
cmake --build build_ci_cpu --parallel
ctest --test-dir build_ci_cpu --output-on-failure
```

The workflow additionally supplies the pinned `UR_FLATC_EXECUTABLE`, installs the root package, builds the package consumer, and configures `tests/sdk_free`. The workflow file is the executable authority for exact hosted commands.
