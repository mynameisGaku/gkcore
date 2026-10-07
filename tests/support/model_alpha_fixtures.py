#!/usr/bin/env python3
"""glTF alpha modeとbase color alphaを検証するGLB fixtureを生成する。"""
import argparse
import json
from pathlib import Path
import struct
import zlib


POSITIONS = ((-0.9, -0.65, 0.0), (0.9, -0.65, 0.0),
             (0.9, 0.65, 0.0), (-0.9, 0.65, 0.0))
NORMALS = ((0.0, 0.0, -1.0),) * 4
UVS = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))
INDICES = (0, 1, 2, 0, 2, 3)
ALPHA_VALUES = (0, 64, 128, 255)


def png_chunk(kind, payload):
    """PNG chunkを長さとCRC付きで作る。"""
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload)))


def make_alpha_png():
    """alpha値を横4 stripeに分けた64x32 RGBA PNGを作る。"""
    rows = bytearray()
    for _ in range(32):
        rows.append(0)
        for alpha in ALPHA_VALUES:
            rows.extend(bytes((255, 0, 0, alpha)) * 16)
    header = struct.pack(">IIBBBBB", 64, 32, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", zlib.compress(bytes(rows))) +
            png_chunk(b"IEND", b""))


def append_view(binary, views, payload, target=None):
    """buffer内へ4byte整列したpayloadを追加し、そのview番号を返す。"""
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    binary.extend(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
    if target is not None:
        view["target"] = target
    views.append(view)
    return len(views) - 1


def add_accessor(accessors, view_index, component_type, count, value_type,
                 minimum=None, maximum=None):
    """bufferViewを参照するaccessorを登録する。"""
    accessor = {"bufferView": view_index, "componentType": component_type,
                "count": count, "type": value_type}
    if minimum is not None:
        accessor["min"] = minimum
    if maximum is not None:
        accessor["max"] = maximum
    accessors.append(accessor)
    return len(accessors) - 1


def pack_vectors(values, components):
    """float vector列をlittle-endian binaryへ変換する。"""
    return struct.pack("<" + "f" * (len(values) * components),
                       *(value for row in values for value in row))


def make_material(alpha_mode=None, cutoff=None, base_alpha=1.0, texture=True):
    """alpha設定と任意の基本色画像参照を持つmaterialを作る。"""
    pbr = {"baseColorFactor": [1.0, 1.0, 1.0, base_alpha],
           "metallicFactor": 0.0, "roughnessFactor": 1.0}
    if texture:
        pbr["baseColorTexture"] = {"index": 0}
    material = {"pbrMetallicRoughness": pbr}
    if alpha_mode is not None:
        material["alphaMode"] = alpha_mode
    if cutoff is not None:
        material["alphaCutoff"] = cutoff
    return material


def make_glb(output, materials, material_indices=None, include_texture=True):
    """quad、任意数のmaterial、埋込alpha PNGを持つGLB 2.0を作る。"""
    binary = bytearray()
    views = []
    accessors = []
    position_view = append_view(binary, views, pack_vectors(POSITIONS, 3), 34962)
    position_accessor = add_accessor(accessors, position_view, 5126, 4, "VEC3",
                                     [-0.9, -0.65, 0.0], [0.9, 0.65, 0.0])
    normal_view = append_view(binary, views, pack_vectors(NORMALS, 3), 34962)
    normal_accessor = add_accessor(accessors, normal_view, 5126, 4, "VEC3")
    uv_view = append_view(binary, views, pack_vectors(UVS, 2), 34962)
    uv_accessor = add_accessor(accessors, uv_view, 5126, 4, "VEC2", [0.0, 0.0], [1.0, 1.0])
    index_view = append_view(binary, views, struct.pack("<6H", *INDICES), 34963)
    index_accessor = add_accessor(accessors, index_view, 5123, 6, "SCALAR", [0], [3])

    images = []
    textures = []
    samplers = []
    if include_texture:
        image_view = append_view(binary, views, make_alpha_png())
        images.append({"bufferView": image_view, "mimeType": "image/png"})
        textures.append({"sampler": 0, "source": 0})
        samplers.append({"wrapS": 33071, "wrapT": 33071})

    attributes = {"POSITION": position_accessor, "NORMAL": normal_accessor,
                  "TEXCOORD_0": uv_accessor}
    primitive_materials = material_indices or [0]
    primitives = [{"attributes": attributes, "indices": index_accessor,
                   "material": material_index}
                  for material_index in primitive_materials]
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
        "materials": materials,
        "meshes": [{"primitives": primitives}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    if include_texture:
        document["images"] = images
        document["textures"] = textures
        document["samplers"] = samplers
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total_length = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total_length) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.write_bytes(glb)


def generate_fixtures(directory):
    """正常・境界・拒否対象のalpha fixture群を書き出す。"""
    directory.mkdir(parents=True, exist_ok=True)
    cases = {
        "opaque-default.glb": ([make_material()], [0], True),
        "opaque.glb": ([make_material("OPAQUE")], [0], True),
        "opaque-cutoff-two.glb": ([make_material("OPAQUE", 2.0)], [0], True),
        "mask-default.glb": ([make_material("MASK")], [0], True),
        "mask-zero.glb": ([make_material("MASK", 0.0)], [0], True),
        "mask-one.glb": ([make_material("MASK", 1.0)], [0], True),
        "mask-two.glb": ([make_material("MASK", 2.0)], [0], True),
        "mask-factor-half.glb": ([make_material("MASK", base_alpha=0.5)], [0], True),
        "mask-factor-quarter.glb": ([make_material("MASK", base_alpha=0.25)], [0], True),
        "mask-equality.glb": ([make_material("MASK", 128.0 / 255.0)], [0], True),
        "mask-no-texture.glb": ([make_material("MASK", base_alpha=0.25, texture=False)], [0], False),
        "mixed-materials.glb": ([make_material("OPAQUE"), make_material("MASK"),
                                  make_material("MASK", 1.0)], [0, 1, 2], True),
        "blend.glb": ([make_material("BLEND")], [0], True),
        "negative-cutoff.glb": ([make_material("MASK", -0.1)], [0], True),
        "nonfinite-cutoff.glb": ([make_material("MASK", 1.0e100)], [0], True),
    }
    for filename, (materials, material_indices, include_texture) in cases.items():
        make_glb(directory / filename, materials, material_indices, include_texture)
    return tuple(directory / filename for filename in cases)


def main():
    """指定directoryへalpha mode検証用GLBを生成する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    generate_fixtures(args.output_dir.resolve())


if __name__ == "__main__":
    main()
