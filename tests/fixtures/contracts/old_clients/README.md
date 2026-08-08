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

The current legacy inventory is [`../registry/legacy_surface.json`](../registry/legacy_surface.json). It binds the baseline header and DLL audit to commit `867c03d34039505bc03aa4539309618e44d8dad2`.
