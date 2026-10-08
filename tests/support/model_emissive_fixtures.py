#!/usr/bin/env python3
"""自己発光材質のGPU画素比較に使うGLBを生成する。"""

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
INDICES = (0, 1, 2, 0, 2, 3)


def png_chunk(kind, payload):
    """PNG chunkへ長さとCRCを付ける。"""
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload)))


def make_png(rows):
    """RGBA画素列から8bit PNGを作る。"""
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


def solid_png(pixel):
    """一定のRGBA値を持つ1x1 PNGを作る。"""
    return make_png([[pixel]])


def append_view(binary, views, payload, target=None):
    """bufferへ4byte整列payloadを追加しview indexを返す。"""
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    binary.extend(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
    if target is not None:
        view["target"] = target
    views.append(view)
    return len(views) - 1


def append_float_accessor(binary, views, accessors, values, components, kind,
                          minimum=None, maximum=None):
    """浮動小数vector列をaccessorへ記録する。"""
    payload = struct.pack("<" + "f" * (len(values) * components),
                          *(value for row in values for value in row))
    view = append_view(binary, views, payload, 34962)
    accessor = {"bufferView": view, "componentType": 5126,
                "count": len(values), "type": kind}
    if minimum is not None:
        accessor["min"] = minimum
    if maximum is not None:
        accessor["max"] = maximum
    accessors.append(accessor)
    return len(accessors) - 1


def append_indices(binary, views, accessors):
    """四角形を二つの三角形へ分けるindex列を追加する。"""
    view = append_view(binary, views, struct.pack("<6H", *INDICES), 34963)
    accessors.append({"bufferView": view, "componentType": 5123,
                      "count": 6, "type": "SCALAR", "min": [0], "max": [3]})
    return len(accessors) - 1


def center_world(x, y):
    """640x480画像の画素中心をmodel平面へ逆投影する。"""
    return ((2.0 * (x + 0.5) / WIDTH - 1.0) * ASPECT * CAMERA_DISTANCE / FOCAL_LENGTH,
            (1.0 - 2.0 * (y + 0.5) / HEIGHT) * CAMERA_DISTANCE / FOCAL_LENGTH)


def quad_uv(bounds=QUAD, value=(0.5, 0.5), sample=(320, 240), rho_u=0.0,
            rho_v=0.0, width=64, height=64):
    """中心画素のUVと指定texel footprintを持つUV四頂点を作る。"""
    left, right, bottom, top = bounds
    x, y = center_world(*sample)
    slope_u = rho_u * FOCAL_LENGTH / CAMERA_DISTANCE / ASPECT * WIDTH / 2.0 / width
    slope_v = rho_v * FOCAL_LENGTH / CAMERA_DISTANCE * HEIGHT / 2.0 / height
    u_left = value[0] - (x - left) * slope_u
    u_right = value[0] + (right - x) * slope_u
    v_bottom = value[1] + (y - bottom) * slope_v
    v_top = value[1] - (top - y) * slope_v
    return ((u_left, v_bottom), (u_right, v_bottom),
            (u_right, v_top), (u_left, v_top))


def append_quad(binary, views, accessors, quad):
    """quadの頂点属性とindexをGLBへ追加する。"""
    left, right, bottom, top = quad.get("bounds", QUAD)
    positions = ((left, bottom, 0.0), (right, bottom, 0.0),
                 (right, top, 0.0), (left, top, 0.0))
    normals = ((0.0, 0.0, -1.0),) * 4
    tangents = ((1.0, 0.0, 0.0, 1.0),) * 4
    position = append_float_accessor(binary, views, accessors, positions, 3,
                                     "VEC3", [left, bottom, 0.0], [right, top, 0.0])
    normal = append_float_accessor(binary, views, accessors, normals, 3, "VEC3")
    tangent = append_float_accessor(binary, views, accessors, tangents, 4, "VEC4")
    texcoord0 = append_float_accessor(binary, views, accessors, quad["uv0"], 2,
                                     "VEC2", [0.0, 0.0], [1.0, 1.0])
    attributes = {"POSITION": position, "NORMAL": normal, "TANGENT": tangent,
                  "TEXCOORD_0": texcoord0}
    if "uv1" in quad:
        attributes["TEXCOORD_1"] = append_float_accessor(
            binary, views, accessors, quad["uv1"], 2, "VEC2", [0.0, 0.0], [1.0, 1.0])
    return {"attributes": attributes,
            "indices": append_indices(binary, views, accessors),
            "material": quad["material"]}


def write_glb(output, name, quads, materials, images=(), samplers=(),
              textures=(), extensions_used=()):
    """quadと埋込画像からGLBを生成する。"""
    binary = bytearray()
    views = []
    accessors = []
    primitives = [append_quad(binary, views, accessors, quad) for quad in quads]
    document = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
                "bufferViews": views, "accessors": accessors,
                "materials": list(materials),
                "meshes": [{"primitives": primitives}], "nodes": [{"mesh": 0}],
                "scenes": [{"nodes": [0]}], "scene": 0}
    if extensions_used:
        document["extensionsUsed"] = list(extensions_used)
    if images:
        image_views = [append_view(binary, views, payload) for payload in images]
        document["images"] = [{"bufferView": index, "mimeType": "image/png"}
                              for index in image_views]
        document["samplers"] = list(samplers)
        document["textures"] = list(textures)
        document["buffers"][0]["byteLength"] = len(binary)
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=True).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.joinpath(f"{name}.glb").write_bytes(glb)


