#!/usr/bin/env python3
"""sampler設定が異なる埋め込みGLBを生成する。"""
import argparse
import json
from pathlib import Path
import struct
import zlib


POSITIONS = ((-0.9, -0.65, 0.0), (0.9, -0.65, 0.0),
             (0.9, 0.65, 0.0), (-0.9, 0.65, 0.0))
NORMALS = ((0.0, 0.0, -1.0),) * 4
TANGENTS = ((1.0, 0.0, 0.0, 1.0),) * 4
UV0 = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))
UV1 = ((1.0, 1.0), (0.0, 1.0), (0.0, 0.0), (1.0, 0.0))
PIXEL = (128, 128, 255, 255)
PATTERN = (((192, 128, 255, 255), (64, 128, 255, 255)),
           ((128, 192, 255, 255), (128, 64, 255, 255)))


def png_chunk(kind, payload):
    """PNG chunkへ長さとCRCを加える。"""
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload)))


def make_png():
    """samplerが共有する1x1 RGBA画像を作る。"""
    row = b"\0" + bytes(PIXEL)
    header = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", zlib.compress(row)) + png_chunk(b"IEND", b""))


def make_pattern_png():
    """四象限の色が異なる64x32画像を作る。"""
    rows = []
    for pattern_row in PATTERN:
        row = []
        for pixel in pattern_row:
            row.extend((pixel,) * 32)
        rows.extend((row,) * 16)
    return make_png_pixels(rows)


def make_png_pixels(rows):
    """RGBA画素列をPNGへ変換する。"""
    height = len(rows)
    width = len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for pixel in row:
            raw.extend(pixel)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", zlib.compress(bytes(raw))) +
            png_chunk(b"IEND", b""))


def append_float_accessor(binary, views, accessors, values, components, value_type,
                          minimum=None, maximum=None):
    """float属性をGLBのbufferViewとaccessorへ追加する。"""
    payload = struct.pack("<" + "f" * (len(values) * components),
                          *(value for row in values for value in row))
    view = append_view(binary, views, payload, 34962)
    accessor = {"bufferView": view, "componentType": 5126,
                "count": len(values), "type": value_type}
    if minimum is not None:
        accessor["min"] = minimum
    if maximum is not None:
        accessor["max"] = maximum
    accessors.append(accessor)
    return len(accessors) - 1


def linear_channel(value):
    """sRGBの1成分を線形色へ変換する。"""
    encoded = value / 255.0
    return encoded / 12.92 if encoded <= 0.04045 else ((encoded + 0.055) / 1.055) ** 2.4


def linear_pixel(pixel):
    """PNG画素のRGBをglTF材質係数用の線形色へ変換する。"""
    return [linear_channel(value) for value in pixel[:3]] + [1.0]


