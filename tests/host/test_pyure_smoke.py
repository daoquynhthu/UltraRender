import pyure
import tempfile
import time
from pathlib import Path


def wait_until(predicate, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.01)
    return predicate()


def main() -> int:
    pyure.set_min_log_level(pyure.LogLevel.WARN)

    assert pyure.aov_channel_count(pyure.AovType.BEAUTY) == 3
    assert pyure.aov_channel_count(pyure.AovType.NORMAL) == 3
    assert pyure.aov_channel_count(pyure.AovType.ALBEDO) == 3
    assert pyure.aov_channel_count(pyure.AovType.DEPTH) == 1
    assert pyure.aov_channel_count(pyure.AovType.UV) == 2
    assert pyure.aov_channel_count(pyure.AovType.MOTION_VECTOR) == 2

    with pyure.create_session(num_wavelengths=8, queue_capacity=64, max_trace_depth=12) as session:
        progress = session.progress()
        assert progress.spp == 0
        assert progress.state == pyure.SessionState.EMPTY
        assert not progress.has_scene
        try:
            session.render_pass()
        except RuntimeError:
            pass
        else:
            raise AssertionError("render_pass without a scene must fail")

    with pyure.create_session(domain_bins=1000000, packet_lanes=1, queue_capacity=64, max_trace_depth=12) as session:
        progress = session.progress()
        assert progress.spp == 0
        assert progress.state == pyure.SessionState.EMPTY
        assert not progress.has_scene

    with pyure.create_session(
        domain_bins=1000000,
        packet_lanes=8,
        queue_capacity=64,
        max_trace_depth=12,
        allow_wave_preview_degradation=True,
    ) as session:
        progress = session.progress()
        assert progress.spp == 0
        assert progress.state == pyure.SessionState.EMPTY
        assert not progress.has_scene

    with pyure.create_session(
        num_wavelengths=8,
        queue_capacity=64,
        max_trace_depth=12,
        integrator_mode="path_guided",
        integrator_sampler="low_discrepancy",
        integrator_quality_preset="final",
        path_guiding=True,
    ) as session:
        progress = session.progress()
        assert progress.spp == 0
        assert progress.state == pyure.SessionState.EMPTY
        assert not progress.has_scene

    try:
        pyure.create_session(
            domain_bins=1000000,
            packet_lanes=8,
            queue_capacity=64,
            max_trace_depth=12,
            wave_optics_mode="coherent_field",
            coherent_field=True,
        )
    except RuntimeError:
        pass
    else:
        raise AssertionError("unimplemented coherent wave optics mode must fail")

    try:
        pyure.create_session(wave_optics_mode="invalid")
    except ValueError:
        pass
    else:
        raise AssertionError("unknown wave optics mode must fail")

    try:
        pyure.create_session(integrator_mode="invalid")
    except ValueError:
        pass
    else:
        raise AssertionError("unknown integrator mode must fail")

    scene_text = """{
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
      "indices": 3,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "graph_mat",
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 0.1, 0.1, 1.0],
      "roughnessFactor": 0.4
    }
  }],
  "buffers": [{
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA",
    "byteLength": 102
  }],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ]
}
"""
    with tempfile.TemporaryDirectory() as tmp:
        scene_path = Path(tmp) / "pyure_smoke.gltf"
        scene_path.write_text(scene_text, encoding="utf-8")
        with pyure.create_session(num_wavelengths=8, queue_capacity=256, max_trace_depth=8) as session:
            session.load_scene_file(scene_path)
            progress = session.progress()
            assert progress.has_scene
            assert progress.state == pyure.SessionState.READY
            assert session.framebuffer_size() == (8, 8)
            session.start(progressive=True)
            assert session.progress().state == pyure.SessionState.RUNNING
            assert wait_until(lambda: session.progress().spp >= 1)
            framebuffer = session.framebuffer()
            assert len(framebuffer) == 8 * 8 * 3
            hdr_path = Path(tmp) / "pyure_smoke.hdr"
            session.save_hdr(hdr_path)
            assert hdr_path.read_bytes().startswith(b"#?RADIANCE\n")
            depth = session.aov(pyure.AovType.DEPTH)
            assert len(depth) == 8 * 8
            motion = session.aov(pyure.AovType.MOTION_VECTOR)
            assert len(motion) == 8 * 8 * 2
            session.pause()
            assert session.progress().state == pyure.SessionState.PAUSED
            paused_spp = session.progress().spp
            time.sleep(0.05)
            assert session.progress().spp == paused_spp
            session.resume()
            assert session.progress().state == pyure.SessionState.RUNNING
            assert wait_until(lambda: session.progress().spp > paused_spp)

            session.update_camera((0.0, 0.0, 6.0), (0.0, 0.0, 0.0), 45.0)
            progress = session.progress()
            assert progress.state == pyure.SessionState.READY
            assert progress.spp == 0
            assert session.render_pass() >= 1

            try:
                session.update_material(
                    0,
                    pyure.MaterialType.LAMBERTIAN,
                    albedo=(0.2, 0.8, 0.3),
                    roughness=0.4,
                    ior=1.45,
                )
            except RuntimeError:
                pass
            else:
                raise AssertionError("MaterialGraph material mutation must require scene reload")

            session.load_scene_file(scene_path)
            progress = session.progress()
            assert progress.state == pyure.SessionState.READY
            assert progress.spp == 0
            assert session.render_pass() >= 1

            try:
                session.update_material_texture(0, 1, 1, [0.1, 0.6, 0.9])
            except RuntimeError:
                pass
            else:
                raise AssertionError("raw texture mutation must fail")

            try:
                session.update_instance_transform(999, (0.0, 0.0, 0.0))
            except RuntimeError:
                pass
            else:
                raise AssertionError("invalid instance mutation must fail")

            session.start(progressive=True)
            assert wait_until(lambda: session.progress().state == pyure.SessionState.RUNNING)
            assert wait_until(lambda: session.progress().spp >= 1)
            session.cancel()
            assert session.progress().state == pyure.SessionState.CANCELED
            canceled_spp = session.progress().spp
            time.sleep(0.05)
            assert session.progress().spp == canceled_spp
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
