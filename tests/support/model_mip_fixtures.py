#!/usr/bin/env python3
"""glTF sampler mip filterのGPU画素比較用GLBを生成する。"""

import argparse
import json
from pathlib import Path
import struct
import zlib


WIDTH = 640
HEIGHT = 480
CAMERA_DISTANCE = 3.0
FOCAL_LENGTH = 3.0 ** 0.5
ASPECT = WIDTH / HEIGHT
QUAD = (-0.9, 0.9, -0.65, 0.65)
WRAP_CLAMP = {"wrapS": 33071, "wrapT": 33071}
MIP_LEVEL = 2.25
MIP_RHO = 2.0 ** MIP_LEVEL


def png_chunk(kind, payload):
    """PNG chunkへ長さとCRCを加える。"""
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload)))


def make_png(rows):
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


def append_view(binary, views, payload, target=None):
    """binaryへ4byte整列したpayloadを加えbufferView indexを返す。"""
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    binary.extend(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
    if target is not None:
        view["target"] = target
    views.append(view)
    return len(views) - 1


def append_float_accessor(binary, views, accessors, values, components, value_type,
                          minimum=None, maximum=None):
    """float列をaccessorとして記録する。"""
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


def append_indices(binary, views, accessors):
    """四角形を二つの三角形へ分けるindex列を追加する。"""
    view = append_view(binary, views, struct.pack("<6H", 0, 1, 2, 0, 2, 3), 34963)
    accessors.append({"bufferView": view, "componentType": 5123,
                      "count": 6, "type": "SCALAR", "min": [0], "max": [3]})
    return len(accessors) - 1


def center_world(x, y):
    """640x480 captureの画素中心をmodel平面へ逆投影する。"""
    return ((2.0 * (x + 0.5) / WIDTH - 1.0) * ASPECT * CAMERA_DISTANCE / FOCAL_LENGTH,
            (1.0 - 2.0 * (y + 0.5) / HEIGHT) * CAMERA_DISTANCE / FOCAL_LENGTH)


def pixel_rate_per_world(axis):
    """平面上の単位距離がscreenで占めるpixel数を返す。"""
    if axis == "u":
        return FOCAL_LENGTH / CAMERA_DISTANCE / ASPECT * WIDTH / 2.0
    return FOCAL_LENGTH / CAMERA_DISTANCE * HEIGHT / 2.0


def quad_uv(bounds=QUAD, target=(0.4375, 0.5), sample=(320, 240),
            rho_u=0.0, width=64, rho_v=0.0, height=64):
    """指定画素で目標UVとなるようquad頂点UVとscreen footprintを計算する。"""
    left, right, bottom, top = bounds
    sample_x, sample_y = center_world(*sample)
    slope_u = rho_u * pixel_rate_per_world("u") / width
    slope_v = rho_v * pixel_rate_per_world("v") / height
    u_left = target[0] - (sample_x - left) * slope_u
    u_right = target[0] + (right - sample_x) * slope_u
    v_bottom = target[1] + (sample_y - bottom) * slope_v
    v_top = target[1] - (top - sample_y) * slope_v
    return ((u_left, v_bottom), (u_right, v_bottom),
            (u_right, v_top), (u_left, v_top))


def factor_material(factor, metallic=0.0, roughness=1.0):
    """textureのbaked referenceに使う線形材質を作る。"""
    return {"pbrMetallicRoughness": {
        "baseColorFactor": list(factor) + [1.0],
        "metallicFactor": metallic, "roughnessFactor": roughness}}


def texture_material(texture=0, metallic=0.0, roughness=1.0):
    """base color imageを参照する材質を作る。"""
    return {"pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
        "metallicFactor": metallic, "roughnessFactor": roughness,
        "baseColorTexture": {"index": texture}}}


def write_glb(output, filename, quads, materials, images=(), samplers=(), textures=()):
    """quad、材質、画像、samplerを一つの埋込GLBへ書く。"""
    binary = bytearray()
    views = []
    accessors = []
    primitives = []
    for quad in quads:
        left, right, bottom, top = quad.get("bounds", QUAD)
        positions = ((left, bottom, 0.0), (right, bottom, 0.0),
                     (right, top, 0.0), (left, top, 0.0))
        normals = (quad.get("normal", (0.0, 0.0, -1.0)),) * 4
        tangents = ((1.0, 0.0, 0.0, 1.0),) * 4
        uvs = quad["uv"]
        position = append_float_accessor(binary, views, accessors, positions, 3, "VEC3",
                                         [left, bottom, 0.0], [right, top, 0.0])
        normal = append_float_accessor(binary, views, accessors, normals, 3, "VEC3")
        tangent = append_float_accessor(binary, views, accessors, tangents, 4, "VEC4")
        texcoord = append_float_accessor(binary, views, accessors, uvs, 2, "VEC2")
        indices = append_indices(binary, views, accessors)
        primitives.append({"attributes": {"POSITION": position, "NORMAL": normal,
                                           "TANGENT": tangent, "TEXCOORD_0": texcoord},
                           "indices": indices, "material": quad["material"]})
    document = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
                "bufferViews": views, "accessors": accessors, "materials": list(materials),
                "meshes": [{"primitives": primitives}], "nodes": [{"mesh": 0}],
                "scenes": [{"nodes": [0]}], "scene": 0}
    if images:
        image_views = [append_view(binary, views, payload) for payload in images]
        document["images"] = [{"bufferView": index, "mimeType": "image/png"}
                              for index in image_views]
        document["samplers"] = list(samplers)
        document["textures"] = list(textures)
        document["buffers"][0]["byteLength"] = len(binary)
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.joinpath(f"{filename}.glb").write_bytes(glb)


