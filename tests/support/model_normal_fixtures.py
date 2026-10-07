#!/usr/bin/env python3
"""GLB normal textureの有効・無効fixtureを生成する。"""
import argparse
import json
from pathlib import Path
import struct
import zlib


POSITIONS = ((-0.9, -0.65, 0.0), (0.9, -0.65, 0.0),
             (0.9, 0.65, 0.0), (-0.9, 0.65, 0.0))
UV0 = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))
UV1 = ((1.0, 1.0), (0.0, 1.0), (0.0, 0.0), (1.0, 0.0))
INDICES = (0, 1, 2, 0, 2, 3)
BASE_PIXEL = (128, 100, 40, 255)
SHARED_PIXEL = (128, 128, 255, 255)
NORMAL_PIXEL = (192, 192, 255, 255)
PATTERN_PIXELS = (
    ((192, 128, 255, 255), (64, 128, 255, 255)),
    ((128, 192, 255, 255), (128, 64, 255, 255)),
)


def png_chunk(kind, payload):
    """PNG chunkへ長さとCRCを付ける。"""
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload)))


def make_png(width, height, pixels):
    """RGBA pixel列から8bit PNGを作る。"""
    rows = bytearray()
    for row in pixels:
        rows.append(0)
        for pixel in row:
            rows.extend(pixel)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", zlib.compress(bytes(rows))) +
            png_chunk(b"IEND", b""))


def uniform_png(pixel):
    """同じRGBA値を持つ1x1 PNGを作る。"""
    return make_png(1, 1, ((pixel,),))


def pattern_png():
    """指定された四象限pixelを持つ64x32 PNGを作る。"""
    rows = []
    for row_index in range(2):
        row = []
        for pixel in PATTERN_PIXELS[row_index]:
            row.extend((pixel,) * 32)
        rows.extend((row,) * 16)
    return make_png(64, 32, rows)