def blended_pattern_pixel(weights):
    """四象限のsRGB色を指定比率で混ぜ、線形色を返す。"""
    rgb = [sum(weights[index] * linear_channel(PATTERN[index // 2][index % 2][channel])
               for index in range(4)) for channel in range(3)]
    return rgb + [1.0]


def mapped_pattern_normal(pixel):
    """選択したnormal画素をfixtureのTBN基準法線へ変換する。"""
    vector = (pixel[0] / 255.0 * 2.0 - 1.0,
              -(pixel[1] / 255.0 * 2.0 - 1.0),
              -(pixel[2] / 255.0 * 2.0 - 1.0))
    length = sum(value * value for value in vector) ** 0.5
    return tuple(value / length for value in vector)


def write_gpu_fixture(output, filename, quads, materials, image_payload=None,
                       samplers=(), textures=()):
    """GPU用GLBをprimitiveごとの属性と材質から組み立てる。"""
    binary = bytearray()
    views = []
    accessors = []
    primitives = []
    for quad in quads:
        left, right, bottom, top = quad["bounds"]
        positions = ((left, bottom, 0.0), (right, bottom, 0.0),
                     (right, top, 0.0), (left, top, 0.0))
        normals = (quad.get("normal", (0.0, 0.0, -1.0)),) * 4
        tangents = ((1.0, 0.0, 0.0, 1.0),) * 4
        uv = quad["uv"]
        uv0 = (uv,) * 4 if len(uv) == 2 and isinstance(uv[0], (int, float)) else uv
        uv1 = ((1.0, 1.0), (0.0, 1.0), (0.0, 0.0), (1.0, 0.0))
        position = append_float_accessor(binary, views, accessors, positions, 3, "VEC3",
                                         [left, bottom, 0.0], [right, top, 0.0])
        normal = append_float_accessor(binary, views, accessors, normals, 3, "VEC3")
        tangent = append_float_accessor(binary, views, accessors, tangents, 4, "VEC4")
        texcoord0 = append_float_accessor(binary, views, accessors, uv0, 2, "VEC2")
        texcoord1 = append_float_accessor(binary, views, accessors, uv1, 2, "VEC2")
        indices_payload = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
        indices_view = append_view(binary, views, indices_payload, 34963)
        accessors.append({"bufferView": indices_view, "componentType": 5123,
                          "count": 6, "type": "SCALAR", "min": [0], "max": [3]})
        indices = len(accessors) - 1
        primitives.append({"attributes": {"POSITION": position, "NORMAL": normal,
                                           "TANGENT": tangent, "TEXCOORD_0": texcoord0,
                                           "TEXCOORD_1": texcoord1},
                           "indices": indices, "material": quad["material"]})
    document = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
                "bufferViews": views, "accessors": accessors, "materials": materials,
                "meshes": [{"primitives": primitives}], "nodes": [{"mesh": 0}],
                "scenes": [{"nodes": [0]}], "scene": 0}
    if image_payload is not None:
        image_view = append_view(binary, views, image_payload)
        document["images"] = [{"bufferView": image_view, "mimeType": "image/png"}]
        document["textures"] = list(textures)
        document["samplers"] = list(samplers)
        document["buffers"][0]["byteLength"] = len(binary)
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.joinpath(f"{filename}.glb").write_bytes(glb)


def make_base_material(texture_index=None, factor=None):
    """base color用のtexture viewまたは線形色材質を作る。"""
    pbr = {"baseColorFactor": factor or [1.0, 1.0, 1.0, 1.0],
           "metallicFactor": 0.0, "roughnessFactor": 1.0}
    if texture_index is not None:
        pbr["baseColorTexture"] = {"index": texture_index, "texCoord": 0}
    return {"pbrMetallicRoughness": pbr}


def gpu_wrap_fixtures(output):
    """wrap modeごとにtexture表示とfactor基準モデルを作る。"""
    cases = (
        ("repeat", (-0.25, 1.75), {"wrapS": 10497, "wrapT": 10497}, PATTERN[1][1]),
        ("mirror", (-0.25, 1.75), {"wrapS": 33648, "wrapT": 33648}, PATTERN[0][0]),
        ("clamp", (-0.25, 1.75), {"wrapS": 33071, "wrapT": 33071}, PATTERN[1][0]),
        ("u-repeat-v-clamp", (-0.25, 1.75), {"wrapS": 10497, "wrapT": 33071}, PATTERN[1][1]),
        ("u-clamp-v-repeat", (-0.25, 1.75), {"wrapS": 33071, "wrapT": 10497}, PATTERN[1][0]),
        ("default-repeat", (-0.25, 1.75), None, PATTERN[1][1]),
        ("negative-integer", (-1.0, -1.0), {"wrapS": 10497, "wrapT": 10497,
         "minFilter": 9728, "magFilter": 9728}, PATTERN[0][0]),
        ("positive-integer", (1.0, 1.0), {"wrapS": 10497, "wrapT": 10497,
         "minFilter": 9728, "magFilter": 9728}, PATTERN[0][0]),
    )
    for name, uv, sampler, pixel in cases:
        sampler_list = [] if sampler is None else [sampler]
        texture = {"source": 0}
        if sampler is not None:
            texture["sampler"] = 0
        material = make_base_material(0)
        write_gpu_fixture(output, f"sampler-wrap-{name}",
                          [{"bounds": (-0.9, 0.9, -0.65, 0.65), "uv": uv, "material": 0}],
                          [material], make_pattern_png(), sampler_list, [texture])
        write_gpu_fixture(output, f"sampler-wrap-{name}-reference",
                          [{"bounds": (-0.9, 0.9, -0.65, 0.65), "uv": uv, "material": 0}],
                          [make_base_material(factor=linear_pixel(pixel))])


def gpu_batch_fixture(output):
    """同じ画像の異なるsamplerを隣接材質から描く。"""
    uv = (-0.25, 1.75)
    samplers = [{"wrapS": 10497, "wrapT": 10497, "minFilter": 9728, "magFilter": 9728},
                {"wrapS": 33071, "wrapT": 33071, "minFilter": 9728, "magFilter": 9728}]
    textures = [{"source": 0, "sampler": 0}, {"source": 0, "sampler": 1}]
    materials = [make_base_material(0), make_base_material(1)]
    quads = [{"bounds": (-0.9, 0.0, -0.65, 0.65), "uv": uv, "material": 0},
             {"bounds": (0.0, 0.9, -0.65, 0.65), "uv": uv, "material": 1}]
    write_gpu_fixture(output, "sampler-batch-mixed", quads, materials,
                      make_pattern_png(), samplers, textures)
    reference = [make_base_material(factor=linear_pixel(PATTERN[1][1])),
                 make_base_material(factor=linear_pixel(PATTERN[1][0]))]
    write_gpu_fixture(output, "sampler-batch-mixed-reference", quads, reference)


def gpu_role_fixture(output):
    """同じ画像を三役で異なるaddress modeから参照する。"""
    uv = (-0.25, 1.75)
    samplers = [{"wrapS": 33071, "wrapT": 33071, "minFilter": 9728, "magFilter": 9728},
                {"wrapS": 10497, "wrapT": 10497, "minFilter": 9728, "magFilter": 9728},
                {"wrapS": 33648, "wrapT": 33648, "minFilter": 9728, "magFilter": 9728}]
    textures = [{"source": 0, "sampler": index} for index in range(3)]
    actual = {"pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1],
                                        "metallicFactor": 1, "roughnessFactor": 1,
                                        "baseColorTexture": {"index": 0},
                                        "metallicRoughnessTexture": {"index": 1}},
              "normalTexture": {"index": 2, "scale": 1}}
    quad = {"bounds": (-0.9, 0.9, -0.65, 0.65), "uv": uv, "material": 0}
    write_gpu_fixture(output, "sampler-role-mixed", [quad], [actual],
                      make_pattern_png(), samplers, textures)
    reference_normal = mapped_pattern_normal(PATTERN[0][0])
    reference = {"pbrMetallicRoughness": {
        "baseColorFactor": linear_pixel(PATTERN[1][0]), "metallicFactor": 1,
        "roughnessFactor": 64 / 255.0}}
    reference_quad = dict(quad, normal=reference_normal)
    write_gpu_fixture(output, "sampler-role-mixed-reference", [reference_quad], [reference])


