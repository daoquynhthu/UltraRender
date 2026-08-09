# Candidate 0.1 Mock Worker

This fixture is a deterministic frontend-development aid, not a production worker or a stable protocol promise.

`mock_protocol.cpp` provides the bounded in-memory protocol harness. `ultrarender_mock_worker` is a standalone wrapper that reads one length-prefixed request fixture and writes one deterministic response fixture. It links only the contract-codegen support library and FlatBuffers headers; it never loads renderer, scene, GPU, or backend code and opens no network endpoint.

The `external_client` target is compiled as C11 against the staged generated headers only. It launches the staged mock executable and verifies every response against the staged golden byte package, including required/optional capability negotiation, an actual forward-schema field, malformed/truncated/oversized framing, event gaps, backpressure, device loss, and worker termination.

The file wrapper exists only to make the in-memory harness independently executable. PB.4 owns the authenticated local Named Pipe and shared-memory transport.
