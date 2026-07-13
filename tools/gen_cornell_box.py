#!/usr/bin/env python3
"""Generate a Cornell Box glTF 2.0 scene (inline base64 buffer)."""

import json, struct, base64, os

def quad(pos, normal, uv_scale=(1,1)):
    """pos: 4x Vec3f, normal: Vec3f. Returns (positions, normals, uvs, indices)."""
    n = [normal] * 4
    uvs = [(0,0), (uv_scale[0],0), (uv_scale[0],uv_scale[1]), (0,uv_scale[1])]
    indices = [0, 1, 2, 0, 2, 3]
    return pos, n, uvs, indices

def box_faces(min_pt, max_pt):
    """Return 6 quads for a box (floor, ceiling, back, front, left, right)."""
    x0, y0, z0 = min_pt
    x1, y1, z1 = max_pt
    return [
        # Floor (y=y0, normal +Y)
        quad([(x0,y0,z1),(x1,y0,z1),(x1,y0,z0),(x0,y0,z0)], (0,1,0)),
        # Ceiling (y=y1, normal -Y)
        quad([(x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1)], (0,-1,0)),
        # Back wall (z=z0, normal +Z)
        quad([(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0)], (0,0,1)),
        # Front wall (z=z1, normal -Z) -- omitted for open front
        # Left wall (x=x0, normal +X)
        quad([(x0,y0,z0),(x0,y0,z1),(x0,y1,z1),(x0,y1,z0)], (1,0,0)),
        # Right wall (x=x1, normal -X)
        quad([(x1,y0,z1),(x1,y0,z0),(x1,y1,z0),(x1,y1,z1)], (-1,0,0)),
    ]

# Cornell Box room: 1x1x1, origin at (0,0,0), front open at z=1
room = box_faces((0,0,0), (1,1,1))

# Tall box (left side)
tall_box = box_faces((0.15, 0, 0.05), (0.45, 0.6, 0.45))

# Short box (right side)
short_box = box_faces((0.55, 0, 0.3), (0.85, 0.35, 0.7))

# Light panel (ceiling center, emissive)
light_panel = quad(
    [(0.35, 0.999, 0.35), (0.65, 0.999, 0.35),
     (0.65, 0.999, 0.65), (0.35, 0.999, 0.65)],
    (0, -1, 0)
)

# Mesh definitions: name, faces_list, material_index
meshes_def = [
    ("floor",       [room[0]],   0),  # white
    ("ceiling",     [room[1]],   0),  # white
    ("back_wall",   [room[2]],   0),  # white
    ("left_wall",   [room[3]],   1),  # red
    ("right_wall",  [room[4]],   2),  # green
    ("tall_box",    tall_box,    0),  # white
    ("short_box",   short_box,   0),  # white
    ("light_panel", [light_panel], 3),  # emissive
]

# Materials
materials = [
    {"name": "white_diffuse",   "albedo": (0.8, 0.8, 0.8), "emission": None},
    {"name": "red_diffuse",     "albedo": (0.8, 0.1, 0.1), "emission": None},
    {"name": "green_diffuse",   "albedo": (0.1, 0.8, 0.1), "emission": None},
    {"name": "light_emissive",  "albedo": (1.0, 1.0, 1.0), "emission": (4.0, 3.5, 3.0)},
]

