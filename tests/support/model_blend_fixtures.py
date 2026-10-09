#!/usr/bin/env python3
"""BLEND、MASK、OPAQUEの画像検査に使う小さなGLBを生成する。"""

import argparse
import json
import math
from pathlib import Path
import struct
import zlib


def _chunk(kind, payload):
    """PNG chunkに長さとCRCを付ける。"""
    data = kind + payload
    return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF)


def _png(width, height, rgba):
    """RGBA画素列から最小構成のPNGを組み立てる。"""
    rows = b"".join(b"\0" + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", header) +
            _chunk(b"IDAT", zlib.compress(rows)) + _chunk(b"IEND", b""))


def _write_glb(path, vertices, uvs, indices, factor, alpha_mode,
               texture_rgba=None, texture_size=None, node_translation=(0, 0, 0),
               normals=None):
    """頂点、材質、必要に応じて埋込PNGを含むGLBを保存する。"""
    binary = bytearray()
    views = []
    accessors = []

    def add_accessor(values, components, kind, component_type=5126, target=None):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        code = "f" if component_type == 5126 else "H"
        binary.extend(struct.pack("<" + code * len(values), *values))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(binary) - offset}
        if target is not None:
            view["target"] = target
        views.append(view)
        accessor = {"bufferView": len(views) - 1, "componentType": component_type,
                    "count": len(values) // components, "type": kind}
        if kind == "VEC3":
            accessor["min"] = [min(values[axis::3]) for axis in range(3)]
            accessor["max"] = [max(values[axis::3]) for axis in range(3)]
        accessors.append(accessor)
        return len(accessors) - 1

    positions = add_accessor(tuple(value for vertex in vertices for value in vertex), 3, "VEC3", target=34962)
    texcoords = add_accessor(tuple(value for uv in uvs for value in uv), 2, "VEC2", target=34962)
    index_accessor = add_accessor(indices, 1, "SCALAR", 5123, 34963)
    attributes = {"POSITION": positions, "TEXCOORD_0": texcoords}
    if normals is not None:
        attributes["NORMAL"] = add_accessor(tuple(value for normal in normals for value in normal), 3, "VEC3", target=34962)
    primitive = {"attributes": attributes,
                 "indices": index_accessor, "material": 0}
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": 0}],
        "bufferViews": views,
        "accessors": accessors,
        "materials": [{"pbrMetallicRoughness": {"baseColorFactor": list(factor),
                                                   "metallicFactor": 0.0,
                                                   "roughnessFactor": 1.0},
                       "alphaMode": alpha_mode}],
        "meshes": [{"primitives": [primitive]}],
        "nodes": [{"mesh": 0, "translation": list(node_translation)}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    if texture_rgba is not None:
        width, height = texture_size
        png_data = _png(width, height, texture_rgba)
        while len(binary) % 4:
            binary.append(0)
        image_offset = len(binary)
        binary.extend(png_data)
        views.append({"buffer": 0, "byteOffset": image_offset, "byteLength": len(png_data)})
        document["images"] = [{"bufferView": len(views) - 1, "mimeType": "image/png"}]
        document["samplers"] = [{"magFilter": 9728, "minFilter": 9728,
                                 "wrapS": 33071, "wrapT": 33071}]
        document["textures"] = [{"source": 0, "sampler": 0}]
        primitive["material"] = 0
        document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
    document["buffers"][0]["byteLength"] = len(binary)
    json_data = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_data += b" " * ((-len(json_data)) % 4)
    binary_data = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_data) + 8 + len(binary_data)
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, total) +
                     struct.pack("<II", len(json_data), 0x4E4F534A) + json_data +
                     struct.pack("<II", len(binary_data), 0x004E4942) + binary_data)


def _quad(z=0.0):
    """標準cameraから見える四角形の頂点、UV、indexを返す。"""
    return ((-0.9, -0.65, z), (0.9, -0.65, z), (0.9, 0.65, z), (-0.9, 0.65, z)), ((0, 0), (1, 0), (1, 1), (0, 1)), (0, 1, 2, 0, 2, 3)


def _write_quad(path, factor, mode, texture=None, size=None, node_translation=(0, 0, 0)):
    """四角形の形状を共通にして材質を変えたfixtureを作る。"""
    vertices, uvs, indices = _quad()
    _write_glb(path, vertices, uvs, indices, factor, mode, texture, size, node_translation)