def gpu_filter_fixtures(output):
    """minificationとmagnificationのfilter選択を比較する。"""
    source_pixel = PATTERN[1][1]
    nearest_uv = (0.5 + 0.25 / 64.0, 0.5 + 0.25 / 32.0)
    nearest_factor = linear_pixel(source_pixel)
    linear_factor = blended_pattern_pixel((0.0625, 0.1875, 0.1875, 0.5625))
    for name, min_filter, mag_filter, factor in (
            ("mag-nearest", 9729, 9728, nearest_factor),
            ("mag-linear", 9728, 9729, linear_factor)):
        sampler = {"wrapS": 33071, "wrapT": 33071,
                   "minFilter": min_filter, "magFilter": mag_filter}
        uv_quad = {"bounds": (-0.9, 0.9, -0.65, 0.65), "uv": nearest_uv, "material": 0}
        material = make_base_material(0)
        write_gpu_fixture(output, f"sampler-{name}", [uv_quad], [material],
                          make_pattern_png(), [sampler], [{"source": 0, "sampler": 0}])
        write_gpu_fixture(output, f"sampler-{name}-reference", [uv_quad],
                          [make_base_material(factor=factor)])
    # pixel (320,240)の中心を逆投影し、期待UVがその位置で一致するよう平面中心をずらす。
    focal = 3.0 ** 0.5
    sample_world_x = (2.0 * 320.5 / 640.0 - 1.0) * (640.0 / 480.0) * 3.0 / focal
    sample_world_y = (1.0 - 2.0 * 240.5 / 480.0) * 3.0 / focal
    center_u = nearest_uv[0] - sample_world_x * (64.0 / 1.8)
    center_v = nearest_uv[1] + sample_world_y * (32.0 / 1.3)
    varying_uv = ((center_u - 32.0, center_v + 16.0),
                  (center_u + 32.0, center_v + 16.0),
                  (center_u + 32.0, center_v - 16.0),
                  (center_u - 32.0, center_v - 16.0))
    varying_quad = {"bounds": (-0.9, 0.9, -0.65, 0.65), "uv": varying_uv, "material": 0}
    for name, min_filter, mag_filter, factor in (("min-nearest", 9728, 9729, nearest_factor),
                                                ("min-linear", 9729, 9728, linear_factor)):
        sampler = {"wrapS": 33071, "wrapT": 33071,
                   "minFilter": min_filter, "magFilter": mag_filter}
        material = make_base_material(0)
        write_gpu_fixture(output, f"sampler-{name}", [varying_quad], [material],
                          make_pattern_png(), [sampler], [{"source": 0, "sampler": 0}])
        write_gpu_fixture(output, f"sampler-{name}-reference", [varying_quad],
                          [make_base_material(factor=factor)])


def generate_gpu(output):
    """画素比較用sampler GLB群を作る。"""
    gpu_wrap_fixtures(output)
    gpu_batch_fixture(output)
    gpu_role_fixture(output)
    gpu_filter_fixtures(output)
    return 28