def quad(material=0, uv=(0.5, 0.5), bounds=QUAD, uv1=None, sample=(320, 240),
         rho_u=0.0, rho_v=0.0, width=64, height=64):
    """一定UVまたは縮小footprintを持つquadを作る。"""
    result = {"material": material, "bounds": bounds,
              "uv0": quad_uv(bounds, uv, sample, rho_u, rho_v, width, height)}
    if uv1 is not None:
        result["uv1"] = uv1
    return result


def factor_material(rgb, alpha=1.0):
    """線形baseColor参照材質を作る。"""
    return {"pbrMetallicRoughness": {
        "baseColorFactor": list(rgb) + [alpha],
        "metallicFactor": 0.0, "roughnessFactor": 1.0}}


def texture_material(index=0, texcoord=0):
    """baseColor imageを読む参照材質を作る。"""
    return {"pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
        "metallicFactor": 0.0, "roughnessFactor": 1.0,
        "baseColorTexture": {"index": index, "texCoord": texcoord}}}


def emissive_material(factor=(1.0, 1.0, 1.0), strength=1.0, texture=None,
                      texcoord=0, sampler=None, base_texture=None, mask=False):
    """黒い通常色と自己発光入力を持つ材質を作る。"""
    material = {"pbrMetallicRoughness": {
        "baseColorFactor": [0.0, 0.0, 0.0, 1.0],
        "metallicFactor": 0.0, "roughnessFactor": 1.0},
        "emissiveFactor": list(factor)}
    if base_texture is not None:
        material["pbrMetallicRoughness"]["baseColorTexture"] = {
            "index": base_texture, "texCoord": 0}
    if texture is not None:
        view = {"index": texture, "texCoord": texcoord}
        if sampler is not None:
            view.update(sampler)
        material["emissiveTexture"] = view
    if strength != 1.0:
        material["extensions"] = {"KHR_materials_emissive_strength": {
            "emissiveStrength": strength}}
    if mask:
        material["alphaMode"] = "MASK"
        material["alphaCutoff"] = 0.5
    return material


def srgb_to_linear(channel):
    """PNG sRGB byteを線形値へ変換する。"""
    encoded = channel / 255.0
    if encoded <= 0.04045:
        return encoded / 12.92
    return ((encoded + 0.055) / 1.055) ** 2.4


def fixture_textures(sampler=None, sources=(0,)):
    """PNG source列とsamplerをtexture配列へ結びつける。"""
    selected = [sampler or {}]
    return selected, [{"source": source, "sampler": 0} for source in sources]