def append_view(binary, views, payload, target=None):
    """bufferへ4byte整列したpayloadを追加しview indexを返す。"""
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    binary.extend(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
    if target is not None:
        view["target"] = target
    views.append(view)
    return len(views) - 1


def add_accessor(accessors, view, component_type, count, value_type,
                 minimum=None, maximum=None):
    """bufferViewと属性形式をaccessorへ記録する。"""
    accessor = {"bufferView": view, "componentType": component_type,
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


def mapped_normal(pixel, scale=1.0, tangent_sign=1.0):
    """normal pixelと接線向きから基準法線vectorを作る。"""
    x = (pixel[0] / 255.0 * 2.0 - 1.0) * scale
    y = (pixel[1] / 255.0 * 2.0 - 1.0) * scale
    z = pixel[2] / 255.0 * 2.0 - 1.0
    # N=(0,0,-1)、T=(1,0,0)からbitangentを作る。
    vector = (x, -y * tangent_sign, -z)
    length = sum(value * value for value in vector) ** 0.5
    return tuple(value / length for value in vector)


def append_geometry(binary, views, accessors, positions, normals, tangents,
                    uv0, uv1, primitive_ranges):
    """頂点属性とprimitiveごとのindexをGLB binaryへ追加する。"""
    position_view = append_view(binary, views, pack_vectors(positions, 3), 34962)
    position_accessor = add_accessor(accessors, position_view, 5126, len(positions),
                                     "VEC3", [-0.9, -0.65, 0.0], [0.9, 0.65, 0.0])
    normal_accessor = None
    if normals is not None:
        normal_view = append_view(binary, views, pack_vectors(normals, 3), 34962)
        normal_accessor = add_accessor(accessors, normal_view, 5126, len(normals), "VEC3")
    tangent_accessor = None
    if tangents is not None:
        tangent_view = append_view(binary, views, pack_vectors(tangents, 4), 34962)
        tangent_accessor = add_accessor(accessors, tangent_view, 5126,
                                        len(tangents), "VEC4")
    uv0_view = append_view(binary, views, pack_vectors(uv0, 2), 34962)
    uv0_accessor = add_accessor(accessors, uv0_view, 5126, len(uv0), "VEC2",
                                [0.0, 0.0], [1.0, 1.0])
    uv1_accessor = None
    if uv1 is not None:
        uv1_view = append_view(binary, views, pack_vectors(uv1, 2), 34962)
        uv1_accessor = add_accessor(accessors, uv1_view, 5126, len(uv1), "VEC2",
                                    [0.0, 0.0], [1.0, 1.0])
    index_accessors = []
    for _, indices in primitive_ranges:
        local_indices = indices
        index_view = append_view(binary, views,
                                 struct.pack("<" + "H" * len(local_indices), *local_indices),
                                 34963)
        index_accessors.append(add_accessor(accessors, index_view, 5123,
                                            len(local_indices), "SCALAR",
                                            [min(local_indices)], [max(local_indices)]))
    attributes = {"POSITION": position_accessor, "TEXCOORD_0": uv0_accessor}
    if normal_accessor is not None:
        attributes["NORMAL"] = normal_accessor
    if tangent_accessor is not None:
        attributes["TANGENT"] = tangent_accessor
    if uv1_accessor is not None:
        attributes["TEXCOORD_1"] = uv1_accessor
    primitives = [{"attributes": dict(attributes), "indices": index_accessor,
                   "material": 0}
                  for index_accessor in index_accessors]
    return primitives


def make_glb(output, filename, normal_texture=None, normal_scale=1.0,
             normal_texcoord=0, tangent_sign=1.0, include_tangent=True,
             include_normal_uv1=True, base_pixel=BASE_PIXEL,
             node_mirror=False, transform_normal=False, bad_normal_scale=False,
             four_quads=False, reference_pixel=None, reference_scale=1.0,
             reference_sign=1.0, shared=False, shared_image=False,
             bad_image_alpha=False, include_normal=True, parallel_tangent=False,
             metallic_factor=0.0, roughness_factor=1.0,
             zero_tangent=False, node_scale=None, tangent_w_mismatch=False,
             mixed_sign_triangles=False):
    """fixture設定から1つの埋め込みGLBを生成する。"""
    binary = bytearray()
    views = []
    accessors = []
    if mixed_sign_triangles:
        positions = [POSITIONS[0], POSITIONS[1], POSITIONS[2],
                     POSITIONS[0], POSITIONS[2], POSITIONS[3]]
        uv0 = [UV0[0], UV0[1], UV0[2], UV0[0], UV0[2], UV0[3]]
        uv1 = [UV1[0], UV1[1], UV1[2], UV1[0], UV1[2], UV1[3]]
        primitive_ranges = [(0, (0, 1, 2, 3, 4, 5))]
        normals = [mapped_normal(reference_pixel, reference_scale, reference_sign)
                   if reference_pixel else (0.0, 0.0, -1.0)] * 6
    elif four_quads:
        bounds = ((-0.9, 0.0, 0.0, 0.65), (0.0, 0.9, 0.0, 0.65),
                  (-0.9, 0.0, -0.65, 0.0), (0.0, 0.9, -0.65, 0.0))
        positions = []
        uv0 = []
        uv1 = []
        primitive_ranges = []
        for left, right, bottom, top in bounds:
            first = len(positions)
            positions.extend(((left, bottom, 0), (right, bottom, 0),
                              (right, top, 0), (left, top, 0)))
            uv0.extend(UV0)
            uv1.extend(UV1)
            primitive_ranges.append((first, tuple(first + index for index in INDICES)))
        normal_pixels = (PATTERN_PIXELS[0][1], PATTERN_PIXELS[0][0],
                         PATTERN_PIXELS[1][1], PATTERN_PIXELS[1][0])
        normals = [mapped_normal(pixel) for pixel in normal_pixels for _ in range(4)]
    else:
        positions = list(POSITIONS)
        uv0 = list(UV0)
        uv1 = list(UV1) if include_normal_uv1 else None
        primitive_ranges = [(0, INDICES)]
        source_normal = mapped_normal(reference_pixel, reference_scale, reference_sign) if reference_pixel else (0.0, 0.0, -1.0)
        normals = [source_normal] * 4 if include_normal else None
    tangent_w = tangent_sign
    tangents = [(1.0, 0.0, 0.0, tangent_w) for _ in positions] if include_tangent else None
    if tangents is not None and mixed_sign_triangles:
        tangents[3:] = [tangent[:3] + (-1.0,) for tangent in tangents[3:]]
    if tangents is not None and parallel_tangent:
        tangents = [(0.0, 0.0, -1.0, tangent_w) for _ in positions]
    if tangents is not None and zero_tangent:
        tangents = [(0.0, 0.0, 0.0, tangent_w) for _ in positions]
    if tangents is not None and tangent_w_mismatch:
        tangents[0] = tangents[0][:3] + (-tangent_w,)
    if four_quads:
        normals = [mapped_normal(pixel) for pixel in normal_pixels for _ in range(4)]

    primitives = append_geometry(binary, views, accessors, positions, normals,
                                 tangents, uv0, uv1, primitive_ranges)
    base_png = uniform_png(base_pixel)
    images = [(base_png, "image/png")]
    texture_sources = [0]
    if normal_texture is not None and not shared_image:
        if normal_texture == "pattern":
            normal_png = pattern_png()
        else:
            pixel = tuple(normal_texture)
            if bad_image_alpha:
                pixel = pixel[:3] + (0,)
            normal_png = uniform_png(pixel)
        images.append((normal_png, "image/png"))
        texture_sources.append(1)
    materials = [{"pbrMetallicRoughness": {
        "baseColorFactor": [1, 1, 1, 1], "metallicFactor": metallic_factor,
        "roughnessFactor": roughness_factor, "baseColorTexture": {"index": 0}}}]
    if shared:
        materials[0]["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {"index": 0}
    if normal_texture is not None:
        texture_index = 0 if shared_image else 1
        normal_view = {"index": texture_index, "scale": normal_scale,
                       "texCoord": normal_texcoord}
        if transform_normal:
            normal_view["extensions"] = {"KHR_texture_transform": {"offset": [0.1, 0.0]}}
        materials[0]["normalTexture"] = normal_view
    if bad_normal_scale:
        materials[0]["normalTexture"]["scale"] = 1.0e100
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
        "images": [{"bufferView": append_view(binary, views, payload), "mimeType": mime}
                   for payload, mime in images],
        "textures": [{"sampler": 0, "source": source} for source in texture_sources],
        "samplers": [{"wrapS": 33071, "wrapT": 33071}],
        "materials": materials,
        "meshes": [{"primitives": primitives}],
        "nodes": [{"mesh": 0, **({"scale": node_scale} if node_scale is not None else ({"scale": [-1, 1, 1]} if node_mirror else {}))}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    if transform_normal:
        document["extensionsUsed"] = ["KHR_texture_transform"]
        document["extensionsRequired"] = ["KHR_texture_transform"]
    # image view追加後にbuffer長を確定する。
    document["buffers"][0]["byteLength"] = len(binary)
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.joinpath(filename).write_bytes(glb)


def rewrite_glb_json(output, filename, added_texture_sources=None,
                     metallic_roughness_texture=None, normal_texture=None,
                     normal_texcoord=None, normal_transform=False):
    """GLBのJSON chunkだけを変更してtexture役割のaliasを作る。"""
    path = output / filename
    content = path.read_bytes()
    magic, version, _ = struct.unpack_from("<4sII", content, 0)
    json_length, json_kind = struct.unpack_from("<II", content, 12)
    if magic != b"glTF" or version != 2 or json_kind != 0x4E4F534A:
        raise ValueError("生成したGLB headerが不正です")
    json_start = 20
    document = json.loads(content[json_start:json_start + json_length].decode("utf-8"))
    if added_texture_sources:
        document["textures"].extend({"sampler": 0, "source": source}
                                     for source in added_texture_sources)
    material = document["materials"][0]
    if metallic_roughness_texture is not None:
        material["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {
            "index": metallic_roughness_texture, "texCoord": 0}
    if normal_texture is not None:
        material["normalTexture"]["index"] = normal_texture
    if normal_texcoord is not None:
        material["normalTexture"]["texCoord"] = normal_texcoord
    if normal_transform:
        material["normalTexture"]["extensions"] = {
            "KHR_texture_transform": {"offset": [0.1, 0.0]}}
        document["extensionsUsed"] = ["KHR_texture_transform"]
        document["extensionsRequired"] = ["KHR_texture_transform"]
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_chunk = content[json_start + json_length:]
    total = 12 + 8 + len(json_payload) + len(binary_chunk)
    rebuilt = (struct.pack("<4sII", b"glTF", 2, total) +
               struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
               binary_chunk)
    path.write_bytes(rebuilt)


def generate(output):
    """positive22件とnegative12件のnormal fixtureを作る。"""
    output.mkdir(parents=True, exist_ok=True)
    normal = NORMAL_PIXEL
    make_glb(output, "no-normal.glb")
    make_glb(output, "uniform-normal.glb", normal_texture=normal)
    make_glb(output, "normal-reference.glb", reference_pixel=normal)
    make_glb(output, "alpha-ignored.glb", normal_texture=normal, bad_image_alpha=True)
    make_glb(output, "scale-zero.glb", normal_texture=normal, normal_scale=0.0)
    make_glb(output, "scale-two.glb", normal_texture=normal, normal_scale=2.0)
    make_glb(output, "scale-two-reference.glb", reference_pixel=normal, reference_scale=2.0)
    make_glb(output, "scale-negative.glb", normal_texture=normal, normal_scale=-1.0)
    make_glb(output, "scale-negative-reference.glb", reference_pixel=normal, reference_scale=-1.0)
    make_glb(output, "mirrored-tangent.glb", normal_texture=normal, tangent_sign=-1.0)
    make_glb(output, "mirrored-reference.glb", reference_pixel=normal, reference_sign=-1.0)
    make_glb(output, "uv1-pattern.glb", normal_texture="pattern", normal_texcoord=1)
    make_glb(output, "uv1-reference.glb", four_quads=True)
    make_glb(output, "shared-image.glb", normal_texture=SHARED_PIXEL, shared=True, shared_image=True, base_pixel=SHARED_PIXEL, metallic_factor=1.0)
    make_glb(output, "shared-reference.glb", base_pixel=SHARED_PIXEL, reference_pixel=SHARED_PIXEL, metallic_factor=1.0, roughness_factor=SHARED_PIXEL[1] / 255.0)
    make_glb(output, "shared-image-aliases.glb", normal_texture=SHARED_PIXEL, shared=True, shared_image=True, base_pixel=SHARED_PIXEL, normal_texcoord=1, metallic_factor=1.0)
    rewrite_glb_json(output, "shared-image-aliases.glb", added_texture_sources=[0, 0], metallic_roughness_texture=1, normal_texture=2, normal_texcoord=1)
    make_glb(output, "distinct-image-records.glb", normal_texture=SHARED_PIXEL, base_pixel=SHARED_PIXEL, metallic_factor=1.0)
    rewrite_glb_json(output, "distinct-image-records.glb", metallic_roughness_texture=1)
    make_glb(output, "mixed-role-image-alias.glb", normal_texture=SHARED_PIXEL, base_pixel=SHARED_PIXEL, normal_texcoord=1, metallic_factor=1.0)
    rewrite_glb_json(output, "mixed-role-image-alias.glb", added_texture_sources=[1], metallic_roughness_texture=2, normal_texture=1, normal_texcoord=1)
    make_glb(output, "alias-missing-uv.glb", normal_texture=SHARED_PIXEL, shared=True, shared_image=True, base_pixel=SHARED_PIXEL)
    rewrite_glb_json(output, "alias-missing-uv.glb", added_texture_sources=[0], metallic_roughness_texture=1, normal_texture=1, normal_texcoord=2)
    make_glb(output, "alias-transform.glb", normal_texture=SHARED_PIXEL, shared=True, shared_image=True, base_pixel=SHARED_PIXEL)
    rewrite_glb_json(output, "alias-transform.glb", added_texture_sources=[0], metallic_roughness_texture=1, normal_texture=1, normal_transform=True)
    make_glb(output, "node-mirror-normal.glb", normal_texture=normal, node_mirror=True)
    make_glb(output, "node-mirror-reference.glb", reference_pixel=normal, node_mirror=True)
    make_glb(output, "tiny-node-scale.glb", normal_texture=normal, node_scale=[1.0e-5, 1.0e-5, 1.0e-5])
    make_glb(output, "mixed-sign-triangles.glb", normal_texture=normal, mixed_sign_triangles=True)
    make_glb(output, "missing-tangent.glb", normal_texture=normal, include_tangent=False)
    make_glb(output, "missing-normal.glb", normal_texture=normal, include_normal=False)
    make_glb(output, "missing-normal-uv.glb", normal_texture=normal, normal_texcoord=2)
    make_glb(output, "bad-tangent-w.glb", normal_texture=normal, tangent_sign=0.5)
    make_glb(output, "tangent-w-mismatch.glb", normal_texture=normal, tangent_w_mismatch=True)
    make_glb(output, "parallel-tangent.glb", normal_texture=normal, parallel_tangent=True)
    make_glb(output, "zero-tangent.glb", normal_texture=normal, zero_tangent=True)
    make_glb(output, "singular-node.glb", normal_texture=normal, node_scale=[0, 1, 1])
    make_glb(output, "bad-normal-scale.glb", normal_texture=normal, bad_normal_scale=True)
    make_glb(output, "transform-normal.glb", normal_texture=normal, transform_normal=True)
    return 34


def main():
    """fixture出力先を解析してGLBを生成する。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    generate(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
