# Core 1.0 Public Contract Registry

This generated reference freezes the Core ABI 1.x and Worker Protocol 1.x identity space. Extension maturity remains independent.

Registry digest: `62501eff808502857828856d4a83f4a9be3322725747d2cb1e12343d6ef06c56`

| Registry ID | Kind | Canonical name | Stability | Maturity | Since | Default | Dependencies |
|---:|---|---|---|---|---|---|---|
|1|Structure|`ure.structure.input_header`|Core|NotApplicable|1.0.0|enabled||
|2|Structure|`ure.structure.output_header`|Core|NotApplicable|1.0.0|enabled||
|3|Structure|`ure.structure.uuid`|Core|NotApplicable|1.0.0|enabled||
|4|Structure|`ure.structure.digest256`|Core|NotApplicable|1.0.0|enabled||
|5|Structure|`ure.structure.byte_span`|Core|NotApplicable|1.0.0|enabled||
|6|Structure|`ure.structure.mutable_byte_span`|Core|NotApplicable|1.0.0|enabled||
|7|Structure|`ure.structure.string_view`|Core|NotApplicable|1.0.0|enabled||
|8|Structure|`ure.structure.bootstrap_diagnostic`|Core|NotApplicable|1.0.0|enabled|2, 7|
|9|Structure|`ure.structure.runtime_manifest_request`|Core|NotApplicable|1.0.0|enabled|1, 4|
|10|Structure|`ure.structure.runtime_manifest`|Core|NotApplicable|1.0.0|enabled|2, 4, 5, 7|
|11|Structure|`ure.structure.interface_query`|Core|NotApplicable|1.0.0|enabled|1, 3|
|12|Structure|`ure.structure.interface_response`|Core|NotApplicable|1.0.0|enabled|2, 3|
|13|Structure|`ure.structure.interface_table_header`|Core|NotApplicable|1.0.0|enabled||
|14|Structure|`ure.structure.runtime_interface`|Core|NotApplicable|1.0.0|enabled|13, 15, 27, 700|
|15|Structure|`ure.structure.instance_create_info`|Core|NotApplicable|1.0.0|enabled|1, 301|
|16|Structure|`ure.structure.capability_query`|Core|NotApplicable|1.0.0|enabled|1, 26, 300|
|17|Structure|`ure.structure.capability_descriptor`|Core|NotApplicable|1.0.0|enabled|2, 26, 300|
|18|Structure|`ure.structure.error_info`|Core|NotApplicable|1.0.0|enabled|2, 5, 7, 27, 200|
|19|Structure|`ure.structure.operation_info`|Core|NotApplicable|1.0.0|enabled|2, 26, 27, 301|
|20|Structure|`ure.structure.event_record`|Core|NotApplicable|1.0.0|enabled|2, 5, 27, 400|
|22|Structure|`ure.structure.instance_interface`|Core|NotApplicable|1.0.0|enabled|13, 16, 17, 27, 100, 701|
|23|Structure|`ure.structure.error_interface`|Core|NotApplicable|1.0.0|enabled|13, 18, 27, 100, 702|
|24|Structure|`ure.structure.operation_interface`|Core|NotApplicable|1.0.0|enabled|13, 19, 27, 100, 703|
|25|Structure|`ure.structure.event_interface`|Core|NotApplicable|1.0.0|enabled|13, 20, 27, 100, 704|
|26|Structure|`ure.structure.bool32`|Core|NotApplicable|1.0.0|enabled||
|27|Structure|`ure.structure.handle`|Core|NotApplicable|1.0.0|enabled||
|28|Structure|`ure.structure.instance_frame_budget`|Core|NotApplicable|1.0.0|disabled|1, 302|
|29|Structure|`ure.structure.frame_info`|Core|NotApplicable|1.0.0|disabled|2, 4, 27, 302|
|30|Structure|`ure.structure.frame_plane_info`|Core|NotApplicable|1.0.0|disabled|2, 4, 302, 600|
|31|Structure|`ure.structure.frame_map`|Core|NotApplicable|1.0.0|disabled|2, 27, 302|
|32|Structure|`ure.structure.frame_copy_info`|Core|NotApplicable|1.0.0|disabled|1, 6, 27, 302|
|33|Structure|`ure.structure.frame_interface`|Core|NotApplicable|1.0.0|disabled|13, 27, 29, 30, 31, 32, 100, 705|
|34|Structure|`ure.structure.scene_budget`|Core|NotApplicable|1.0.0|disabled|1, 304|
|35|Structure|`ure.structure.native_scene_blob`|Core|NotApplicable|1.0.0|disabled|1, 5, 7, 34, 304|
|36|Structure|`ure.structure.scene_validation_result`|Core|NotApplicable|1.0.0|disabled|2, 4, 304|
|37|Structure|`ure.structure.scene_revision_info`|Core|NotApplicable|1.0.0|disabled|2, 4, 7, 304|
|38|Structure|`ure.structure.objective_envelope`|Core|NotApplicable|1.0.0|disabled|1, 4, 5, 305|
|39|Structure|`ure.structure.session_info`|Core|NotApplicable|1.0.0|disabled|2, 4, 27, 305|
|40|Structure|`ure.structure.scene_interface`|Core|NotApplicable|1.0.0|disabled|13, 27, 34, 35, 36, 37, 706|
|41|Structure|`ure.structure.session_interface`|Core|NotApplicable|1.0.0|disabled|13, 27, 38, 39, 707|
|100|Result|`ure.result.success`|Core|NotApplicable|1.0.0|enabled||
|101|Result|`ure.result.incomplete`|Core|NotApplicable|1.0.0|enabled||
|102|Result|`ure.result.invalid_argument`|Core|NotApplicable|1.0.0|enabled||
|103|Result|`ure.result.incompatible_version`|Core|NotApplicable|1.0.0|enabled||
|104|Result|`ure.result.malformed_data`|Core|NotApplicable|1.0.0|enabled||
|105|Result|`ure.result.capability_unavailable`|Core|NotApplicable|1.0.0|enabled||
|106|Result|`ure.result.buffer_too_small`|Core|NotApplicable|1.0.0|enabled||
|107|Result|`ure.result.backpressure`|Core|NotApplicable|1.0.0|enabled||
|108|Result|`ure.result.worker_lost`|Core|NotApplicable|1.0.0|enabled||
|109|Result|`ure.result.internal`|Core|NotApplicable|1.0.0|enabled||
|110|Result|`ure.result.invalid_handle`|Core|NotApplicable|1.0.0|enabled||
|111|Result|`ure.result.busy`|Core|NotApplicable|1.0.0|enabled||
|112|Result|`ure.result.timeout`|Core|NotApplicable|1.0.0|enabled||
|113|Result|`ure.result.canceled`|Core|NotApplicable|1.0.0|enabled||
|114|Result|`ure.result.device_lost`|Core|NotApplicable|1.0.0|enabled||
|115|Result|`ure.result.budget_exhausted`|Core|NotApplicable|1.0.0|enabled||
|116|Result|`ure.result.revision_conflict`|Core|NotApplicable|1.0.0|enabled||
|200|ErrorDomain|`ure.error_domain.core`|Core|NotApplicable|1.0.0|enabled||
|300|Capability|`ure.capability.bootstrap`|Core|NotApplicable|1.0.0|enabled||
|301|Capability|`ure.capability.lifecycle`|Core|NotApplicable|1.0.0|enabled|300|
|302|Capability|`ure.capability.frame_lease`|Core|Experimental|1.0.0|disabled|301|
|304|Capability|`ure.capability.native_scene`|Core|Experimental|1.0.0|disabled|301|
|305|Capability|`ure.capability.render_session`|Core|Experimental|1.0.0|disabled|302, 304|
|400|Event|`ure.event.operation_state`|Core|NotApplicable|1.0.0|enabled|301|
|401|Event|`ure.event.frame_ready`|Core|NotApplicable|1.0.0|disabled|302|
|402|Event|`ure.event.gap`|Core|NotApplicable|1.0.0|enabled|301|
|403|Event|`ure.event.device_lost`|Core|NotApplicable|1.0.0|enabled|301|
|404|Event|`ure.event.diagnostic`|Core|NotApplicable|1.0.0|enabled|301|
|405|Event|`ure.event.scene_replaced`|Core|NotApplicable|1.0.0|disabled|304|
|500|PayloadSchema|`ure.payload.worker_handshake`|Core|NotApplicable|1.0.0|enabled|4, 300|
|501|PayloadSchema|`ure.payload.worker_envelope`|Core|NotApplicable|1.0.0|enabled|500|
|502|PayloadSchema|`ure.payload.error`|Core|NotApplicable|1.0.0|enabled|200|
|503|PayloadSchema|`ure.payload.capability`|Core|NotApplicable|1.0.0|enabled|300|
|504|PayloadSchema|`ure.payload.operation`|Core|NotApplicable|1.0.0|disabled|301|
|505|PayloadSchema|`ure.payload.event`|Core|NotApplicable|1.0.0|disabled|400|
|506|PayloadSchema|`ure.payload.frame`|Core|NotApplicable|1.0.0|disabled|302, 401|
|507|PayloadSchema|`ure.payload.shared_blob`|Core|NotApplicable|1.0.0|disabled|302|
|508|PayloadSchema|`ure.payload.native_scene`|Core|NotApplicable|1.0.0|disabled|304|
|509|PayloadSchema|`ure.payload.scene_revision`|Core|NotApplicable|1.0.0|disabled|304|
|510|PayloadSchema|`ure.payload.render_objective`|Core|NotApplicable|1.0.0|disabled|305|
|600|FramePlane|`ure.frame_plane.color`|Core|NotApplicable|1.0.0|disabled|302|
|700|Interface|`ure.interface.runtime`|Core|NotApplicable|1.0.0|enabled|300|
|701|Interface|`ure.interface.instance`|Core|NotApplicable|1.0.0|enabled|301|
|702|Interface|`ure.interface.error`|Core|NotApplicable|1.0.0|enabled|200|
|703|Interface|`ure.interface.operation`|Core|NotApplicable|1.0.0|enabled|301|
|704|Interface|`ure.interface.event`|Core|NotApplicable|1.0.0|enabled|301|
|705|Interface|`ure.interface.frame`|Core|NotApplicable|1.0.0|disabled|302|
|706|Interface|`ure.interface.scene`|Core|NotApplicable|1.0.0|disabled|304|
|707|Interface|`ure.interface.session`|Core|NotApplicable|1.0.0|disabled|305|
|800|Semantic|`ure.operation.create_instance`|Core|NotApplicable|1.0.0|disabled|301|
|801|Semantic|`ure.operation.start`|Core|NotApplicable|1.0.0|disabled|301|
|802|Semantic|`ure.operation.cancel`|Core|NotApplicable|1.0.0|disabled|301|
|803|Semantic|`ure.operation.acquire_frame`|Core|NotApplicable|1.0.0|disabled|302|
|804|Semantic|`ure.operation.shutdown`|Core|NotApplicable|1.0.0|disabled|301|
|805|Semantic|`ure.operation.validate_scene`|Core|NotApplicable|1.0.0|disabled|304|
|806|Semantic|`ure.operation.replace_scene`|Core|NotApplicable|1.0.0|disabled|304|
|807|Semantic|`ure.operation.render_session`|Core|NotApplicable|1.0.0|disabled|305|
|900|Semantic|`ure.operation_state.queued`|Core|NotApplicable|1.0.0|disabled|703|
|901|Semantic|`ure.operation_state.running`|Core|NotApplicable|1.0.0|disabled|703|
|902|Semantic|`ure.operation_state.paused`|Core|NotApplicable|1.0.0|disabled|703|
|903|Semantic|`ure.operation_state.cancel_pending`|Core|NotApplicable|1.0.0|disabled|703|
|904|Semantic|`ure.operation_state.succeeded`|Core|NotApplicable|1.0.0|disabled|703|
|905|Semantic|`ure.operation_state.canceled`|Core|NotApplicable|1.0.0|disabled|703|
|906|Semantic|`ure.operation_state.failed`|Core|NotApplicable|1.0.0|disabled|703|
|907|Semantic|`ure.operation_state.device_lost`|Core|NotApplicable|1.0.0|disabled|703|
|920|Semantic|`ure.thread_policy.externally_synchronized`|Core|NotApplicable|1.0.0|enabled||
|921|Semantic|`ure.thread_policy.concurrent_read`|Core|NotApplicable|1.0.0|enabled||
|922|Semantic|`ure.thread_policy.concurrent`|Core|NotApplicable|1.0.0|enabled||
|930|Semantic|`ure.runtime_state.compiled`|Core|NotApplicable|1.0.0|enabled||
|931|Semantic|`ure.runtime_state.available`|Core|NotApplicable|1.0.0|enabled||
|932|Semantic|`ure.runtime_state.enabled`|Core|NotApplicable|1.0.0|enabled||
|933|Semantic|`ure.runtime_state.applicable`|Core|NotApplicable|1.0.0|enabled||
|940|Semantic|`ure.stability.core`|Core|NotApplicable|1.0.0|enabled||
|941|Semantic|`ure.stability.stable_extension`|Core|NotApplicable|1.0.0|enabled||
|942|Semantic|`ure.stability.unstable_extension`|Core|NotApplicable|1.0.0|enabled||
|943|Semantic|`ure.stability.private`|Core|NotApplicable|1.0.0|enabled||
|950|Semantic|`ure.maturity.research`|Core|NotApplicable|1.0.0|enabled||
|951|Semantic|`ure.maturity.experimental`|Core|NotApplicable|1.0.0|enabled||
|952|Semantic|`ure.maturity.production`|Core|NotApplicable|1.0.0|enabled||
|953|Semantic|`ure.maturity.not_applicable`|Core|NotApplicable|1.0.0|enabled||
|960|Semantic|`ure.scalar_type.float32`|Core|NotApplicable|1.0.0|disabled|600|
|961|Semantic|`ure.component_layout.rgba`|Core|NotApplicable|1.0.0|disabled|600|
|962|Semantic|`ure.frame_completion.complete`|Core|NotApplicable|1.0.0|disabled|302|
|963|Semantic|`ure.normalization.sample_mean`|Core|NotApplicable|1.0.0|disabled|600|
|964|Semantic|`ure.digest_algorithm.sha256`|Core|NotApplicable|1.0.0|disabled|4, 507|
|965|Semantic|`ure.shared_blob_access.read`|Core|NotApplicable|1.0.0|disabled|507|
|966|Semantic|`ure.scene_source.memory`|Core|NotApplicable|1.0.0|disabled|35|
|967|Semantic|`ure.scene_source.file`|Core|NotApplicable|1.0.0|disabled|35|
|968|Semantic|`ure.scene_format.text`|Core|NotApplicable|1.0.0|disabled|35|
|969|Semantic|`ure.scene_format.binary`|Core|NotApplicable|1.0.0|disabled|35|
|970|Semantic|`ure.scene_format.package`|Core|NotApplicable|1.0.0|disabled|35|
|971|Semantic|`ure.scene_reset.full_replacement`|Core|NotApplicable|1.0.0|disabled|304|
|972|Semantic|`ure.scene_reset.explicit`|Core|NotApplicable|1.0.0|disabled|305|
|973|Semantic|`ure.session_state.created`|Core|NotApplicable|1.0.0|disabled|707|
|974|Semantic|`ure.session_state.ready`|Core|NotApplicable|1.0.0|disabled|707|
|975|Semantic|`ure.session_state.running`|Core|NotApplicable|1.0.0|disabled|707|
|976|Semantic|`ure.session_state.paused`|Core|NotApplicable|1.0.0|disabled|707|
|977|Semantic|`ure.session_state.failed`|Core|NotApplicable|1.0.0|disabled|707|
|978|Semantic|`ure.session_state.device_lost`|Core|NotApplicable|1.0.0|disabled|707|
|979|Semantic|`ure.session_state.closed`|Core|NotApplicable|1.0.0|disabled|707|
|2147483648|PayloadSchema|`ure.experimental.payload.scene_transaction`|UnstableExtension|Experimental|1.0.0|disabled|304|
|2147483649|Semantic|`ure.experimental.scene_edit.transform`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483650|Semantic|`ure.experimental.scene_edit.camera`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483651|Semantic|`ure.experimental.scene_edit.material_reference`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483652|Semantic|`ure.experimental.scene_edit.payload_replace`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483653|Semantic|`ure.experimental.scene_edit.mesh_reference`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483654|Semantic|`ure.experimental.scene_edit.add_object`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483655|Semantic|`ure.experimental.scene_edit.remove_object`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483656|Semantic|`ure.experimental.scene_edit.visibility`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483657|Semantic|`ure.experimental.scene_edit.light`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483658|Semantic|`ure.experimental.scene_edit.environment`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483659|Semantic|`ure.experimental.scene_edit.mesh_replace`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483660|Semantic|`ure.experimental.scene_object.instance`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483661|Semantic|`ure.experimental.scene_object.sphere`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483662|Semantic|`ure.experimental.scene_object.quad_light`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483663|Semantic|`ure.experimental.scene_object.material`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483664|Semantic|`ure.experimental.scene_object.mesh`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483665|Semantic|`ure.experimental.scene_object.image`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483666|Semantic|`ure.experimental.scene_object.texture`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483667|Semantic|`ure.experimental.scene_object.camera`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483668|Semantic|`ure.experimental.scene_object.environment`|UnstableExtension|Experimental|1.0.0|disabled|2147483648|
|2147483669|Capability|`ure.experimental.capability.telemetry`|UnstableExtension|Experimental|1.0.0|disabled|301|
|2147483670|FramePlane|`ure.experimental.frame_plane.spectral`|UnstableExtension|Experimental|1.0.0|disabled|302|
|2147483671|FramePlane|`ure.experimental.frame_plane.stokes`|UnstableExtension|Experimental|1.0.0|disabled|302|
|2147483672|Semantic|`ure.experimental.scene_update.hot_update`|UnstableExtension|Experimental|1.0.0|disabled|304|
|2147483673|Semantic|`ure.experimental.scene_update.partial_rebuild`|UnstableExtension|Experimental|1.0.0|disabled|304|
|2147483674|Semantic|`ure.experimental.scene_update.full_reload`|UnstableExtension|Experimental|1.0.0|disabled|304|
|2147483675|Semantic|`ure.experimental.scene_update.rejected`|UnstableExtension|Experimental|1.0.0|disabled|304|
|2147483676|Structure|`ure.experimental.structure.scene_transaction`|UnstableExtension|Experimental|1.0.0|disabled|1, 3, 4, 5, 7, 304, 2147483648|
|2147483677|Structure|`ure.experimental.structure.scene_transaction_result`|UnstableExtension|Experimental|1.0.0|disabled|2, 3, 4, 6, 304, 2147483648, 2147483672, 2147483673, 2147483674, 2147483675|
|2147483678|Structure|`ure.experimental.structure.scene_transaction_interface`|UnstableExtension|Experimental|1.0.0|disabled|13, 27, 2147483676, 2147483677, 2147483679|
|2147483679|Interface|`ure.experimental.interface.scene_transaction`|UnstableExtension|Experimental|1.0.0|disabled|304, 2147483648|
|2147483680|Event|`ure.experimental.event.scene_transaction_committed`|UnstableExtension|Experimental|1.0.0|disabled|304, 2147483681|
|2147483681|Semantic|`ure.experimental.operation.apply_scene_transaction`|UnstableExtension|Experimental|1.0.0|disabled|304, 2147483648|
|2147483682|Capability|`ure.preview.capability.product_job`|UnstableExtension|Experimental|1.0.0|disabled|301, 302, 304, 305|
|2147483683|PayloadSchema|`ure.preview.payload.product_job`|UnstableExtension|Experimental|1.0.0|disabled|2147483682|
|2147483684|Structure|`ure.preview.structure.product_job_info`|UnstableExtension|Experimental|1.0.0|disabled|2, 4, 27, 2147483682|
|2147483685|Structure|`ure.preview.structure.product_artifact_manifest`|UnstableExtension|Experimental|1.0.0|disabled|2, 4, 2147483682|
|2147483686|Structure|`ure.preview.structure.product_job_interface`|UnstableExtension|Experimental|1.0.0|disabled|13, 27, 35, 38, 2147483684, 2147483685, 2147483687|
|2147483687|Interface|`ure.preview.interface.product_job`|UnstableExtension|Experimental|1.0.0|disabled|2147483682, 2147483683|
|2147483688|Semantic|`ure.preview.operation.create_product_job`|UnstableExtension|Experimental|1.0.0|disabled|2147483682, 2147483683|
|2147483689|Semantic|`ure.preview.operation.start_product_job`|UnstableExtension|Experimental|1.0.0|disabled|2147483682, 2147483683|
|2147483690|Semantic|`ure.preview.operation.cancel_product_job`|UnstableExtension|Experimental|1.0.0|disabled|2147483682, 2147483683|
|2147483691|Semantic|`ure.preview.operation.acquire_product_artifact`|UnstableExtension|Experimental|1.0.0|disabled|2147483682, 2147483683|
|4026531840|Semantic|`ure.private.mock.device_loss`|Private|NotApplicable|1.0.0|disabled|403|
|4026531841|Semantic|`ure.private.mock.worker_crash`|Private|NotApplicable|1.0.0|disabled||
|4026531842|Semantic|`ure.private.mock.event_gap`|Private|NotApplicable|1.0.0|disabled|402|
|4026531843|Semantic|`ure.private.mock.backpressure`|Private|NotApplicable|1.0.0|disabled|107|
|4026531844|Structure|`ure.private.structure.conformance_interface`|Private|NotApplicable|1.0.0|disabled|4026531845|
|4026531845|Interface|`ure.private.interface.conformance`|Private|NotApplicable|1.0.0|disabled||
|4026531846|Structure|`ure.private.structure.conformance_operation_request`|Private|NotApplicable|1.0.0|disabled|1, 4026531845|
|4026531847|Structure|`ure.private.structure.conformance_frame_request`|Private|NotApplicable|1.0.0|disabled|1, 302, 4026531845|
