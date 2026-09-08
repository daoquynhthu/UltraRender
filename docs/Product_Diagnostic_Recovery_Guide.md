# Product diagnostic recovery guide

UltraRender Preview diagnostics enrich the existing Core Error and Operation model. They do not define a parallel error system or change Core ABI 1.0 result meanings. The machine-readable catalog is `contracts/diagnostics/product_diagnostic_catalog_v0.json`.

Clients should retain the stable result, domain and detail together with the 32-byte correlation identity. Worker calls additionally retain the transport correlation ID. Terminal operation failures can carry a retained cause; consumers should inspect at most eight levels and release every retained Error handle.

Recovery is determined by `retryability`:

- `DoNotRetry`: preserve the diagnostic and identities. A canceled job requires a new job; `Internal` requires investigation rather than an automatic retry loop.
- `CorrectThenRetry`: correct the request, input, version, budget or selected capability before creating a new job.
- `RetryAfterWindow`: retry only after the observation window or bounded resource pressure has cleared. A Worker wait timeout does not cancel or fail the job.
- `RecreateBoundaryThenRetry`: recreate the Worker/runtime boundary and re-evaluate device applicability before resubmitting.

`BudgetExhausted` with `MemoryNotApplicable` is a pre-execution applicability result. Increase the explicit budget or select a declared semantically equivalent device/plan; do not disable spectral, estimator, output or reconstruction semantics silently. `BudgetExhausted` with `WallBudgetIncomplete` means accepted work was not completed and no complete artifact was published.

`CapabilityUnavailable` with `DeviceNotApplicable` means the requested backend, provider, feature set or stable device identity cannot execute the product objective. Enumerate the Device/Execution 0.1 extension again, choose an `Applicable` descriptor, and compile a new job. `Available` means the adapter exists but does not imply that the current complete-scene product route can execute on it.

`Incomplete` with `ProgressiveFrameUnavailable` means the job is valid but has not published a frame generation yet. Continue observing monotonic progress instead of recreating the job. `Backpressure` with `ProgressiveFrameLeaseBackpressure` means the negotiated Worker mapping or retained-lease budget is full; release older immutable frame leases and retry after a bounded window. A malformed progressive-frame request must be corrected and is never retryable as-is.

Messages are bounded presentation text. Automation should branch on result/domain/detail and versioned structured detail, not match message strings. Absolute private paths, addresses, credentials and unfiltered vendor text are not part of the diagnostic contract.

## Scene, material and package recovery

Scene Tool 0.2 and ProductJob exact-build 0.4 use catalog details 600-628 for PRV.2 failures and details 700-706/711-714 for the currently implemented PRV.3 material boundary. Values 707-710 are intentionally not registered because the production call sites do not currently emit them. Clients should preserve the scene/package semantic identity, operation and sanitized field/resource location supplied with the detail.

- Parse, schema and request-shape details require correcting the input or selecting a supported schema before retry. Corrupt, truncated and ambiguous packages are input errors; do not guess a scene.
- Feature, solver and simulation details distinguish required unsupported semantics from optional data retained for tooling. A required declaration must be removed, corrected or executed by an applicable product capability; retrying unchanged input cannot succeed.
- Resource details distinguish missing content, hash mismatch, dependency cycle, domain mismatch and path traversal. Supply the declared payload with the expected content identity, repair the dependency graph/domain, or repack from a trusted root. Never add an ambient search path or expose an author-machine absolute path as a workaround.
- Stored, decompressed, resident, streamed, temporary and output limits are independent. Increase the specific explicit limit or reduce the declared source within its semantics. A decompression-ratio or container-size rejection must not be bypassed by streaming the same untrusted payload unchecked.
- Package publication details require a writable explicit destination and an atomic retry after the cause is corrected. A rebuildable cache may be removed; required resource payloads may not.

Direct, Worker and CLI preserve the originating result/domain/detail and cause graph. Worker adds transport context without replacing the scene failure. Presentation output redacts private path prefixes; recovery automation should use content identities and safe relative field locations rather than reconstructing a redacted path.

### PRV.3 material recovery

The Product Realizer validates canonical material graphs and all accepted packaged material payloads before renderer allocation. Texture bytes, SPD samples, Mie phase resources and medium coefficients are therefore fail-loud product inputs; a decode or validation failure is not converted into a default material or a late renderer warning.

- `700` (`InvalidMaterialGraph`) and `701` (`UnsupportedMaterialNode`) require repairing the canonical graph or replacing the unsupported node with the maintained MaterialGraph subset.
- `702` (`MaterialResourceMissing`) and `711` (`MaterialTextureDecodeFailed`) require packaging a readable content-bound image and rebuilding the scene/package from a trusted resource root.
- `703` (`SpectralDomainMismatch`) and `712` (`SpectralResourceInvalid`) require a compatible, finite, strictly increasing SPD with complete 400–700 nm coverage; endpoint clamp outside that accepted domain is bound into compiler identity, and ambient search paths are not a recovery mechanism.
- `704` (`WaveMaterialContractInvalid`) requires repairing the bounded radiometric wave-material contract or separating incompatible wave operators into different jobs.
- `705` (`EstimatorNotApplicable`) means no requested/automatic estimator is applicable to the complete material program set; it must not be resolved by silently selecting a weaker semantic route.
- `706` (`InvalidMaterialAdapterRequest`) requires correcting the MaterialX/preset selector, source or operation. Adapter loss reports remain evidence and do not authorize fallback execution.
- `713` (`MieResourceInvalid`) requires regenerating and packaging a normalized content-addressed phase table.
- `714` (`MediumContractInvalid`) requires finite physical coefficients, valid anisotropy and, for Mie media, non-conflicting analytic coefficients and a valid phase resource.

These details are structured Product diagnostics. Direct, Worker and CLI paths preserve the same classification and recovery hint; Worker transport correlation is additional context only.
