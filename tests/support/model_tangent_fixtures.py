#!/usr/bin/env python3
"""接線自動生成のGPU比較に使うGLBを作る。"""

import argparse
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import model_normal_fixtures as normal


POSITIONS = normal.POSITIONS
CANONICAL_UV = normal.UV0
NORMAL_VALUE = (0.0, 0.0, -1.0)
NORMAL_PIXEL = (224, 176, 255, 255)
REVERSED_TRIANGLES = (0, 2, 1, 0, 3, 2)


def append_vectors(binary, views, accessors, values, components, kind,
                   minimum=None, maximum=None):
    """float属性をbufferViewとaccessorに追加する。"""
    payload = normal.pack_vectors(values, components)
    view = normal.append_view(binary, views, payload, 34962)
    return normal.add_accessor(accessors, view, 5126, len(values), kind,
                               minimum, maximum)


def tangent_frame(positions, uvs, indices):
    """平面triangleのUV微分から解析的な接線と向きを求める。"""
    first, second, third = (indices[0], indices[1], indices[2])
    edge_a = tuple(positions[second][axis] - positions[first][axis]
                   for axis in range(3))
    edge_b = tuple(positions[third][axis] - positions[first][axis]
                   for axis in range(3))
    uv_a = tuple(uvs[second][axis] - uvs[first][axis] for axis in range(2))
    uv_b = tuple(uvs[third][axis] - uvs[first][axis] for axis in range(2))
    determinant = uv_a[0] * uv_b[1] - uv_b[0] * uv_a[1]
    if abs(determinant) < 1.0e-8:
        raise ValueError("tangent reference UV triangle is degenerate")
    tangent = tuple((edge_a[axis] * uv_b[1] - edge_b[axis] * uv_a[1]) /
                    determinant for axis in range(3))
    normal_vector = NORMAL_VALUE
    projection = sum(tangent[axis] * normal_vector[axis] for axis in range(3))
    tangent = tuple(tangent[axis] - projection * normal_vector[axis]
                    for axis in range(3))
    length = sum(value * value for value in tangent) ** 0.5
    tangent = tuple(value / length for value in tangent)
    bitangent = tuple((edge_b[axis] * uv_a[0] - edge_a[axis] * uv_b[0]) /
                      determinant for axis in range(3))
    cross = (normal_vector[1] * tangent[2] - normal_vector[2] * tangent[1],
             normal_vector[2] * tangent[0] - normal_vector[0] * tangent[2],
             normal_vector[0] * tangent[1] - normal_vector[1] * tangent[0])
    handedness = -1.0 if sum(cross[axis] * bitangent[axis]
                             for axis in range(3)) < 0.0 else 1.0
    return tangent + (handedness,)


