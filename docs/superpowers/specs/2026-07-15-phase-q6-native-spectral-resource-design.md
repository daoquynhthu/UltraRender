# Phase Q.6 Native Spectral, Material, Texture, and Medium Resource Design

> Archive status: historical design record. Phase Q is complete; use `PLAN.md`, `STATUS.md`, and current source/tests for present state.

## Decision

Q.6 adds a backend-neutral native resource catalog, identified by FlatBuffers `URRC` and schema identity `ure.resource-catalog/1.0`. It describes the semantic and storage contract of resources while existing typed payloads remain authoritative: `URIG` owns MaterialGraph and scene bindings, `URMI` owns validated Mie tables, image files own encoded pixels, and future typed chunks may own large basis, tile, volume, or video payloads.

The catalog is not GPU upload data. `domain_bins` describes source-domain resolution; `packet_lanes` is only a compile/runtime execution hint and cannot change resource content identity.

## Resource model

Every entry has a stable ID, content hash, resource kind, schema identity/version, payload URI, payload byte length, residency/cache policy, dependencies, and a typed contract.

Spectral contracts declare semantic (reflectance, emission, IOR, extinction coefficient, scattering, absorption, or generic radiometric), representation (constant, RGB-derived, sampled table, basis, tiled, or source-sample grid), wavelength domain and units, sample/basis/tile counts, interpolation/extrapolation, value bounds, and normalization. Sampled axes must be finite and strictly increasing. Reflectance and nonnegative physical coefficients reject negative values.

Texture contracts declare dimensions, spatial channels, source spectral sample count, storage interpretation, color encoding, wrap/filter policy, and optional wavelength-axis dependency. RGB and source-spectral grids are distinct representations; a spectral grid never silently becomes RGB.

Material contracts reference an existing native MaterialGraph owner and its dependent texture/spectral resources. Q.6 does not duplicate graph nodes.

Medium contracts reference spectral sigma-s, sigma-a, and optional emission resources plus an explicit HG, Rayleigh, Mie, or extension phase model. Mie requires a `URMI` dependency. Cross-resource wavelength coverage must contain the declared medium domain.

Video stream contracts describe frame count, time base, dimensions, spectral representation, index/manifest dependency, per-frame hash policy, and random-access/cache budget metadata. Q.6 freezes this source contract but does not implement decoding or GPU streaming.

## Ownership and container integration

The catalog uses core chunk kind 19 and required dependency links to its payload chunks. A `scene.resource` required feature gates consumption. Binary and canonical text projections must have the same semantic hash. Unknown enum values, required dependencies, invalid hashes, unsafe URIs, incompatible domains, and budget overflow fail loudly with `URE-Q6-*` diagnostics.

Catalog entries may describe current SceneIR resources without forcing SceneIR to adopt future basis/tile/video runtime structures. Compilation resolves supported representations; unsupported required representations fail before session creation.

## Hashing and cache behavior

The catalog semantic hash covers sorted canonical entries and typed contracts, excluding mutable cache locations and runtime packet width. Payload hashes remain content hashes. Residency and cache policy participate only where they alter source semantics; removable cache records never become authoritative.

## Verification

Host tests cover binary/text roundtrip, semantic-hash equivalence, million-bin metadata without allocation, sampled/source-grid/basis/tile/video contracts, Mie/medium dependency validation, invalid domains and bounds, dependency cycles, payload budgets, and packet-lane independence. Existing Q.3 retained compilation fixtures remain green.
