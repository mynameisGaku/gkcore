#!/usr/bin/env python3
"""Generate the small, self-contained GLB used by the lighting example."""

import json
import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "examples" / "assets" / "model_lighting.glb"


def make_sphere(center_x, segments=24, rings=16):
    positions = []
    normals = []
    texcoords = []
    indices = []
    for ring in range(rings + 1):
        v = ring / rings
        phi = math.pi * v
        for segment in range(segments + 1):
            u = segment / segments
            theta = 2.0 * math.pi * u
            nx = math.sin(phi) * math.cos(theta)
            ny = math.cos(phi)
            nz = math.sin(phi) * math.sin(theta)
            positions.extend((center_x + nx, ny, nz))
            normals.extend((nx, ny, nz))
            texcoords.extend((u, v))
    for ring in range(rings):
        for segment in range(segments):
            a = ring * (segments + 1) + segment
            b = a + segments + 1
            indices.extend((a, a + 1, b, a + 1, b + 1, b))
    return positions, normals, texcoords, indices


binary = bytearray()
buffer_views = []
accessors = []


def add_accessor(values, component_type, accessor_type, *, target, bounds=None):
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    if component_type == 5126:
        binary.extend(struct.pack("<" + "f" * len(values), *values))
        size = 4
    else:
        binary.extend(struct.pack("<" + "H" * len(values), *values))
        size = 2
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}[accessor_type]
    count = len(values) // components
    buffer_views.append({
        "buffer": 0,
        "byteOffset": offset,
        "byteLength": len(values) * size,
        "target": target,
    })
    accessor = {
        "bufferView": len(buffer_views) - 1,
        "componentType": component_type,
        "count": count,
        "type": accessor_type,
    }
    if bounds is not None:
        accessor.update(bounds)
    accessors.append(accessor)
    return len(accessors) - 1


primitives = []
for center_x, material in ((-1.15, 0), (1.15, 1)):
    positions, normals, texcoords, indices = make_sphere(center_x)
    pos_accessor = add_accessor(
        positions, 5126, "VEC3", target=34962,
        bounds={"min": [center_x - 1, -1, -1], "max": [center_x + 1, 1, 1]},
    )
    normal_accessor = add_accessor(normals, 5126, "VEC3", target=34962)
    uv_accessor = add_accessor(texcoords, 5126, "VEC2", target=34962)
    index_accessor = add_accessor(indices, 5123, "SCALAR", target=34963,
                                  bounds={"min": [0], "max": [(17 * 25) - 1]})
    primitives.append({
        "attributes": {"POSITION": pos_accessor, "NORMAL": normal_accessor, "TEXCOORD_0": uv_accessor},
        "indices": index_accessor,
        "material": material,
    })

gltf = {
    "asset": {"version": "2.0", "generator": "gkcore lighting sample"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0}],
    "meshes": [{"primitives": primitives}],
    "materials": [
        {"name": "Warm dielectric, smooth", "pbrMetallicRoughness": {
            "baseColorFactor": [0.88, 0.38, 0.10, 1.0],
            "metallicFactor": 0.0, "roughnessFactor": 0.22}},
        {"name": "Cool metal, rougher", "pbrMetallicRoughness": {
            "baseColorFactor": [0.08, 0.48, 0.85, 1.0],
            "metallicFactor": 1.0, "roughnessFactor": 0.38}},
    ],
    "buffers": [{"byteLength": 0}],
    "bufferViews": buffer_views,
    "accessors": accessors,
}
gltf["buffers"][0]["byteLength"] = len(binary)
json_bytes = json.dumps(gltf, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
while len(json_bytes) % 4:
    json_bytes += b" "
while len(binary) % 4:
    binary.append(0)
total = 12 + 8 + len(json_bytes) + 8 + len(binary)
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
with OUTPUT.open("wb") as glb:
    glb.write(struct.pack("<4sII", b"glTF", 2, total))
    glb.write(struct.pack("<I4s", len(json_bytes), b"JSON"))
    glb.write(json_bytes)
    glb.write(struct.pack("<I4s", len(binary), b"BIN\x00"))
    glb.write(binary)
