#!/usr/bin/env python3
"""glTF metallic-roughness textureと基本色画像のGLB fixtureを生成する。"""
import argparse
import json
from pathlib import Path
import struct
import zlib


QUAD_POSITIONS = ((-0.9, -0.65, 0.0), (0.9, -0.65, 0.0),
                  (0.9, 0.65, 0.0), (-0.9, 0.65, 0.0))
NORMALS = ((0.0, 0.0, -1.0),) * 4
UV0 = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))
UV1 = ((1.0, 1.0), (0.0, 1.0), (0.0, 0.0), (1.0, 0.0))
QUAD_INDICES = (0, 1, 2, 0, 2, 3)
BASE_PIXEL = (128, 100, 40, 255)
MR_UNIFORM = (0, 128, 64, 0)
UV1_MR_PIXELS = (
    ((0, 64, 0, 255), (0, 192, 255, 255)),
    ((0, 128, 64, 255), (0, 255, 192, 255)),
)


def png_chunk(kind, payload):
    """PNG chunkへ長さとCRCを付ける。"""
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload)))


def make_png(width, height, pixels):
    """行ごとのRGBA pixel列から8bit PNGを作る。"""
    rows = bytearray()
    for row in pixels:
        rows.append(0)
        for pixel in row:
            rows.extend(pixel)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", zlib.compress(bytes(rows))) +
            png_chunk(b"IEND", b""))


def make_uniform_png(pixel):
    """同じRGBA値を持つ1x1 PNGを作る。"""
    return make_png(1, 1, ((pixel,),))


def make_pattern_png():
    """指定された4領域の値を持つ64x32 metallic-roughness PNGを作る。"""
    rows = []
    for row_index in range(2):
        row = []
        for pixel in UV1_MR_PIXELS[row_index]:
            row.extend((pixel,) * 32)
        rows.extend((row,) * 16)
    return make_png(64, 32, rows)


def append_view(binary, views, payload, target=None):
    """buffer内へ4byte整列したpayloadを追加し、view番号を返す。"""
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


def texture_view(index, texcoord=0, transformed=False):
    """texture indexと座標セット、任意の未対応変換を記述する。"""
    result = {"index": index}
    if texcoord:
        result["texCoord"] = texcoord
    if transformed:
        result["extensions"] = {"KHR_texture_transform": {"offset": [0.1, 0.0]}}
    return result


def make_material(base_texture=0, mr_texture=None, mr_texcoord=0,
                  metallic=1.0, roughness=1.0, base_color=None,
                  alpha_mode=None, alpha_cutoff=None, mr_transform=False):
    """PBR係数と基本色・metallic-roughness画像参照を持つ材質を作る。"""
    if base_color is None:
        base_color = [1.0, 1.0, 1.0, 1.0]
    pbr = {"baseColorFactor": base_color, "metallicFactor": metallic,
           "roughnessFactor": roughness}
    if base_texture is not None:
        pbr["baseColorTexture"] = texture_view(base_texture)
    if mr_texture is not None:
        pbr["metallicRoughnessTexture"] = texture_view(
            mr_texture, mr_texcoord, mr_transform)
    material = {"pbrMetallicRoughness": pbr}
    if alpha_mode is not None:
        material["alphaMode"] = alpha_mode
    if alpha_cutoff is not None:
        material["alphaCutoff"] = alpha_cutoff
    return material


def quad_geometry(bounds=QUAD_POSITIONS, include_uv1=True):
    """頂点と三角形indexを持つquad geometryを返す。"""
    positions = bounds
    uv0 = UV0
    uv1 = UV1
    return positions, uv0, uv1, QUAD_INDICES, include_uv1


def quadrant_geometry():
    """比較用に画面を4象限へ分けたquad列を作る。"""
    bounds = ((-0.9, 0.0, 0.0, 0.65), (0.0, 0.9, 0.0, 0.65),
              (-0.9, 0.0, -0.65, 0.0), (0.0, 0.9, -0.65, 0.0))
    positions = []
    indices = []
    uv0 = []
    for left, right, bottom, top in bounds:
        first = len(positions)
        positions.extend(((left, bottom, 0.0), (right, bottom, 0.0),
                          (right, top, 0.0), (left, top, 0.0)))
        uv0.extend(UV0)
        indices.append(tuple(first + index for index in QUAD_INDICES))
    return tuple(positions), tuple(uv0), None, tuple(indices), False