def append_view(binary, views, payload, target=None):
    """bufferへ4byte整列したpayloadを追加し、そのviewを返す。"""
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    binary.extend(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
    if target is not None:
        view["target"] = target
    views.append(view)
    return len(views) - 1


def add_accessor(accessors, views, binary, values, components, value_type,
                 target, minimum=None, maximum=None):
    """floatまたはunsigned short列をaccessorとして記録する。"""
    payload = (struct.pack("<" + "f" * (len(values) * components),
                           *(value for row in values for value in row))
               if components != 1 else struct.pack("<6H", *values))
    view = append_view(binary, views, payload, target)
    accessor = {"bufferView": view, "componentType": 5123 if components == 1 else 5126,
                "count": len(values), "type": value_type}
    if minimum is not None:
        accessor["min"] = minimum
    if maximum is not None:
        accessor["max"] = maximum
    accessors.append(accessor)
    return len(accessors) - 1


def sampler_records(case):
    """case名に応じたglTF sampler配列とtexture参照を返す。"""
    if case == "sampler-defaults":
        return [], [None, None, None]
    if case == "sampler-explicit":
        return ([{"wrapS": 33071, "wrapT": 33071, "minFilter": 9728, "magFilter": 9728},
                 {"wrapS": 10497, "wrapT": 10497, "minFilter": 9729, "magFilter": 9729},
                 {"wrapS": 33648, "wrapT": 33648, "minFilter": 9987, "magFilter": 9728}],
                [0, 1, 2])
    if case == "sampler-min-fallbacks-a":
        return ([{"minFilter": 9984, "magFilter": 9729},
                 {"minFilter": 9985, "magFilter": 9728},
                 {"minFilter": 9986, "magFilter": 9729}], [0, 1, 2])
    if case == "sampler-min-fallbacks-b":
        return ([{"minFilter": 9987, "magFilter": 9729}], [0, 0, 0])
    if case == "bad-sampler-wrap":
        return ([{"wrapS": 99999}], [0, None, None])
    if case == "bad-sampler-mag":
        return ([{"magFilter": 99999}], [0, None, None])
    if case == "bad-sampler-min":
        return ([{"minFilter": 99999}], [0, None, None])
    if case == "bad-sampler-reference":
        return ([], [99, None, None])
    raise ValueError(f"未対応のsampler fixtureです: {case}")


def generate_one(output, case):
    """texture役割ごとのsamplerを持つGLBを1つ作る。"""
    binary = bytearray()
    views = []
    accessors = []
    position = add_accessor(accessors, views, binary, POSITIONS, 3, "VEC3", 34962,
                            [-0.9, -0.65, 0.0], [0.9, 0.65, 0.0])
    normal = add_accessor(accessors, views, binary, NORMALS, 3, "VEC3", 34962)
    tangent = add_accessor(accessors, views, binary, TANGENTS, 4, "VEC4", 34962)
    uv0 = add_accessor(accessors, views, binary, UV0, 2, "VEC2", 34962,
                       [0.0, 0.0], [1.0, 1.0])
    uv1 = add_accessor(accessors, views, binary, UV1, 2, "VEC2", 34962,
                       [0.0, 0.0], [1.0, 1.0])
    indices = add_accessor(accessors, views, binary, (0, 1, 2, 0, 2, 3), 1,
                           "SCALAR", 34963, [0], [3])
    image_view = append_view(binary, views, make_png())
    samplers, texture_sampler_indices = sampler_records(case)
    textures = []
    for sampler_index in texture_sampler_indices:
        texture = {"source": 0}
        if sampler_index is not None:
            texture["sampler"] = sampler_index
        textures.append(texture)
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
        "images": [{"bufferView": image_view, "mimeType": "image/png"}],
        "textures": textures,
        "samplers": samplers,
        "materials": [{"pbrMetallicRoughness": {
            "baseColorFactor": [1, 1, 1, 1], "metallicFactor": 1,
            "roughnessFactor": 1,
            "baseColorTexture": {"index": 0, "texCoord": 0},
            "metallicRoughnessTexture": {"index": 1, "texCoord": 0}},
            "normalTexture": {"index": 2, "texCoord": 1}}],
        "meshes": [{"primitives": [{"attributes": {
            "POSITION": position, "NORMAL": normal, "TANGENT": tangent,
            "TEXCOORD_0": uv0, "TEXCOORD_1": uv1},
            "indices": indices, "material": 0}]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
    }
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.joinpath(f"{case}.glb").write_bytes(glb)


def generate(output):
    """CPU契約用とGPU画素比較用のsampler fixtureを作る。"""
    output.mkdir(parents=True, exist_ok=True)
    cases = ("sampler-defaults", "sampler-explicit", "sampler-min-fallbacks-a",
             "sampler-min-fallbacks-b", "bad-sampler-wrap", "bad-sampler-mag",
             "bad-sampler-min", "bad-sampler-reference")
    for case in cases:
        generate_one(output, case)
    return len(cases) + generate_gpu(output)


def main():
    """fixture出力先を解析してsampler GLBを生成する。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    generate(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
