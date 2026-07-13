#!/usr/bin/env python3
"""Generate a product-level showcase glTF 2.0 scene for UltraRender."""

import json, struct, base64, os, math

def quad(min_pt, max_pt, y, normal):
    x0, z0 = min_pt
    x1, z1 = max_pt
    positions = [
        x0, y, z1,
        x1, y, z1,
        x1, y, z0,
        x0, y, z0,
    ]
    normals = list(normal) * 4
    uvs = [0, 0, 1, 0, 1, 1, 0, 1]
    indices = [0, 1, 2, 0, 2, 3]
    return positions, normals, uvs, indices

def wall(x, y_range, z_range, axis, sign):
    """axis: 'x' or 'z', sign: +1 or -1."""
    y0, y1 = y_range
    z0, z1 = z_range
    if axis == 'x':
        x = sign * 2.0
        n = (-sign, 0, 0)
        positions = [
            x, y0, z1,
            x, y0, z0,
            x, y1, z0,
            x, y1, z1,
        ]
    else:
        z = sign * 2.5
        n = (0, 0, -sign)
        positions = [
            -2.0, y0, z,
            2.0, y0, z,
            2.0, y1, z,
            -2.0, y1, z,
        ]
    normals = list(n) * 4
    uvs = [0, 0, 1, 0, 1, 1, 0, 1]
    indices = [0, 1, 2, 0, 2, 3]
    return positions, normals, uvs, indices

def make_box(min_pt, max_pt):
    """Return 6 quads for a box."""
    x0, y0, z0 = min_pt
    x1, y1, z1 = max_pt
    faces = []
    # Floor (y=y0, normal +Y)
    faces.append(quad((x0, z1), (x1, z0), y0, (0, 1, 0)))
    # Ceiling (y=y1, normal -Y)
    faces.append(quad((x0, z0), (x1, z1), y1, (0, -1, 0)))
    # Back wall (z=z0, normal +Z)
    n = (0, 0, 1)
    positions = [x0,y0,z0, x1,y0,z0, x1,y1,z0, x0,y1,z0]
    normals = list(n) * 4
    uvs = [0,0, 1,0, 1,1, 0,1]
    indices = [0,1,2, 0,2,3]
    faces.append((positions, normals, uvs, indices))
    # Left wall (x=x0, normal +X)
    n = (1, 0, 0)
    positions = [x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0]
    normals = list(n) * 4
    uvs = [0,0, 1,0, 1,1, 0,1]
    indices = [0,1,2, 0,2,3]
    faces.append((positions, normals, uvs, indices))
    # Right wall (x=x1, normal -X)
    n = (-1, 0, 0)
    positions = [x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1]
    normals = list(n) * 4
    uvs = [0,0, 1,0, 1,1, 0,1]
    indices = [0,1,2, 0,2,3]
    faces.append((positions, normals, uvs, indices))
    return faces