def _write_triangle_pair(path):
    """同じprimitiveに手前の赤と奥の青の半透明四角形を入れる。"""
    vertices = []
    uvs = []
    indices = []
    for z, u in ((-0.35, 0.25), (0.35, 0.75)):
        base = len(vertices)
        vertices.extend(((-0.8, -0.65, z), (0.8, -0.65, z),
                         (0.8, 0.65, z), (-0.8, 0.65, z)))
        uvs.extend(((u, 0.5),) * 4)
        indices.extend((base, base + 1, base + 2, base, base + 2, base + 3))
    texture = (255, 0, 0, 128, 0, 0, 255, 128)
    _write_glb(path, vertices, uvs, indices, (1, 1, 1, 1), "BLEND",
               texture, (2, 1), (0.08, 0, 0))


def _rotate_xyz(vector, rotation_degrees):
    """独立な逐次軸回転でmodel頂点を変換する。"""
    x, y, z = vector
    rx, ry, rz = (math.radians(value) for value in rotation_degrees)
    x, y = x * math.cos(rz) - y * math.sin(rz), x * math.sin(rz) + y * math.cos(rz)
    x, z = x * math.cos(ry) + z * math.sin(ry), -x * math.sin(ry) + z * math.cos(ry)
    y, z = y * math.cos(rx) - z * math.sin(rx), y * math.sin(rx) + z * math.cos(rx)
    return x, y, z


def _normalize(vector):
    """参照GLB用vectorを単位長にする。"""
    length = math.sqrt(sum(component * component for component in vector))
    return tuple(component / length for component in vector)


def _transform_position(position, scale, rotation):
    """scale後にZ、Y、X回転する独立参照位置を返す。"""
    return _rotate_xyz(tuple(position[axis] * scale[axis] for axis in range(3)), rotation)


def _transform_normal(normal, scale, rotation):
    """逆scale後に回転する独立参照法線を返す。"""
    return _normalize(_rotate_xyz(tuple(normal[axis] / scale[axis] for axis in range(3)), rotation))


def _write_projection_fixture(path, factor, alpha_mode, baked):
    """実行時変換modelと、位置・法線を事前変換した独立参照を保存する。"""
    scale = (-0.82, 1.27, 0.61)
    rotation = (23.0, -34.0, 19.0)
    vertices, uvs, indices = _quad()
    normal = _normalize((0.24, 0.37, 0.90))
    normals = (normal,) * len(vertices)
    if baked:
        vertices = tuple(_transform_position(vertex, scale, rotation) for vertex in vertices)
        normals = tuple(_transform_normal(vertex_normal, scale, rotation) for vertex_normal in normals)
    _write_glb(path, vertices, uvs, indices, factor, alpha_mode, normals=normals)


def generate(output_dir):
    """指定先へalpha描画とGPU変換比較fixtureを出力する。"""
    output_dir.mkdir(parents=True, exist_ok=True)
    _write_quad(output_dir / "blend-swatch.glb", (1, 0, 0, 0.5), "BLEND",
                (255, 255, 255, 0, 255, 255, 255, 64,
                 255, 255, 255, 128, 255, 255, 255, 255), (4, 1))
    _write_quad(output_dir / "blend-red.glb", (1, 0, 0, 0.5), "BLEND", node_translation=(0.04, 0, 0))
    _write_quad(output_dir / "blend-blue.glb", (0, 0, 1, 0.5), "BLEND", node_translation=(-0.04, 0, 0))
    _write_quad(output_dir / "blend-opaque-back.glb", (0, 1, 0, 1), "OPAQUE")
    _write_quad(output_dir / "blend-mask-front.glb", (1, 0, 0, 1), "MASK",
                (255, 255, 255, 0, 255, 255, 255, 255), (2, 1))
    _write_quad(output_dir / "blend-opaque-front.glb", (1, 0, 0, 0.25), "OPAQUE")
    _write_triangle_pair(output_dir / "blend-triangle-pair.glb")
    _write_projection_fixture(output_dir / "projection-opaque-actual.glb", (0.78, 0.48, 0.12, 1.0), "OPAQUE", False)
    _write_projection_fixture(output_dir / "projection-opaque-reference.glb", (0.78, 0.48, 0.12, 1.0), "OPAQUE", True)
    _write_projection_fixture(output_dir / "projection-blend-red-actual.glb", (0.82, 0.12, 0.08, 0.48), "BLEND", False)
    _write_projection_fixture(output_dir / "projection-blend-red-reference.glb", (0.82, 0.12, 0.08, 0.48), "BLEND", True)
    _write_projection_fixture(output_dir / "projection-blend-blue-actual.glb", (0.08, 0.22, 0.84, 0.62), "BLEND", False)
    _write_projection_fixture(output_dir / "projection-blend-blue-reference.glb", (0.08, 0.22, 0.84, 0.62), "BLEND", True)
    return 13


def main():
    """コマンドライン引数で指定した場所へfixture群を生成する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()
    count = generate(arguments.output_dir)
    print(f"generated {count} blend and projection fixtures")


if __name__ == "__main__":
    main()
