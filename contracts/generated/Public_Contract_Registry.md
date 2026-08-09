# Candidate 0.1 Public Contract Registry

This generated reference is a Candidate artifact. It is not a stable ABI or protocol promise.

Registry digest: `bb9a25aacb63bd88b4e79b67d7932a8b66174627beada11fa068475ca76e1513`

| Registry ID | Kind | Canonical name | Stability | Maturity | Since | Default | Dependencies |
|---:|---|---|---|---|---|---|---|
|1|Structure|`ure.structure.input_header`|Core|NotApplicable|0.1.0|enabled||
|2|Structure|`ure.structure.output_header`|Core|NotApplicable|0.1.0|enabled||
|3|Structure|`ure.structure.uuid`|Core|NotApplicable|0.1.0|enabled||
|4|Structure|`ure.structure.digest256`|Core|NotApplicable|0.1.0|enabled||
|5|Structure|`ure.structure.byte_span`|Core|NotApplicable|0.1.0|enabled||
|6|Structure|`ure.structure.mutable_byte_span`|Core|NotApplicable|0.1.0|enabled||
|7|Structure|`ure.structure.string_view`|Core|NotApplicable|0.1.0|enabled||
|8|Structure|`ure.structure.bootstrap_diagnostic`|Core|NotApplicable|0.1.0|enabled|2, 7|
|9|Structure|`ure.structure.runtime_manifest_request`|Core|NotApplicable|0.1.0|enabled|1, 4|
|10|Structure|`ure.structure.runtime_manifest`|Core|NotApplicable|0.1.0|enabled|2, 4, 5, 7|
|11|Structure|`ure.structure.interface_query`|Core|NotApplicable|0.1.0|enabled|1, 3|
|12|Structure|`ure.structure.interface_response`|Core|NotApplicable|0.1.0|enabled|2, 3|
|13|Structure|`ure.structure.interface_table_header`|Core|NotApplicable|0.1.0|enabled||
|14|Structure|`ure.structure.runtime_interface`|Core|NotApplicable|0.1.0|enabled|13, 700|
|100|Result|`ure.result.success`|Core|NotApplicable|0.1.0|enabled||
|101|Result|`ure.result.incomplete`|Core|NotApplicable|0.1.0|enabled||
|102|Result|`ure.result.invalid_argument`|Core|NotApplicable|0.1.0|enabled||
|103|Result|`ure.result.incompatible_version`|Core|NotApplicable|0.1.0|enabled||
|104|Result|`ure.result.malformed_data`|Core|NotApplicable|0.1.0|enabled||
|105|Result|`ure.result.capability_unavailable`|Core|NotApplicable|0.1.0|enabled||
|106|Result|`ure.result.buffer_too_small`|Core|NotApplicable|0.1.0|enabled||
|107|Result|`ure.result.backpressure`|Core|NotApplicable|0.1.0|enabled||
|108|Result|`ure.result.worker_lost`|Core|NotApplicable|0.1.0|enabled||
|109|Result|`ure.result.internal`|Core|NotApplicable|0.1.0|enabled||
|200|ErrorDomain|`ure.error_domain.core`|Core|NotApplicable|0.1.0|enabled||
|300|Capability|`ure.capability.bootstrap`|Core|NotApplicable|0.1.0|enabled||
|301|Capability|`ure.capability.lifecycle`|Core|NotApplicable|0.1.0|disabled|300|
|302|Capability|`ure.capability.frame_lease`|Core|Experimental|0.1.0|disabled|301|
|303|Capability|`ure.capability.telemetry`|Core|Experimental|0.1.0|disabled|301|
|400|Event|`ure.event.operation_state`|Core|NotApplicable|0.1.0|disabled|301|
|401|Event|`ure.event.frame_ready`|Core|NotApplicable|0.1.0|disabled|302|
|402|Event|`ure.event.gap`|Core|NotApplicable|0.1.0|disabled|301|
|403|Event|`ure.event.device_lost`|Core|NotApplicable|0.1.0|disabled|301|
|500|PayloadSchema|`ure.payload.worker_handshake`|Core|NotApplicable|0.1.0|enabled|4, 300|
|501|PayloadSchema|`ure.payload.worker_envelope`|Core|NotApplicable|0.1.0|enabled|500|
|502|PayloadSchema|`ure.payload.error`|Core|NotApplicable|0.1.0|enabled|200|
|503|PayloadSchema|`ure.payload.capability`|Core|NotApplicable|0.1.0|enabled|300|
|504|PayloadSchema|`ure.payload.operation`|Core|NotApplicable|0.1.0|disabled|301|
|505|PayloadSchema|`ure.payload.event`|Core|NotApplicable|0.1.0|disabled|400|
|506|PayloadSchema|`ure.payload.frame`|Core|NotApplicable|0.1.0|disabled|302, 401|
|507|PayloadSchema|`ure.payload.shared_blob`|Core|NotApplicable|0.1.0|disabled|302|
|600|FramePlane|`ure.frame_plane.color`|Core|NotApplicable|0.1.0|disabled|302|
|601|FramePlane|`ure.frame_plane.spectral`|Core|Experimental|0.1.0|disabled|302|
|602|FramePlane|`ure.frame_plane.stokes`|Core|Experimental|0.1.0|disabled|302|
|700|Interface|`ure.interface.runtime`|Core|NotApplicable|0.1.0|enabled|300|
|701|Interface|`ure.interface.instance`|Core|NotApplicable|0.1.0|disabled|301|
|800|Semantic|`ure.operation.create_instance`|Core|NotApplicable|0.1.0|disabled|301|
|801|Semantic|`ure.operation.start`|Core|NotApplicable|0.1.0|disabled|301|
|802|Semantic|`ure.operation.cancel`|Core|NotApplicable|0.1.0|disabled|301|
|803|Semantic|`ure.operation.acquire_frame`|Core|NotApplicable|0.1.0|disabled|302|
|804|Semantic|`ure.operation.shutdown`|Core|NotApplicable|0.1.0|disabled|301|
|4026531840|Semantic|`ure.private.mock.device_loss`|Private|NotApplicable|0.1.0|disabled|403|
|4026531841|Semantic|`ure.private.mock.worker_crash`|Private|NotApplicable|0.1.0|disabled||
|4026531842|Semantic|`ure.private.mock.event_gap`|Private|NotApplicable|0.1.0|disabled|402|
|4026531843|Semantic|`ure.private.mock.backpressure`|Private|NotApplicable|0.1.0|disabled|107|
