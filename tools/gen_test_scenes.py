#!/usr/bin/env python3
"""Generate minimal test glTF scenes for UltraRender debugging."""

import json, struct, base64, os, math

def make_quad(min_pt, max_pt, y=0.0, normal=(0,1,0)):
    """Horizontal quad at y height, facing up."""
    x0, _, z0 = min_pt
    x1, _, z1 = max_pt
    positions = [
        x0, y, z1,
        x1, y, z1,
        x1, y, z0,
        x0, y, z0,
    ]
    normals = list(normal) * 4
    uvs = [0,0, 1,0, 1,1, 0,1]
    indices = [0, 1, 2, 0, 2, 3]
    return positions, normals, uvs, indices

def make_sphere(cx, cy, cz, r, slices=16, stacks=8):
    """UV sphere centered at (cx,cy,cz) with radius r."""
    positions = []
    normals = []
    uvs = []
    indices = []

    for stack in range(stacks + 1):
        phi = math.pi * stack / stacks
        for sl in range(slices + 1):
            theta = 2 * math.pi * sl / slices
            nx = math.sin(phi) * math.cos(theta)
            ny = math.cos(phi)
            nz = math.sin(phi) * math.sin(theta)
            positions.extend([cx + r * nx, cy + r * ny, cz + r * nz])
            normals.extend([nx, ny, nz])
            uvs.extend([sl / slices, stack / stacks])

    for stack in range(stacks):
        for sl in range(slices):
            a = stack * (slices + 1) + sl
            b = a + slices + 1
            indices.extend([a, b, a + 1, a + 1, b, b + 1])

    return positions, normals, uvs, indices

def build_scene(meshes_def, materials):
    buf = bytearray()
    mesh_gl = []
    nodes = []
    accessors = []
    buffer_views = []

    for mi, (name, geo, mat_idx) in enumerate(meshes_def):
        pos, nrm, uv, idx = geo

        pos_bytes = struct.pack(f'<{len(pos)}f', *pos)
        nrm_bytes = struct.pack(f'<{len(nrm)}f', *nrm)
        uv_bytes = struct.pack(f'<{len(uv)}f', *uv)

        # Align indices to 2 bytes
        if len(buf) % 2 != 0:
            buf.extend(b'\x00')
        idx_bytes = struct.pack(f'<{len(idx)}H', *idx)

        pos_off = len(buf); buf.extend(pos_bytes)
        nrm_off = len(buf); buf.extend(nrm_bytes)
        uv_off = len(buf); buf.extend(uv_bytes)
        idx_off = len(buf); buf.extend(idx_bytes)

        vtx_count = len(pos) // 3
        px = pos[0::3]; py = pos[1::3]; pz = pos[2::3]

        bv_pos = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": pos_off, "byteLength": len(pos_bytes), "target": 34962})
        bv_nrm = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": nrm_off, "byteLength": len(nrm_bytes), "target": 34962})
        bv_uv = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": uv_off, "byteLength": len(uv_bytes), "target": 34962})
        bv_idx = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963})

        a_pos = len(accessors)
        accessors.append({
            "bufferView": bv_pos, "componentType": 5126, "count": vtx_count,
            "type": "VEC3", "min": [min(px), min(py), min(pz)],
            "max": [max(px), max(py), max(pz)]
        })
        a_nrm = len(accessors)
        accessors.append({"bufferView": bv_nrm, "componentType": 5126, "count": vtx_count, "type": "VEC3"})
        a_uv = len(accessors)
        accessors.append({"bufferView": bv_uv, "componentType": 5126, "count": vtx_count, "type": "VEC2"})
        a_idx = len(accessors)
        accessors.append({"bufferView": bv_idx, "componentType": 5123, "count": len(idx), "type": "SCALAR"})

        mesh_gl.append({
            "name": name,
            "primitives": [{
                "attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv},
                "indices": a_idx, "material": mat_idx
            }]
        })
        nodes.append({"mesh": mi, "name": name})

    while len(buf) % 4 != 0:
        buf.extend(b'\x00')

    mats_gl = []
    for m in materials:
        mat = {
            "name": m["name"],
            "pbrMetallicRoughness": {
                "baseColorFactor": list(m["albedo"]) + [1.0],
                "metallicFactor": m.get("metallic", 0.0),
                "roughnessFactor": m.get("roughness", 1.0)
            }
        }
        if m.get("emission"):
            mat["emissiveFactor"] = list(m["emission"])
        mats_gl.append(mat)

    return {
        "asset": {"version": "2.0", "generator": "UltraRender Test Generator"},
        "scene": 0,
        "scenes": [{"name": "test", "nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": mesh_gl,
        "materials": mats_gl,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{
            "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(buf)).decode(),
            "byteLength": len(buf)
        }]
    }

def scene_simple_plane():
    """Simple white plane facing up."""
    geo = make_quad((-2, 0, -2), (2, 0, 2))
    mats = [{"name": "white", "albedo": (0.8, 0.8, 0.8)}]
    return build_scene([("plane", geo, 0)], mats)

def scene_plane_glass_sphere():
    """White plane + glass-like sphere on top."""
    plane_geo = make_quad((-3, 0, -3), (3, 0, 3))
    sphere_geo = make_sphere(0, 1.0, 0, 1.0, slices=24, stacks=12)
    mats = [
        {"name": "white_diffuse", "albedo": (0.8, 0.8, 0.8)},
        {"name": "glass", "albedo": (0.9, 0.9, 0.9), "metallic": 0.0, "roughness": 0.05},
    ]
    return build_scene([
        ("plane", plane_geo, 0),
        ("sphere", sphere_geo, 1),
    ], mats)

if __name__ == "__main__":
    scenes_dir = os.path.join(os.path.dirname(__file__), "..", "scenes")

    # Scene 1: Simple plane
    s1 = scene_simple_plane()
    with open(os.path.join(scenes_dir, "test_plane.gltf"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(s1, f, indent=2)
    print("Written: test_plane.gltf")

    # Scene 2: Plane + glass sphere
    s2 = scene_plane_glass_sphere()
    with open(os.path.join(scenes_dir, "test_plane_sphere.gltf"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(s2, f, indent=2)
    print("Written: test_plane_sphere.gltf")
