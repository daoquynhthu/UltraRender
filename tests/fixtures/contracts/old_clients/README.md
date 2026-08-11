# Legacy and Stable Client Binary Fixtures

Document status: active Phase PB fixture policy

PB.0 records the existing C/pyure surface as `LegacyExperimental`. Its fixtures are migration baselines, not stable ABI promises.

Every retained client fixture directory must contain:

- source code;
- compiled Windows x64 binary when binary compatibility is being tested;
- SDK/header digest;
- compiler and Windows SDK identity;
- expected runtime and capability range;
- content-digested manifest;
- expected success and failure behavior.

PB.0 does not retain a binary merely to imply compatibility. PB.2 begins candidate loader clients, PB.7 executes mixed-version matrices, and PB.8 preserves the final candidate binaries as the Core ABI 1.0 compatibility seed. Binary fixtures must not contain credentials, machine paths, private symbols, or redistributables whose license forbids repository storage.

PB.7 retains one independently compiled C11 client for each PB.2-PB.6 Candidate baseline. Those binaries validate only the interface prefixes known to their historical headers and remain migration evidence with no stable promise. PB.8 additionally retains `core_1_0_seed_from_pb7`, compiled from the final Candidate layout while excluding the removed Candidate-only Scene tail; that seed is the oldest Core 1 client required for future `runtime_1` builds. No Candidate runtime is relabeled as a prior stable runtime. The exact policy is recorded in `docs/PB8_Stable_Compatibility_Report.md`.

The current legacy inventory is [`../registry/legacy_surface.json`](../registry/legacy_surface.json). It binds the baseline header and DLL audit to commit `867c03d34039505bc03aa4539309618e44d8dad2`.