def paired_geometry():
    """左右に分けた2つのquad geometryを作る。"""
    bounds = ((-0.9, -0.05), (0.05, 0.9))
    positions = []
    uv0 = []
    indices = []
    for left, right in bounds:
        first = len(positions)
        positions.extend(((left, -0.65, 0.0), (right, -0.65, 0.0),
                          (right, 0.65, 0.0), (left, 0.65, 0.0)))
        uv0.extend(UV0)
        indices.append(tuple(first + index for index in QUAD_INDICES))
    return tuple(positions), tuple(uv0), None, tuple(indices), False


def make_glb(output, materials, image_specs, texture_sources,
             primitive_materials=None, geometry="quad", include_uv1=True,
             bad_image_mime=None, transform_extension=False):
    """材質・画像・geometryをまとめた埋込buffer GLB 2.0を作る。"""
    binary = bytearray()
    views = []
    accessors = []
    if geometry == "quadrants":
        positions, uv0, uv1, index_sets, include_uv1 = quadrant_geometry()
    elif geometry == "paired":
        positions, uv0, uv1, index_sets, include_uv1 = paired_geometry()
    else:
        positions = QUAD_POSITIONS
        uv0 = UV0
        uv1 = UV1
        index_sets = (QUAD_INDICES,)
    normals = (NORMALS[0],) * len(positions)
    position_view = append_view(binary, views, pack_vectors(positions, 3), 34962)
    position_accessor = add_accessor(accessors, position_view, 5126, len(positions),
                                     "VEC3", [-0.9, -0.65, 0.0], [0.9, 0.65, 0.0])
    normal_view = append_view(binary, views, pack_vectors(normals, 3), 34962)
    normal_accessor = add_accessor(accessors, normal_view, 5126, len(normals), "VEC3")
    uv0_view = append_view(binary, views, pack_vectors(uv0, 2), 34962)
    uv0_accessor = add_accessor(accessors, uv0_view, 5126, len(uv0), "VEC2",
                                [0.0, 0.0], [1.0, 1.0])
    uv1_accessor = None
    if include_uv1:
        uv1_view = append_view(binary, views, pack_vectors(uv1, 2), 34962)
        uv1_accessor = add_accessor(accessors, uv1_view, 5126, len(uv1), "VEC2",
                                    [0.0, 0.0], [1.0, 1.0])

    index_accessors = []
    for indices in index_sets:
        index_view = append_view(binary, views,
                                 struct.pack("<" + "H" * len(indices), *indices), 34963)
        index_accessors.append(add_accessor(accessors, index_view, 5123, len(indices),
                                             "SCALAR", [min(indices)], [max(indices)]))

    images = []
    for payload, mime_type in image_specs:
        image_view = append_view(binary, views, payload)
        actual_mime = bad_image_mime if bad_image_mime is not None and len(images) == 1 else mime_type
        images.append({"bufferView": image_view, "mimeType": actual_mime})
    samplers = [{"wrapS": 33071, "wrapT": 33071}] if images else []
    textures = [{"sampler": 0, "source": source} for source in texture_sources]

    attributes = {"POSITION": position_accessor, "NORMAL": normal_accessor,
                  "TEXCOORD_0": uv0_accessor}
    if include_uv1 and uv1_accessor is not None:
        attributes["TEXCOORD_1"] = uv1_accessor
    if primitive_materials is None:
        primitive_materials = [0] * len(index_accessors)
    primitives = []
    for primitive_index, material_index in enumerate(primitive_materials):
        primitives.append({"attributes": attributes,
                           "indices": index_accessors[primitive_index],
                           "material": material_index})
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
    if images:
        document["images"] = images
        document["textures"] = textures
        document["samplers"] = samplers
    if transform_extension:
        document["extensionsUsed"] = ["KHR_texture_transform"]
        document["extensionsRequired"] = ["KHR_texture_transform"]
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total_length = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total_length) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.write_bytes(glb)


def base_image():
    """全材質で共有する1x1基本色画像を返す。"""
    return make_uniform_png(BASE_PIXEL), "image/png"


def mr_image(pixel):
    """指定MR値を持つ1x1画像を返す。"""
    return make_uniform_png(pixel), "image/png"