def checker_srgb():
    """level2で白黒texelが交互になるSRGB checker画像を作る。"""
    return make_png([[(0 if (x // 4) % 2 == 0 else 255,) * 3 + (255,)
                      for x in range(64)] for _ in range(64)])


def role_checker():
    """一画像をSRGB、MR線形、normal線形として読むcheckerを作る。"""
    return make_png([[(0 if (x // 4) % 2 == 0 else 255,
                       0 if (x // 4) % 2 == 0 else 255, 255, 255)
                      for x in range(64)] for _ in range(64)])


def mr_checker():
    """粗さと金属度が別々に変わる線形MR checker画像を作る。"""
    return make_png([[(31, 0 if (x // 4) % 2 == 0 else 255,
                       255 if (x // 4) % 2 == 0 else 0, 17)
                      for x in range(64)] for _ in range(64)])


def normal_checker():
    """反対向きの接線X成分がmip平均で平らになるnormal画像を作る。"""
    return make_png([[(0 if (x // 4) % 2 == 0 else 255, 128, 255, 255)
                      for x in range(64)] for _ in range(64)])


def sampler(min_filter, mag_filter=9729):
    """clamp座標と指定filterを持つglTF samplerを作る。"""
    return dict(WRAP_CLAMP, minFilter=min_filter, magFilter=mag_filter)


def one_texture(sampler_index=0):
    """source image zeroを一つのsamplerで参照するtextureを作る。"""
    return {"source": 0, "sampler": sampler_index}


def add_factor_pair(output, name, min_filter, expected, image, rho=MIP_RHO,
                    target=(6.75 / 16.0, 0.5), dimensions=(64, 64)):
    """実textureと単色baked reference GLBを作る。"""
    width, height = dimensions
    uv = quad_uv(target=target, rho_u=rho, width=width)
    quad = {"bounds": QUAD, "uv": uv, "material": 0}
    write_glb(output, name, [quad], [texture_material()], [image],
              [sampler(min_filter)], [one_texture()])
    write_glb(output, f"{name}-reference", [quad], [factor_material(expected)])


def generate_min_filter_fixtures(output):
    """4種mip filterを異なる既知linear colorで照合する。"""
    image = checker_srgb()
    cases = ((9984, 0.0), (9985, 0.25), (9986, 0.125), (9987, 0.3125))
    for min_filter, factor in cases:
        add_factor_pair(output, f"mip-min-{min_filter}", min_filter,
                        (factor, factor, factor), image)
    add_factor_pair(output, "mip-no-mipmap", 9729, (0.0, 0.0, 0.0), image)


def generate_role_fixture(output):
    """一つのsource imageをsRGB、MR、normalの三役で読む。"""
    image = role_checker()
    selected_sampler = sampler(9987)
    textures = [one_texture(0), one_texture(0), one_texture(0)]
    actual_material = {"pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0], "metallicFactor": 1.0,
        "roughnessFactor": 1.0, "baseColorTexture": {"index": 0},
        "metallicRoughnessTexture": {"index": 1}},
        "normalTexture": {"index": 2, "scale": 1.0}}
    uv = quad_uv(target=(29.0 / 64.0, 0.5), rho_u=256.0)
    quad = {"bounds": QUAD, "uv": uv, "material": 0, "normal": (0.0, 0.0, -1.0)}
    write_glb(output, "mip-shared-roles", [quad], [actual_material], [image],
              [selected_sampler], textures)
    encoded_188 = 188.0 / 255.0
    decoded_188 = ((encoded_188 + 0.055) / 1.055) ** 2.4
    reference = factor_material((decoded_188, decoded_188, 1.0),
                                metallic=1.0, roughness=128.0 / 255.0)
    normal_reference = (1.0 / 255.0, -1.0 / 255.0, -1.0)
    normal_length = sum(component * component for component in normal_reference) ** 0.5
    reference_quad = dict(quad, normal=tuple(component / normal_length
                                             for component in normal_reference))
    write_glb(output, "mip-shared-roles-reference", [reference_quad], [reference])
    no_mipmap_textures = [one_texture(0), one_texture(0), one_texture(0)]
    write_glb(output, "mip-shared-roles-no-mipmap", [quad], [actual_material], [image],
              [sampler(9729)], no_mipmap_textures)


def generate_mr_fixture(output):
    """線形MR mipがroughnessとmetallicへ平均値を渡す。"""
    image = mr_checker()
    material = {"pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0], "metallicFactor": 1.0,
        "roughnessFactor": 1.0, "metallicRoughnessTexture": {"index": 0}}}
    uv = quad_uv(target=(29.0 / 64.0, 0.5), rho_u=256.0)
    quad = {"bounds": QUAD, "uv": uv, "material": 0, "normal": (0.0, 0.0, -1.0)}
    write_glb(output, "mip-linear-mr", [quad], [material], [image],
              [sampler(9987)], [one_texture()])
    write_glb(output, "mip-linear-mr-reference", [quad],
              [factor_material((1.0, 1.0, 1.0), metallic=128.0 / 255.0,
                               roughness=128.0 / 255.0)])
    write_glb(output, "mip-linear-mr-no-mipmap", [quad], [material], [image],
              [sampler(9729)], [one_texture()])


def generate_normal_fixture(output):
    """線形normal mipの反対向きX成分を平均し、平らな法線と比べる。"""
    image = normal_checker()
    material = {"pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                                           "metallicFactor": 0.0, "roughnessFactor": 1.0},
                "normalTexture": {"index": 0, "scale": 1.0}}
    uv = quad_uv(target=(29.0 / 64.0, 0.5), rho_u=256.0)
    quad = {"bounds": QUAD, "uv": uv, "material": 0, "normal": (0.0, 0.0, -1.0)}
    write_glb(output, "mip-linear-normal", [quad], [material], [image],
              [sampler(9987)], [one_texture()])
    normal_reference = (1.0 / 255.0, -1.0 / 255.0, -1.0)
    normal_length = sum(component * component for component in normal_reference) ** 0.5
    normal_reference = tuple(component / normal_length for component in normal_reference)
    reference_quad = dict(quad, normal=normal_reference)
    write_glb(output, "mip-linear-normal-reference", [reference_quad],
              [factor_material((1.0, 1.0, 1.0))])
    write_glb(output, "mip-linear-normal-no-mipmap", [quad], [material], [image],
              [sampler(9729)], [one_texture()])


def generate_npot_fixture(output, name, width, height, expected):
    """odd寸法画像の最小mipが最終画素を含むか調べる。"""
    rows = [[(0, 0, 0, 255) for _ in range(width)] for _ in range(height)]
    rows[-1][-1] = (255, 255, 255, 255)
    image = make_png(rows)
    rho = 32.0
    uv = quad_uv(target=(0.5, 0.5), rho_u=rho, width=width,
                 rho_v=rho, height=height)
    quad = {"bounds": QUAD, "uv": uv, "material": 0}
    write_glb(output, name, [quad], [texture_material()], [image],
              [sampler(9987)], [one_texture()])
    write_glb(output, f"{name}-reference", [quad],
              [factor_material((expected, expected, expected))])


def generate_npot_fixtures(output):
    """5x3と1x7画像の最終mip平均を検査する。"""
    generate_npot_fixture(output, "mip-npot-5x3", 5, 3, 1.0 / 15.0)
    generate_npot_fixture(output, "mip-npot-1x7", 1, 7, 1.0 / 7.0)


def generate_stress_fixture(output):
    """同一画像のno-mip/mip samplerを同時に描き、退避後も保持する。"""
    image = checker_srgb()
    sampler_list = [sampler(9729), sampler(9987)]
    textures = [one_texture(0), one_texture(1)]
    materials = [texture_material(0), texture_material(1)]
    left_bounds = (-0.9, 0.0, -0.65, 0.65)
    right_bounds = (0.0, 0.9, -0.65, 0.65)
    left_uv = quad_uv(bounds=left_bounds, target=(6.75 / 16.0, 0.5), sample=(250, 240),
                      rho_u=MIP_RHO, width=64)
    right_uv = quad_uv(bounds=right_bounds, target=(6.75 / 16.0, 0.5), sample=(390, 240),
                       rho_u=MIP_RHO, width=64)
    quads = [{"bounds": left_bounds, "uv": left_uv, "material": 0},
             {"bounds": right_bounds, "uv": right_uv, "material": 1}]
    write_glb(output, "mip-sampler-stress", quads, materials, [image], sampler_list, textures)
    reference_materials = [factor_material((0.0, 0.0, 0.0)),
                          factor_material((0.3125, 0.3125, 0.3125))]
    write_glb(output, "mip-sampler-stress-reference", quads, reference_materials)


def generate(output):
    """GPU mip fixturesを出力先へ生成する。"""
    output.mkdir(parents=True, exist_ok=True)
    generate_min_filter_fixtures(output)
    generate_role_fixture(output)
    generate_mr_fixture(output)
    generate_normal_fixture(output)
    generate_npot_fixtures(output)
    generate_stress_fixture(output)
    return 25


def main():
    """fixture出力先を受け取りGLB群を作る。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    generate(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