def pair(output, name, actual_material, reference_rgb, image=None, sampler=None,
         texture_sources=(0,), uv=(0.5, 0.5), uv1=None, reference_uv=None,
         rho_u=0.0, rho_v=0.0, width=64, height=64):
    """texture actualと線形baseColor参照を同じgeometryで作る。"""
    images = [image] if image is not None else []
    samplers, textures = fixture_textures(sampler, texture_sources) if image is not None else ((), ())
    actual_quad = quad(0, uv, uv1=uv1, rho_u=rho_u, rho_v=rho_v,
                       width=width, height=height)
    write_glb(output, name, [actual_quad], [actual_material], images, samplers, textures,
              ("KHR_materials_emissive_strength",) if "extensions" in actual_material else ())
    reference_quad = quad(0, reference_uv or uv, uv1=uv1, rho_u=rho_u,
                          rho_v=rho_v, width=width, height=height)
    write_glb(output, f"{name}-reference", [reference_quad], [factor_material(reference_rgb)])


def generate_valid(output):
    """主要な係数・画像・UV・sampler・mask・mip経路を生成する。"""
    pair(output, "emissive-default", emissive_material((0.0, 0.0, 0.0)), (0.0, 0.0, 0.0))
    pair(output, "emissive-factor", emissive_material((0.25, 0.5, 0.75)), (0.25, 0.5, 0.75))
    pair(output, "emissive-zero-strength", emissive_material((1.0, 0.5, 0.25), 0.0), (0.0, 0.0, 0.0))
    uniform = solid_png((204, 102, 51, 0))
    product = tuple(srgb_to_linear(value) for value in (204, 102, 51))
    pair(output, "emissive-uniform-texture-alpha-ignored",
         emissive_material(texture=0), product, uniform)
    product_rgb = tuple(srgb_to_linear(value) * factor * 2.0
                        for value, factor in zip((128, 64, 32), (0.5, 0.25, 1.0)))
    pair(output, "emissive-factor-product",
         emissive_material((0.5, 0.25, 1.0), 2.0, texture=0), product_rgb,
         solid_png((128, 64, 32, 255)))
    mid = solid_png((128, 128, 128, 255))
    mid_linear = srgb_to_linear(128)
    pair(output, "emissive-srgb-mid", emissive_material(texture=0),
         (mid_linear,) * 3, mid)

    pattern_pixels = ((192, 128, 64, 255), (64, 192, 128, 255),
                      (128, 64, 192, 255), (32, 160, 224, 255))
    rows = [[pattern_pixels[0], pattern_pixels[1]],
            [pattern_pixels[2], pattern_pixels[3]]]
    pattern = make_png(rows)
    uv_quads = []
    reference_quads = []
    sample_uvs = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75))
    for index, uv_value in enumerate(sample_uvs):
        bounds = ((-0.9, 0.0, 0.0, 0.65), (0.0, 0.9, 0.0, 0.65),
                  (-0.9, 0.0, -0.65, 0.0), (0.0, 0.9, -0.65, 0.0))[index]
        uv1_value = (1.0 - uv_value[0], uv_value[1])
        uv_values = (uv1_value,) * 4
        actual_quad = quad(0, (0.5, 0.5), bounds, uv1=uv_values, width=2, height=2)
        actual_quad["uv0"] = quad_uv(bounds, (0.5, 0.5), width=2, height=2)
        uv_quads.append(actual_quad)
        pixel_index = ((0 if uv1_value[1] < 0.5 else 2) +
                       (0 if uv1_value[0] < 0.5 else 1))
        color = tuple(srgb_to_linear(value) for value in pattern_pixels[pixel_index][:3])
        reference_quads.append(quad(index, (0.5, 0.5), bounds, width=2, height=2))
        # Reference material is per quadrant and stores linear emission in baseColorFactor.
        if len(reference_quads) != len(uv_quads):
            raise ValueError("UV reference fixture construction failed")
        reference_quads[-1]["material"] = index
    write_glb(output, "emissive-uv1-pattern", uv_quads,
              [emissive_material(texture=0, texcoord=1)], [pattern], [{}], [{"source": 0, "sampler": 0}])
    write_glb(output, "emissive-uv1-pattern-reference", reference_quads,
              [factor_material(tuple(srgb_to_linear(v) for v in pixel[:3])) for pixel in
               (pattern_pixels[1], pattern_pixels[0], pattern_pixels[3], pattern_pixels[2])])

    wrap_image = make_png([[(192, 128, 64, 255), (64, 192, 128, 255)],
                           [(128, 64, 192, 255), (32, 160, 224, 255)]])
    mirror_rgb = tuple(srgb_to_linear(v) for v in (192, 128, 64))
    pair(output, "emissive-wrap-mirror", emissive_material(texture=0), mirror_rgb,
         wrap_image, {"wrapS": 33648, "wrapT": 33648}, uv=(-0.25, 1.75), width=2, height=2)

    shared_image = solid_png((64, 64, 64, 255))
    shared_value = srgb_to_linear(64)
    shared_factor = min(shared_value * 2.0, 1.0)
    shared_material = emissive_material(texture=0, base_texture=0)
    shared_material["pbrMetallicRoughness"]["baseColorFactor"] = [1.0, 1.0, 1.0, 1.0]
    pair(output, "emissive-shared-base",
         shared_material,
         (shared_factor,) * 3, shared_image)

    batch_image = solid_png((8, 8, 8, 255))
    emit_images = (solid_png((64, 128, 192, 255)), solid_png((192, 128, 64, 255)))
    batch_images = [batch_image] + list(emit_images)
    left = (-0.9, 0.0, -0.65, 0.65)
    right = (0.0, 0.9, -0.65, 0.65)
    batch_quads = [quad(0, bounds=left, sample=(250, 240)),
                   quad(1, bounds=right, sample=(390, 240))]
    batch_materials = [emissive_material(texture=1, base_texture=0),
                       emissive_material(texture=2, base_texture=0)]
    for material in batch_materials:
        material["pbrMetallicRoughness"]["baseColorFactor"] = [1.0, 1.0, 1.0, 1.0]
    batch_samplers = [{"wrapS": 10497, "wrapT": 10497},
                      {"wrapS": 33071, "wrapT": 33071},
                      {"wrapS": 33648, "wrapT": 33648}]
    batch_textures = [{"source": 0, "sampler": 0}, {"source": 1, "sampler": 1},
                      {"source": 2, "sampler": 2}]
    write_glb(output, "emissive-batch-mixed", batch_quads, batch_materials,
              batch_images, batch_samplers, batch_textures,
              ("KHR_materials_emissive_strength",))
    base = srgb_to_linear(8)
    batch_refs = [factor_material(tuple(base + srgb_to_linear(v) for v in (64, 128, 192))),
                  factor_material(tuple(base + srgb_to_linear(v) for v in (192, 128, 64)))]
    write_glb(output, "emissive-batch-mixed-reference",
              [quad(0, bounds=left, sample=(250, 240)), quad(1, bounds=right, sample=(390, 240))],
              batch_refs)

    sampler_pixels = ((192, 128, 64, 255), (64, 192, 128, 255),
                      (128, 64, 192, 255), (32, 160, 224, 255))
    sampler_image = make_png([sampler_pixels[:2], sampler_pixels[2:]])
    sampler_uv = (1.25, 0.75)
    sampler_left = (-0.9, 0.0, -0.65, 0.65)
    sampler_right = (0.0, 0.9, -0.65, 0.65)
    sampler_materials = [emissive_material(texture=0), emissive_material(texture=1)]
    sampler_quads = [quad(0, uv=sampler_uv, bounds=sampler_left, sample=(250, 240)),
                     quad(1, uv=sampler_uv, bounds=sampler_right, sample=(390, 240))]
    sampler_settings = [{"wrapS": 10497, "wrapT": 10497,
                         "minFilter": 9728, "magFilter": 9728},
                        {"wrapS": 33071, "wrapT": 33071,
                         "minFilter": 9728, "magFilter": 9728}]
    sampler_textures = [{"source": 0, "sampler": 0}, {"source": 0, "sampler": 1}]
    write_glb(output, "emissive-sampler-mixed", sampler_quads,
              sampler_materials, [sampler_image], sampler_settings,
              sampler_textures)
    sampler_expected = [tuple(srgb_to_linear(value) for value in (128, 64, 192)),
                        tuple(srgb_to_linear(value) for value in (32, 160, 224))]
    write_glb(output, "emissive-sampler-mixed-reference",
              [quad(0, bounds=sampler_left, sample=(250, 240)),
               quad(1, bounds=sampler_right, sample=(390, 240))],
              [factor_material(sampler_expected[0]), factor_material(sampler_expected[1])])

    factor_image = solid_png((128, 128, 128, 255))
    factor_materials = [emissive_material((0.5, 1.0, 1.0), texture=0),
                        emissive_material((1.0, 0.5, 0.5), 2.0, texture=0)]
    factor_quads = [quad(0, bounds=left, sample=(250, 240)),
                    quad(1, bounds=right, sample=(390, 240))]
    write_glb(output, "emissive-factor-mixed", factor_quads, factor_materials,
              [factor_image], [{}], [{"source": 0, "sampler": 0}],
              ("KHR_materials_emissive_strength",))
    factor_sample = srgb_to_linear(128)
    factor_references = [factor_material((factor_sample * 0.5, factor_sample, factor_sample)),
                         factor_material((factor_sample * 2.0, factor_sample, factor_sample))]
    write_glb(output, "emissive-factor-mixed-reference", factor_quads,
              factor_references)

    checker = make_png([[(0, 0, 0, 255) if (x // 4) % 2 == 0 else (255, 255, 255, 255)
                         for x in range(64)] for _ in range(64)])
    rho = 256.0
    mip_uv_quad = quad(0, rho_u=rho, width=64)
    mip_material = emissive_material(texture=0)
    mip_sampler = {"minFilter": 9987, "magFilter": 9729,
                   "wrapS": 33071, "wrapT": 33071}
    write_glb(output, "emissive-mip-9987", [mip_uv_quad], [mip_material],
              [checker], [mip_sampler], [{"source": 0, "sampler": 0}],
              ("KHR_materials_emissive_strength",))
    mip_value = srgb_to_linear(188)
    write_glb(output, "emissive-mip-9987-reference", [mip_uv_quad],
              [factor_material((mip_value,) * 3)])
    no_mip_sampler = {"minFilter": 9729, "magFilter": 9729,
                      "wrapS": 33071, "wrapT": 33071}
    write_glb(output, "emissive-mip-9987-no-mip", [mip_uv_quad], [mip_material],
              [checker], [no_mip_sampler], [{"source": 0, "sampler": 0}],
              ("KHR_materials_emissive_strength",))

    mask_base = make_png([[(255, 255, 255, 0), (255, 255, 255, 255)]])
    mask_emit = solid_png((64, 160, 224, 255))
    mask_actual = emissive_material(texture=1, base_texture=0, mask=True)
    mask_actual["pbrMetallicRoughness"]["baseColorFactor"] = [0.0, 0.0, 0.0, 1.0]
    mask_quad = quad(0)
    mask_quad["uv0"] = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))
    write_glb(output, "emissive-mask", [mask_quad], [mask_actual],
              [mask_base, mask_emit], [{}], [{"source": 0}, {"source": 1}],
              ("KHR_materials_emissive_strength",))
    mask_visible = tuple(srgb_to_linear(v) for v in (64, 160, 224))
    mask_ref_quad = quad(0, bounds=(0.0, 0.9, -0.65, 0.65))
    write_glb(output, "emissive-mask-reference", [mask_ref_quad],
              [factor_material(mask_visible)])

    # HDR値4をBloom on/offで比較し、baseColor参照側はambientを4にする。
    hdr_material = emissive_material((1.0, 1.0, 1.0), 4.0)
    hdr_material["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": 4.0}}
    hdr_quad = quad(0)
    write_glb(output, "emissive-hdr-bloom", [hdr_quad], [hdr_material],
              extensions_used=("KHR_materials_emissive_strength",))
    write_glb(output, "emissive-hdr-bloom-reference", [hdr_quad],
              [factor_material((1.0, 1.0, 1.0))])

    stress_base = solid_png((0, 0, 0, 255))
    stress_emit = solid_png((96, 160, 224, 255))
    stress_material = emissive_material(texture=1, base_texture=0)
    write_glb(output, "emissive-stress", [quad(0)], [stress_material],
              [stress_base, stress_emit], [{}, {}],
              [{"source": 0, "sampler": 0}, {"source": 1, "sampler": 1}],
              ("KHR_materials_emissive_strength",))
    stress_expected = tuple(srgb_to_linear(v) for v in (96, 160, 224))
    write_glb(output, "emissive-stress-reference", [quad(0)],
              [factor_material(stress_expected)])