def one_material_case(name, material, images, texture_sources,
                      include_uv1=True, bad_image_mime=None,
                      transform_extension=False):
    """標準quadの単一材質GLB設定をまとめる。"""
    return (name, [material], images, texture_sources, [0], "quad",
            include_uv1, bad_image_mime, transform_extension)


def generate_fixtures(directory):
    """正常・境界・拒否対象のmetallic-roughness fixture群を書き出す。"""
    directory.mkdir(parents=True, exist_ok=True)
    base = base_image()
    uniform_mr = mr_image(MR_UNIFORM)
    ignored_ra = mr_image((255, 128, 64, 255))
    mixed_mr = mr_image((0, 64, 192, 255))
    cases = [
        one_material_case("uniform-mr", make_material(0, 1), [base, uniform_mr], [0, 1]),
        one_material_case("factor-reference", make_material(0, metallic=64 / 255,
                           roughness=128 / 255), [base], [0]),
        one_material_case("no-mr-default", make_material(0), [base], [0]),
        one_material_case("ignored-ra", make_material(0, 1, metallic=1,
                           roughness=1), [base, ignored_ra], [0, 1]),
        one_material_case("scaled-mr", make_material(0, 1, metallic=0.5,
                           roughness=0.5), [base, uniform_mr], [0, 1]),
        one_material_case("scaled-reference", make_material(0, metallic=32 / 255,
                           roughness=64 / 255), [base], [0]),
        one_material_case("shared-image", make_material(0, 0), [base], [0]),
        one_material_case("shared-reference", make_material(0, metallic=40 / 255,
                           roughness=100 / 255), [base], [0]),
        one_material_case("mr-only", make_material(None, 0, base_color=[0.4, 0.2, 0.1, 1.0]),
                           [uniform_mr], [0]),
        one_material_case("mr-only-reference", make_material(None, metallic=64 / 255,
                           roughness=128 / 255, base_color=[0.4, 0.2, 0.1, 1.0]), [], []),
        one_material_case("uv1-pattern", make_material(0, 1, mr_texcoord=1),
                           [base, (make_pattern_png(), "image/png")], [0, 1]),
        ("uv1-reference", [
            make_material(0, metallic=1.0, roughness=192 / 255),
            make_material(0, metallic=0.0, roughness=64 / 255),
            make_material(0, metallic=192 / 255, roughness=1.0),
            make_material(0, metallic=64 / 255, roughness=128 / 255),
         ], [base], [0], [0, 1, 2, 3], "quadrants", False, None, False),
        ("mixed-pairs", [make_material(0, 1), make_material(0, 2)],
         [base, uniform_mr, mixed_mr], [0, 1, 2], [0, 1], "paired", False, None, False),
        ("mixed-reference", [
            make_material(0, metallic=64 / 255, roughness=128 / 255),
            make_material(0, metallic=192 / 255, roughness=64 / 255),
         ], [base], [0], [0, 1], "paired", False, None, False),
        one_material_case("masked-mr", make_material(0, 1, alpha_mode="MASK",
                           alpha_cutoff=0.5), [base, uniform_mr], [0, 1]),
        one_material_case("missing-mr-uv", make_material(0, 1, mr_texcoord=1),
                           [base, uniform_mr], [0, 1], include_uv1=False),
        one_material_case("transform-mr", make_material(0, 1, mr_transform=True),
                           [base, uniform_mr], [0, 1], transform_extension=True),
        one_material_case("bad-mr-image", make_material(0, 1),
                           [base, uniform_mr], [0, 1], bad_image_mime="image/jpeg"),
        one_material_case("bad-mr-index", make_material(0, 999), [base], [0]),
    ]
    for (name, materials, images, texture_sources, primitive_materials,
         geometry, include_uv1, bad_image_mime, transform_extension) in cases:
        make_glb(directory / f"{name}.glb", materials, images, texture_sources,
                 primitive_materials, geometry, include_uv1, bad_image_mime,
                 transform_extension)
    return tuple(directory / f"{case[0]}.glb" for case in cases)


def main():
    """指定directoryへmetallic-roughness材質fixtureを生成する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    generate_fixtures(args.output_dir.resolve())


if __name__ == "__main__":
    main()