def build_gltf():
    meshes_data = []  # (positions, normals, uvs, indices, name, mat_idx)

    for name, faces_list, mat_idx in meshes_def:
        positions = []
        normals = []
        uvs = []
        indices = []
        vtx = 0
        for pos, nrm, uv, idx in faces_list:
            positions.extend([v for p in pos for v in p])
            normals.extend([v for n in nrm for v in n])
            uvs.extend([v for u in uv for v in u])
            indices.extend([vtx + i for i in idx])
            vtx += 4
        meshes_data.append((positions, normals, uvs, indices, name, mat_idx))

    # Pack binary buffer: [positions..., normals..., uvs..., indices...]
    # For each mesh, we store its data contiguously.

    # Actually, glTF wants interleaved or separate bufferViews.
    # Simplest: one big buffer with all positions, then all normals, then all UVs, then all indices.
    # But accessors need per-mesh slices. Let's just do per-mesh layout:

    # Buffer layout: for each mesh: [pos, normal, uv, indices]
    buf = bytearray()
    mesh_gl = []
    nodes = []
    accessors = []
    buffer_views = []

    for mi, (pos, nrm, uv, idx, name, mat_idx) in enumerate(meshes_data):
        pos_bytes = struct.pack(f'<{len(pos)}f', *pos)
        nrm_bytes = struct.pack(f'<{len(nrm)}f', *nrm)
        uv_bytes = struct.pack(f'<{len(uv)}f', *uv)
        idx_bytes = struct.pack(f'<{len(idx)}H', *idx)

        pos_off = len(buf)
        buf.extend(pos_bytes)
        nrm_off = len(buf)
        buf.extend(nrm_bytes)
        uv_off = len(buf)
        buf.extend(uv_bytes)
        # Align indices to 2 bytes
        if len(buf) % 2 != 0:
            buf.extend(b'\x00')
        idx_off = len(buf)
        buf.extend(idx_bytes)

        vtx_count = len(pos) // 3

        # Compute bounds
        px = pos[0::3]; py = pos[1::3]; pz = pos[2::3]

        # bufferViews
        bv_pos = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": pos_off, "byteLength": len(pos_bytes), "target": 34962})
        bv_nrm = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": nrm_off, "byteLength": len(nrm_bytes), "target": 34962})
        bv_uv = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": uv_off, "byteLength": len(uv_bytes), "target": 34962})
        bv_idx = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963})

        # accessors
        a_pos = len(accessors)
        accessors.append({
            "bufferView": bv_pos, "componentType": 5126, "count": vtx_count,
            "type": "VEC3", "min": [min(px), min(py), min(pz)],
            "max": [max(px), max(py), max(pz)]
        })
        a_nrm = len(accessors)
        accessors.append({
            "bufferView": bv_nrm, "componentType": 5126, "count": vtx_count, "type": "VEC3"
        })
        a_uv = len(accessors)
        accessors.append({
            "bufferView": bv_uv, "componentType": 5126, "count": vtx_count, "type": "VEC2"
        })
        a_idx = len(accessors)
        accessors.append({
            "bufferView": bv_idx, "componentType": 5123, "count": len(idx), "type": "SCALAR"
        })

        mesh_gl.append({
            "name": name,
            "primitives": [{
                "attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv},
                "indices": a_idx,
                "material": mat_idx
            }]
        })
        nodes.append({"mesh": mi, "name": name})

    # Pad buffer to 4-byte alignment
    while len(buf) % 4 != 0:
        buf.extend(b'\x00')

    # Materials
    mats_gl = []
    for m in materials:
        mat = {
            "name": m["name"],
            "pbrMetallicRoughness": {
                "baseColorFactor": list(m["albedo"]) + [1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0
            }
        }
        if m["emission"]:
            mat["emissiveFactor"] = list(m["emission"])
        mats_gl.append(mat)

    # Camera: position (0.5, 0.5, 2.5) looking at center (0.5, 0.5, 0.5)
    # glTF camera looks down -Z of its node; node at (0.5,0.5,2.5) with identity rotation
    # already looks toward -Z which is into the box (z decreases from 2.5 toward 0.5)
    camera_node_index = len(nodes)
    nodes.append({
        "name": "camera",
        "camera": 0,
        "translation": [0.5, 0.5, 2.5]
    })

    # Scene
    scene = {
        "asset": {"version": "2.0", "generator": "UltraRender Cornell Box Generator"},
        "scene": 0,
        "scenes": [{"name": "cornell_box", "nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": mesh_gl,
        "materials": mats_gl,
        "cameras": [{
            "name": "perspective",
            "type": "perspective",
            "perspective": {
                "yfov": 0.785398,
                "aspectRatio": 1.0,
                "zfar": 100.0,
                "znear": 0.1
            }
        }],
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{
            "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(buf)).decode(),
            "byteLength": len(buf)
        }]
    }

    return scene

if __name__ == "__main__":
    scene = build_gltf()
    out_path = os.path.join(os.path.dirname(__file__), "..", "scenes", "cornell_box.gltf")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(scene, f, indent=2)
    print(f"Written: {out_path}")
    print(f"Buffer size: {len(scene['buffers'][0]['uri'])} chars (base64)")
    print(f"Meshes: {len(scene['meshes'])}, Materials: {len(scene['materials'])}")