def generate_negative(output):
    """読み込みで拒否するemissive参照・数値・変換を生成する。"""
    base_quad = quad()
    texture = solid_png((255, 255, 255, 255))
    source_material = emissive_material(texture=0)
    missing_uv = dict(base_quad)
    missing_uv["uv0"] = ((0.5, 0.5),) * 4
    # texCoord 1を指定し、meshからTEXCOORD_1を省く負例。
    missing_material = emissive_material(texture=0, texcoord=1)
    write_glb(output, "emissive-missing-uv", [missing_uv], [missing_material],
              [texture], [{}], [{"source": 0}])
    invalid_uv_material = emissive_material(texture=0, texcoord=2)
    write_glb(output, "emissive-invalid-texcoord", [base_quad], [invalid_uv_material],
              [texture], [{}], [{"source": 0}])
    invalid_texture = emissive_material(texture=9)
    write_glb(output, "emissive-invalid-texture-ref", [base_quad], [invalid_texture],
              [texture], [{}], [{"source": 0}])
    invalid_image = emissive_material(texture=0)
    write_glb(output, "emissive-invalid-image-ref", [base_quad], [invalid_image],
              [texture], [{}], [{"source": 9}])
    transform = emissive_material(texture=0)
    transform["emissiveTexture"]["extensions"] = {
        "KHR_texture_transform": {"offset": [0.1, 0.2]}}
    write_glb(output, "emissive-transform", [base_quad], [transform],
              [texture], [{}], [{"source": 0}],
              ("KHR_texture_transform", "KHR_materials_emissive_strength"))
    bad_factor = emissive_material((1.5, 0.0, 0.0))
    write_glb(output, "emissive-invalid-factor", [base_quad], [bad_factor])
    overflow_factor = emissive_material((1.0e39, 0.0, 0.0))
    write_glb(output, "emissive-invalid-factor-overflow", [base_quad], [overflow_factor])
    bad_strength = emissive_material((1.0, 1.0, 1.0), -1.0)
    write_glb(output, "emissive-invalid-strength", [base_quad], [bad_strength],
              extensions_used=("KHR_materials_emissive_strength",))
    overflow_strength = emissive_material((1.0, 1.0, 1.0), 1.0e39)
    write_glb(output, "emissive-invalid-strength-overflow", [base_quad],
              [overflow_strength], extensions_used=("KHR_materials_emissive_strength",))


def generate(output):
    """全emissive GPU fixtureを出力先へ生成する。"""
    output.mkdir(parents=True, exist_ok=True)
    generate_valid(output)
    generate_negative(output)
    return len(tuple(output.glob("emissive-*.glb")))


def main():
    """出力先を受け取りGLB fixturesを作る。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    print(generate(args.output_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