def make_document(output, filename, positions, uv0, uv1, indices,
                  authored_tangent, normal_texcoord=0, node=None,
                  normal_map=True, include_normal=True, normal_values=None):
    """共通の頂点配列を使うGLBを保存する。"""
    binary = bytearray()
    views = []
    accessors = []
    minimum = [min(position[axis] for position in positions) for axis in range(3)]
    maximum = [max(position[axis] for position in positions) for axis in range(3)]
    position = append_vectors(binary, views, accessors, positions, 3, "VEC3",
                              minimum, maximum)
    normals = normal_values if normal_values is not None else [NORMAL_VALUE] * len(positions)
    normal_accessor = None
    if include_normal:
        normal_accessor = append_vectors(binary, views, accessors, normals, 3, "VEC3")
    uv0_accessor = append_vectors(binary, views, accessors, uv0, 2, "VEC2")
    uv1_accessor = None
    if uv1 is not None:
        uv1_accessor = append_vectors(binary, views, accessors, uv1, 2, "VEC2")
    tangent_accessor = None
    if authored_tangent is not None:
        tangents = [authored_tangent(index) if callable(authored_tangent)
                    else authored_tangent for index in range(len(positions))]
        tangent_accessor = append_vectors(binary, views, accessors, tangents,
                                           4, "VEC4")
    attributes = {"POSITION": position, "TEXCOORD_0": uv0_accessor}
    if normal_accessor is not None:
        attributes["NORMAL"] = normal_accessor
    if uv1_accessor is not None:
        attributes["TEXCOORD_1"] = uv1_accessor
    if tangent_accessor is not None:
        attributes["TANGENT"] = tangent_accessor
    primitive = {"attributes": attributes, "material": 0}
    if indices is not None:
        index_view = normal.append_view(binary, views,
                                        struct.pack("<" + "H" * len(indices), *indices),
                                        34963)
        index_accessor = normal.add_accessor(
            accessors, index_view, 5123, len(indices), "SCALAR",
            [min(indices)], [max(indices)])
        primitive["indices"] = index_accessor

    base_image = normal.uniform_png(normal.BASE_PIXEL)
    image_payloads = [base_image]
    material = {"pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
        "metallicFactor": 0.0, "roughnessFactor": 1.0,
        "baseColorTexture": {"index": 0}}}
    textures = [{"source": 0, "sampler": 0}]
    if normal_map:
        image_payloads.append(normal.uniform_png(NORMAL_PIXEL))
        textures.append({"source": 1, "sampler": 0})
        material["normalTexture"] = {"index": 1, "texCoord": normal_texcoord,
                                      "scale": 1.0}
    image_views = [normal.append_view(binary, views, payload)
                   for payload in image_payloads]
    document = {"asset": {"version": "2.0"},
                "buffers": [{"byteLength": len(binary)}],
                "bufferViews": views, "accessors": accessors,
                "images": [{"bufferView": view, "mimeType": "image/png"}
                           for view in image_views],
                "samplers": [{"wrapS": 33071, "wrapT": 33071}],
                "textures": textures, "materials": [material],
                "meshes": [{"primitives": [primitive]}],
                "nodes": [{"mesh": 0, **(node or {})}],
                "scenes": [{"nodes": [0]}], "scene": 0}
    document["buffers"][0]["byteLength"] = len(binary)
    json_payload = json.dumps(document, separators=(",", ":"),
                              allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    glb = (struct.pack("<4sII", b"glTF", 2, total) +
           struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
           struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.joinpath(filename).write_bytes(glb)


def make_pair(output, name, uv0=CANONICAL_UV, uv1=normal.UV1,
              normal_texcoord=0, tangent=(1.0, 0.0, 0.0, 1.0),
              indices=REVERSED_TRIANGLES, positions=POSITIONS,
              node=None, reference_tangent=None):
    """TANGENTなしGLBと解析的に作った接線参照GLBを対にする。"""
    make_document(output, f"{name}.glb", positions, uv0, uv1, indices,
                 None, normal_texcoord, node)
    make_document(output, f"{name}-reference.glb", positions, uv0, uv1,
                 indices, reference_tangent or tangent, normal_texcoord, node)


def make_seam_pair(output):
    """共有indexでUV向きが反転する面と分割済みの解析参照を作る。"""
    positions = list(POSITIONS)
    uvs = ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (2.0, 0.5))
    indices = REVERSED_TRIANGLES
    make_document(output, "tangent-shared-seam.glb", positions, uvs,
                  None, indices, None, 0)
    split_positions = [positions[index] for index in indices]
    split_uvs = [uvs[index] for index in indices]
    tangents = []
    for first in (0, 3):
        frame = tangent_frame(split_positions[first:first + 3],
                              split_uvs[first:first + 3], (0, 1, 2))
        tangents.extend((frame,) * 3)
    make_document(output, "tangent-shared-seam-reference.glb",
                  split_positions, split_uvs, None, None,
                  lambda index: tangents[index], 0)


def make_weighted_pair(output, name, node=None):
    """共有edgeの角度重みを解析した接線参照を作る。"""
    source_positions = ((0.0, 0.0, 0.0), (-0.35, 0.35, 0.0),
                        (0.35, 0.0, 0.0), (0.0, -0.35, 0.0))
    source_uvs = ((0.0, 0.0), (-1.0, 1.0), (1.0, 0.0), (-1.0, 0.0))
    source_indices = (0, 1, 2, 0, 3, 1)
    shared_tangent = (0.7071067812, 0.7071067812, 0.0, -1.0)
    reference_tangents = (shared_tangent, shared_tangent,
                          (1.0, 0.0, 0.0, -1.0),
                          (0.0, 1.0, 0.0, -1.0))
    make_document(output, f"{name}.glb", source_positions, source_uvs,
                  source_uvs, source_indices, None, 0, node)
    make_document(output, f"{name}-reference.glb", source_positions,
                  source_uvs, source_uvs, source_indices,
                  lambda index: reference_tangents[index], 0, node)


def make_weighted_wrong_order_control(output):
    """変換後の角度で平均した接線を正当accessorとして保存する。"""
    source_positions = ((0.0, 0.0, 0.0), (-0.35, 0.35, 0.0),
                        (0.35, 0.0, 0.0), (0.0, -0.35, 0.0))
    source_uvs = ((0.0, 0.0), (-1.0, 1.0), (1.0, 0.0), (-1.0, 0.0))
    source_indices = (0, 1, 2, 0, 3, 1)
    node = {"scale": [2.0, 1.0, 1.0]}
    wrong_world_tangents = (
        (0.7962759590, 0.6049335477, 0.0, -1.0),
        (0.5620965571, 0.8270716175, 0.0, -1.0),
        (1.0, 0.0, 0.0, -1.0),
        (0.0, 1.0, 0.0, -1.0))
    source_tangents = []
    for tangent in wrong_world_tangents:
        source_x = tangent[0] * 0.5
        source_y = tangent[1]
        length = math.hypot(source_x, source_y)
        source_tangents.append((source_x / length, source_y / length,
                                0.0, tangent[3]))
    make_document(output, "tangent-weighted-node-wrong-order.glb",
                  source_positions, source_uvs, source_uvs, source_indices,
                  lambda index: source_tangents[index], 0, node)


def make_unused_vertex_pair(output):
    """参照外の異常法線頂点を出力頂点へ含めないfixtureを作る。"""
    source_positions = list(POSITIONS) + [(1.0e20, 1.0e20, 0.0)]
    source_uv0 = list(CANONICAL_UV) + [(0.0, 0.0)]
    source_uv1 = list(normal.UV1) + [(0.0, 0.0)]
    source_normals = [NORMAL_VALUE] * 4 + [(0.0, 0.0, 0.0)]
    make_document(output, "tangent-unused-source-vertex.glb",
                  source_positions, source_uv0, source_uv1,
                  REVERSED_TRIANGLES, None, 0,
                  normal_values=source_normals)
    make_document(output, "tangent-unused-source-vertex-reference.glb",
                  POSITIONS, CANONICAL_UV, normal.UV1,
                  REVERSED_TRIANGLES, (1.0, 0.0, 0.0, 1.0), 0)


def generate_valid(output):
    """canonical、鏡映、回転UV、UV1、変換、seamを作る。"""
    make_pair(output, "tangent-canonical")
    mirror_uv = ((1.0, 1.0), (0.0, 1.0), (0.0, 0.0), (1.0, 0.0))
    make_pair(output, "tangent-mirror-u", uv0=mirror_uv,
              tangent=(-1.0, 0.0, 0.0, -1.0))
    rotated_uv = ((0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0))
    make_pair(output, "tangent-rotated-uv", uv0=rotated_uv,
              tangent=(0.0, 1.0, 0.0, 1.0))
    make_pair(output, "tangent-uv1", uv1=rotated_uv, normal_texcoord=1,
              tangent=(0.0, 1.0, 0.0, 1.0))
    transformed_node = {"scale": [-1.5, 0.5, 1.0],
                        "rotation": [0.0, 0.0, 0.3826834324, 0.9238795325]}
    make_pair(output, "tangent-node-transform", node=transformed_node)
    tiny_source_positions = [tuple(value * 100000.0 for value in position)
                             for position in POSITIONS]
    tiny_node = {"scale": [0.00001, 0.00001, 0.00001]}
    make_pair(output, "tangent-tiny-node-scale",
              positions=tiny_source_positions, node=tiny_node)
    nonindexed_positions = [POSITIONS[index] for index in REVERSED_TRIANGLES]
    nonindexed_uv0 = [CANONICAL_UV[index] for index in REVERSED_TRIANGLES]
    nonindexed_uv1 = [normal.UV1[index] for index in REVERSED_TRIANGLES]
    make_pair(output, "tangent-non-indexed", positions=nonindexed_positions,
              uv0=nonindexed_uv0, uv1=nonindexed_uv1, indices=None)
    make_seam_pair(output)
    make_weighted_pair(output, "tangent-weighted-source")
    make_weighted_pair(output, "tangent-weighted-node",
                       {"scale": [2.0, 1.0, 1.0]})
    make_weighted_wrong_order_control(output)
    make_unused_vertex_pair(output)

    make_document(output, "tangent-no-normal-map.glb", POSITIONS,
                  CANONICAL_UV, normal.UV1, REVERSED_TRIANGLES, None, 0,
                  normal_map=False)


def generate_negative(output):
    """NORMAL/UV/形状が不正なGLBを作る。"""
    # これらの入力は自動接線生成へ進まず、CPU loaderで拒否する。
    missing_normal = POSITIONS
    make_document(output, "tangent-missing-normal.glb", missing_normal,
                  CANONICAL_UV, normal.UV1, REVERSED_TRIANGLES, None, 0,
                  include_normal=False)
    # NORMAL属性をJSONから外し、選択UVだけ欠けるケースを作る。
    make_document(output, "tangent-missing-normal-uv.glb", POSITIONS,
                  CANONICAL_UV, None, REVERSED_TRIANGLES, None, 1)
    degenerate_uv = ((0.5, 0.5),) * 4
    make_document(output, "tangent-degenerate-uv.glb", POSITIONS,
                  degenerate_uv, normal.UV1, REVERSED_TRIANGLES, None, 0)
    zero_area = ((0.0, 0.0, 0.0),) * 4
    make_document(output, "tangent-zero-area.glb", zero_area, CANONICAL_UV,
                  normal.UV1, REVERSED_TRIANGLES, None, 0)
    nonfinite_normals = [NORMAL_VALUE] * 3 + [(float("nan"), 0.0, -1.0)]
    make_document(output, "tangent-nonfinite-normal.glb", POSITIONS,
                  CANONICAL_UV, normal.UV1, REVERSED_TRIANGLES, None, 0,
                  normal_values=nonfinite_normals)
    make_document(output, "tangent-malformed-authored.glb", POSITIONS,
                  CANONICAL_UV, normal.UV1, REVERSED_TRIANGLES,
                  (0.0, 0.0, 0.0, 1.0), 0)


def generate(output):
    """全positiveとnegative fixtureを保存する。"""
    output.mkdir(parents=True, exist_ok=True)
    generate_valid(output)
    generate_negative(output)
    return len(tuple(output.glob("tangent-*.glb")))


def main():
    """GLB出力先を受け取る。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    print(generate(args.output_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
