#!/usr/bin/env python3
"""UV選択別のGLBと四象限PNGを生成する。"""
import argparse
import json
from pathlib import Path
import struct
import zlib


PATTERN = (
    ((255, 0, 0), (0, 255, 0)),
    ((0, 0, 255), (255, 255, 255)),
)
POSITIONS = ((-0.9, -0.65, 0.0), (0.9, -0.65, 0.0),
             (0.9, 0.65, 0.0), (-0.9, 0.65, 0.0))
NORMALS = ((0.0, 0.0, -1.0),) * 4
UV0 = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))
UV1 = ((1.0, 1.0), (0.0, 1.0), (0.0, 0.0), (1.0, 0.0))
INDICES = (0, 1, 2, 0, 2, 3)


def png_chunk(kind, payload):
    """PNG chunkの長さとCRCを付ける。"""
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def make_png():
    """64x32の四象限画像を不透明RGBA PNGとして生成する。"""
    rows = bytearray()
    for y in range(32):
        rows.append(0)
        for x in range(64):
            rows.extend((*PATTERN[y // 16][x // 32], 255))
    header = struct.pack(">IIBBBBB", 64, 32, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", zlib.compress(bytes(rows))) + png_chunk(b"IEND", b""))


def append_view(binary, views, payload, target=None, byte_stride=None):
    """binary内へ4byte整列したaccessor dataを追加する。"""
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    binary.extend(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
    if target is not None:
        view["target"] = target
    if byte_stride is not None:
        view["byteStride"] = byte_stride
    views.append(view)
    return len(views) - 1


def add_accessor(accessors, view_index, component_type, count, value_type,
                 minimum=None, maximum=None, normalized=False):
    """bufferViewに対応するgltf accessorを登録する。"""
    accessor = {"bufferView": view_index, "componentType": component_type,
                "count": count, "type": value_type}
    if minimum is not None:
        accessor["min"] = minimum
    if maximum is not None:
        accessor["max"] = maximum
    if normalized:
        accessor["normalized"] = True
    accessors.append(accessor)
    return len(accessors) - 1


def pack_vectors(values, components):
    """float vector列をlittle-endian GLB payloadへ変換する。"""
    return struct.pack("<" + "f" * (len(values) * components),
                       *(value for row in values for value in row))


def make_glb(output, texture_coordinate=None, uv2=False, include_uv1=True,
             uv1_count=4, uv1_component_type=5126, normalized_uv1=False,
             texture_transform=False):
    """指定UV属性とmaterial.texCoordで一つの埋込buffer GLBを作る。"""
    binary = bytearray()
    views = []
    accessors = []

    position_view = append_view(binary, views, pack_vectors(POSITIONS, 3), 34962)
    position_accessor = add_accessor(accessors, position_view, 5126, 4, "VEC3",
                                     [-0.9, -0.65, 0.0], [0.9, 0.65, 0.0])
    normal_view = append_view(binary, views, pack_vectors(NORMALS, 3), 34962)
    normal_accessor = add_accessor(accessors, normal_view, 5126, 4, "VEC3")
    uv0_view = append_view(binary, views, pack_vectors(UV0, 2), 34962)
    uv0_accessor = add_accessor(accessors, uv0_view, 5126, 4, "VEC2", [0.0, 0.0], [1.0, 1.0])

    uv1_accessor = None
    if include_uv1:
        uv_values = UV1[:uv1_count]
        if uv1_component_type == 5126:
            uv_payload = pack_vectors(uv_values, 2)
            uv_minimum = [0.0, 0.0]
            uv_maximum = [1.0, 1.0]
        elif uv1_component_type == 5121:
            # byte VEC2を頂点属性の4byte strideへ揃える。
            uv_payload = b"".join(struct.pack("<BBxx", *(255 if value == 1.0 else 0 for value in row))
                                   for row in uv_values)
            uv_minimum = [0, 0]
            uv_maximum = [255, 255]
            uv_byte_stride = 4
        elif uv1_component_type == 5122:
            uv_payload = struct.pack("<" + "h" * (uv1_count * 2),
                                     *(32767 if value == 1.0 else 0
                                       for row in uv_values for value in row))
            uv_minimum = [0, 0]
            uv_maximum = [32767, 32767]
            uv_byte_stride = None
        else:
            uv_payload = struct.pack("<" + "H" * (uv1_count * 2),
                                     *(65535 if value == 1.0 else 0
                                       for row in uv_values for value in row))
            uv_minimum = [0, 0]
            uv_maximum = [65535, 65535]
            uv_byte_stride = None
        if uv1_component_type == 5126:
            uv_byte_stride = None
        uv1_view = append_view(binary, views, uv_payload, 34962, uv_byte_stride)
        uv1_accessor = add_accessor(accessors, uv1_view, uv1_component_type, uv1_count,
                                    "VEC2", uv_minimum, uv_maximum, normalized_uv1)

    index_view = append_view(binary, views, struct.pack("<6H", *INDICES), 34963)
    index_accessor = add_accessor(accessors, index_view, 5123, 6, "SCALAR", [0], [3])
    png = make_png()
    image_view = append_view(binary, views, png)

    attributes = {"POSITION": position_accessor, "NORMAL": normal_accessor,
                  "TEXCOORD_0": uv0_accessor}
    if include_uv1 and uv1_accessor is not None:
        attributes["TEXCOORD_1"] = uv1_accessor
        if uv2:
            attributes["TEXCOORD_2"] = uv1_accessor
    texture_view = {"index": 0}
    if texture_coordinate is not None:
        texture_view["texCoord"] = texture_coordinate
    if texture_transform:
        texture_view["extensions"] = {"KHR_texture_transform": {
            "offset": [0.1, 0.0],
            "texCoord": 1,
        }}
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
        "images": [{"bufferView": image_view, "mimeType": "image/png"}],
        "samplers": [{"wrapS": 33071, "wrapT": 33071}],
        "textures": [{"sampler": 0, "source": 0}],
        "materials": [{"pbrMetallicRoughness": {
            "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 1.0,
            "baseColorTexture": texture_view,
        }}],
        "meshes": [{"primitives": [{"attributes": attributes,
                                      "indices": index_accessor, "material": 0}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    if texture_transform:
        document["extensionsUsed"] = ["KHR_texture_transform"]
        document["extensionsRequired"] = ["KHR_texture_transform"]
    json_payload = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total_length = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total_length) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.write_bytes(glb)


def generate_fixtures(directory):
    """標準GLBとUV選択・欠落・形式異常のfixture群を書き出す。"""
    directory.mkdir(parents=True, exist_ok=True)
    make_glb(directory / "default.glb")
    make_glb(directory / "uv0.glb", texture_coordinate=0)
    make_glb(directory / "uv1.glb", texture_coordinate=1)
    make_glb(directory / "uv2.glb", texture_coordinate=2, uv2=True)
    make_glb(directory / "missing-selected.glb", texture_coordinate=1, include_uv1=False)
    make_glb(directory / "negative-selected.glb", texture_coordinate=-1)
    make_glb(directory / "wrong-selected-count.glb", texture_coordinate=1, uv1_count=3)
    make_glb(directory / "normalized-uv1.glb", texture_coordinate=1, uv1_component_type=5123, normalized_uv1=True)
    make_glb(directory / "normalized-byte-uv1.glb", texture_coordinate=1,
             uv1_component_type=5121, normalized_uv1=True)
    make_glb(directory / "unnormalized-uv1.glb", texture_coordinate=1,
             uv1_component_type=5123, normalized_uv1=False)
    make_glb(directory / "signed-uv1.glb", texture_coordinate=1,
             uv1_component_type=5122, normalized_uv1=True)
    make_glb(directory / "float-normalized-uv1.glb", texture_coordinate=1,
             uv1_component_type=5126, normalized_uv1=True)
    make_glb(directory / "transformed-selected.glb", texture_coordinate=0,
             texture_transform=True)


def main():
    """指定したdirectoryへGLB texture-coordinate fixture群を生成する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    generate_fixtures(args.output_dir.resolve())


if __name__ == "__main__":
    main()
