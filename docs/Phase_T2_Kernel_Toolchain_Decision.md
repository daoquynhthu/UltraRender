# Phase T.2 — Portable Kernel Toolchain Decision

## Decision

UltraRender will use Slang as the shared-source frontend and portable kernel IR
toolchain. CUDA PTX/cubin, Vulkan SPIR-V, and D3D12 DXIL are generated from one
Slang semantic module. Backend-private intrinsics remain allowed behind focused
interfaces when a capability cannot be expressed portably.

This decision does not adopt `slang-rhi`, change the runtime API, or migrate
production kernels during T.2. Runtime ownership remains T.3-T.6. Existing CUDA
kernels remain the production and physical reference until CUDA parity is
closed.

The gate pins Slang 2026.14 and verifies the release archive SHA-256
`36029c50ef0c82f2616ffb02e0ed27d642cb44a2a297d531cc2ad333b85b85b6`.
The compiler is downloaded into the ignored `.build/toolchains` directory and
is not silently fetched by the ordinary build.

## Evaluated approaches

| Approach | Result | Reason |
|---|---|---|
| Restricted shared C++ device subset | Rejected | CUDA accepts C++, but SPIR-V and DXIL require separate translation paths, binding/reflection conventions, subgroup dialects, and debug mappings. Maintaining those adapters would reproduce three source languages behind macros and leave no single validated IR. |
| Custom URE KernelIR with backend lowerings | Rejected for the production frontend | It would require UltraRender to own parsing, optimization, validation, reflection, source mapping, and three code generators before runtime migration could start. A small execution/dispatch IR remains appropriate for T.5, but it must not become a shader compiler. |
| Slang single source and IR | Selected | The same source compiled directly to PTX, SPIR-V, and DXIL with stable reflection, 64-bit layout, subgroup and atomic operations, specialization constants, validation, and debug source information. |

Slang is Apache-2.0 with LLVM exception. Its official documentation identifies
CUDA, SPIR-V/Vulkan, and DXIL/D3D12 as supported targets and documents direct
SPIR-V generation, reflection JSON, specialization, and debug information:

- [Slang project and license](https://github.com/shader-slang/slang)
- [Supported compilation targets](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/09-targets.html)
- [Compilation and debug options](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/08-compiling.html)
- [64-bit target support](https://docs.shader-slang.org/en/stable/external/slang/docs/64bit-type-support.html)

## Prototype scope

`shaders/shared/portable_semantics.slang` owns shared mathematical
helpers. `phase_t2_prototypes.slang` supplies six compute entry points:

| Entry | Contract exercised | Existing production reference |
|---|---|---|
| `spectral_conversion` | Runtime packet loop, spectral interpolation, XYZ accumulation, specialization constant | `gpu_spectrum_utils.cuh`, `gpu_spectral` |
| `mueller_transport` | Coupled Stokes I/Q and U/V Mueller blocks | `path_tracer_polarization.cuh`, `gpu_polarization` |
| `queue_compaction` | Wave ballot/count, global atomics, bounded output, overflow accounting, 64-bit base identity | Wavefront queues and `gpu_render` |
| `bsdf_sampling` | Cosine-weighted hemisphere sampling and matching PDF | Lambertian/material sampling tests |
| `wave_propagation` | Complex phasor rotation and conserved field power | Wave optics host/CUDA references |
| `traversal_query` | Sign-preserving inverse direction and slab AABB query | CUDA traversal and render intersection tests |

These prototypes exercise the difficult language and lowering boundaries. They
do not replace the production physical oracles. Correctness remains two-layered:
the T.2 gate proves identical source acceptance and required target
instructions/layouts, while the existing CUDA test suite remains authoritative
for numerical physics until migrated kernels execute through T.3-T.6.

## Layout, capability, and generated-code evidence

`scripts/run_phase_t2_kernel_toolchain_gate.ps1` performs an offline,
warnings-as-errors build twice and requires byte-identical output. For every
entry and target it records SHA-256, size, compilation time, and reflection.
The reflected `KernelParams` layout contains a 64-bit `baseAddress` at byte
offset 8 across PTX, SPIR-V, and DXIL.

The gate additionally requires:

- SPIR-V `Int64`, group-nonuniform ballot, atomic add, `OpSpecConstant`, direct
  generation, DirectX-compatible layout, and non-semantic debug line records;
- DXIL shader-model 6.6 declarations for wave operations and 64-bit integers,
  atomic lowering, validator-backed compilation, and `DIFile` source mapping;
- PTX 64-bit addressing, vote/active-mask/popcount lowering, global atomics,
  `.loc` source mapping, and successful `ptxas` assembly for `sm_120`.

On the validated RTX 5060 Laptop GPU, the six cubins use 15-40 registers,
zero spill loads/stores, zero static shared memory, and 100% occupancy at the
64-thread prototype block size according to the CUDA driver occupancy API.
The evidence is generated at `output/phase_t2/phase_t2_report.json`; generated
binaries are deliberately ignored and must be recreated.

## Build and dependency boundary

The pinned Windows archive is approximately 56.5 MB. T.2 uses only `slangc`
and its compiler dependencies; it adds no runtime DLL dependency to
`ure_core`, the C ABI, or applications. The ordinary CUDA build remains
unchanged. CI or a developer runs the explicit gate, and `-Offline` requires
the pinned archive and extracted compiler to already exist.

T.3 should integrate the Slang compiler as an offline module-build step with
content-addressed cache identity containing compiler version, source/module
hash, target/profile, specialization values, and relevant layout options.
Runtime shader compilation must remain optional and must never define scene or
physical semantics.

## Reproduction

```powershell
.\scripts\run_phase_t2_kernel_toolchain_gate.ps1
```

Successful completion produces
`ure.phase_t.kernel_toolchain_feasibility.v1` with artifacts for all six
entries and all three targets. Any missing target, layout disagreement,
non-deterministic binary, warning, validation failure, spill, occupancy below
50%, or absent debug/capability evidence fails the gate.