def make_sphere(cx, cy, cz, r, slices=24, stacks=12):
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

    for mi, (name, geo_list, mat_idx) in enumerate(meshes_def):
        vtx_offset = len(buf)
        mesh_positions = []
        mesh_normals = []
        mesh_uvs = []
        mesh_indices = []
        base_vertex = 0

        for pos, nrm, uv, idx in geo_list:
            mesh_positions.extend(pos)
            mesh_normals.extend(nrm)
            mesh_uvs.extend(uv)
            mesh_indices.extend([base_vertex + i for i in idx])
            base_vertex += len(pos) // 3

        pos_bytes = struct.pack(f'<{len(mesh_positions)}f', *mesh_positions)
        nrm_bytes = struct.pack(f'<{len(mesh_normals)}f', *mesh_normals)
        uv_bytes = struct.pack(f'<{len(mesh_uvs)}f', *mesh_uvs)
        if len(buf) % 2 != 0:
            buf.extend(b'\x00')
        idx_bytes = struct.pack(f'<{len(mesh_indices)}H', *mesh_indices)

        pos_off = len(buf); buf.extend(pos_bytes)
        nrm_off = len(buf); buf.extend(nrm_bytes)
        uv_off = len(buf); buf.extend(uv_bytes)
        idx_off = len(buf); buf.extend(idx_bytes)

        vtx_count = len(mesh_positions) // 3
        px = mesh_positions[0::3]; py = mesh_positions[1::3]; pz = mesh_positions[2::3]

        bv_pos = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": pos_off, "byteLength": len(pos_bytes), "target": 34962})
        bv_nrm = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": nrm_off, "byteLength": len(nrm_bytes), "target": 34962})
        bv_uv = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": uv_off, "byteLength": len(uv_bytes), "target": 34962})
        bv_idx = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963})

        a_pos = len(accessors)
        accessors.append({"bufferView": bv_pos, "componentType": 5126, "count": vtx_count,
                          "type": "VEC3", "min": [min(px), min(py), min(pz)],
                          "max": [max(px), max(py), max(pz)]})
        a_nrm = len(accessors)
        accessors.append({"bufferView": bv_nrm, "componentType": 5126, "count": vtx_count, "type": "VEC3"})
        a_uv = len(accessors)
        accessors.append({"bufferView": bv_uv, "componentType": 5126, "count": vtx_count, "type": "VEC2"})
        a_idx = len(accessors)
        accessors.append({"bufferView": bv_idx, "componentType": 5123, "count": len(mesh_indices), "type": "SCALAR"})

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

    # Camera: slightly elevated, gentle downward angle
    # Negative X quaternion = rotation around X tilts -Z downward
    nodes.append({
        "name": "camera",
        "camera": 0,
        "translation": [0.0, 1.1, 6.0],
        "rotation": [-0.035, 0, 0, 0.999]
    })

    return {
        "asset": {"version": "2.0", "generator": "UltraRender Showcase Generator"},
        "scene": 0,
        "scenes": [{"name": "showcase", "nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": mesh_gl,
        "materials": mats_gl,
        "cameras": [{
            "name": "product_camera",
            "type": "perspective",
            "perspective": {
                "yfov": 0.698132,  # 40 degrees in radians
                "aspectRatio": 1.6,
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

def scene():
    meshes = []
    mat_idx = 0

    # --- Room (dark enclosure) ---
    # Floor: large dark grey plane at y=0
    floor = quad((-2.0, 2.5), (2.0, -2.5), 0.0, (0, 1, 0))
    meshes.append(("floor", [floor], 0))

    # Back wall (z = -2.5, normal +Z)
    n = (0, 0, 1)
    positions = [-2.0,0,-2.5, 2.0,0,-2.5, 2.0,2.0,-2.5, -2.0,2.0,-2.5]
    normals = list(n) * 4
    uvs = [0,0, 1,0, 1,1, 0,1]
    indices = [0,1,2, 0,2,3]
    meshes.append(("back_wall", [(positions, normals, uvs, indices)], 0))

    # Left wall (x = -2.0, normal +X)
    n = (1, 0, 0)
    positions = [-2.0,0,-2.5, -2.0,0,2.5, -2.0,2.0,2.5, -2.0,2.0,-2.5]
    normals = list(n) * 4
    uvs = [0,0, 1,0, 1,1, 0,1]
    indices = [0,1,2, 0,2,3]
    meshes.append(("left_wall", [(positions, normals, uvs, indices)], 0))

    # Right wall (x = 2.0, normal -X)
    n = (-1, 0, 0)
    positions = [2.0,0,2.5, 2.0,0,-2.5, 2.0,2.0,-2.5, 2.0,2.0,2.5]
    normals = list(n) * 4
    uvs = [0,0, 1,0, 1,1, 0,1]
    indices = [0,1,2, 0,2,3]
    meshes.append(("right_wall", [(positions, normals, uvs, indices)], 0))

    # Ceiling (y = 2.0, normal -Y)
    n = (0, -1, 0)
    positions = [-2.0,2.0,-2.5, 2.0,2.0,-2.5, 2.0,2.0,2.5, -2.0,2.0,2.5]
    normals = list(n) * 4
    uvs = [0,0, 1,0, 1,1, 0,1]
    indices = [0,1,2, 0,2,3]
    meshes.append(("ceiling", [(positions, normals, uvs, indices)], 0))

    # --- Light panel (emissive, ceiling center) ---
    light = quad((-0.8, 1.0), (0.8, -1.0), 1.999, (0, -1, 0))
    meshes.append(("light_panel", [light], 1))

    # --- Metal cube (left-center, gold-like) ---
    cube_faces = make_box((-0.7, 0.0, -0.5), (-0.1, 0.6, 0.1))
    meshes.append(("metal_cube", cube_faces, 2))

    # --- White sphere (right-center) ---
    sphere_geo = make_sphere(0.6, 0.35, -0.2, 0.35, slices=28, stacks=14)
    meshes.append(("white_sphere", [sphere_geo], 3))

    # --- Red matte cube (far right) ---
    red_cube_faces = make_box((1.0, 0.0, -0.8), (1.6, 0.5, -0.2))
    meshes.append(("red_cube", red_cube_faces, 4))

    # --- Small metal sphere (front-left) ---
    small_sphere = make_sphere(-0.3, 0.2, 0.8, 0.2, slices=24, stacks=12)
    meshes.append(("small_metal_sphere", [small_sphere], 5))

    materials = [
        {"name": "dark_grey",      "albedo": (0.15, 0.15, 0.15), "metallic": 0.0, "roughness": 0.95},
        {"name": "light_panel",    "albedo": (1.0, 1.0, 1.0),    "emission": (12.0, 11.0, 10.0)},
        {"name": "gold_metal",     "albedo": (1.0, 0.85, 0.5),   "metallic": 0.95, "roughness": 0.03},
        {"name": "white_diffuse",  "albedo": (0.85, 0.85, 0.85), "metallic": 0.0, "roughness": 0.6},
        {"name": "red_matte",      "albedo": (0.7, 0.08, 0.08),  "metallic": 0.0, "roughness": 0.8},
        {"name": "polished_metal", "albedo": (0.9, 0.9, 0.92),   "metallic": 0.98, "roughness": 0.01},
    ]

    return build_scene(meshes, materials)

if __name__ == "__main__":
    scene_data = scene()
    out_path = os.path.join(os.path.dirname(__file__), "..", "scenes", "showcase.gltf")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(scene_data, f, indent=2)
    print(f"Written: {out_path}")
    print(f"Buffer size: {len(scene_data['buffers'][0]['uri'])} chars (base64)")
    print(f"Meshes: {len(scene_data['meshes'])}, Materials: {len(scene_data['materials'])}")
