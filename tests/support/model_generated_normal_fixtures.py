#!/usr/bin/env python3
"""GLB missing-NORMAL GPU comparison fixturesを作る。"""

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import model_normal_fixtures as normal
import model_tangent_fixtures as tangent


FLAT_POSITIONS = normal.POSITIONS
FLAT_UV0 = normal.UV0
FLAT_UV1 = normal.UV1
FLAT_INDICES = (0, 2, 1, 0, 3, 2)
FLAT_NORMAL = (0.0, 0.0, -1.0)
FLAT_TANGENT = (1.0, 0.0, 0.0, 1.0)


def normalize(vector):
    """3成分vectorを単位長へする。"""
    length = sum(value * value for value in vector) ** 0.5
    if length == 0.0:
        raise ValueError("cannot normalize a zero vector")
    return tuple(value / length for value in vector)


def cross(left, right):
    """2方向から右手cross productを計算する。"""
    return (left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0])


def face_normal(positions, triangle):
    """triangleの頂点位置から向きを保った幾何法線を計算する。"""
    first, second, third = triangle
    edge_a = tuple(positions[second][axis] - positions[first][axis]
                   for axis in range(3))
    edge_b = tuple(positions[third][axis] - positions[first][axis]
                   for axis in range(3))
    return normalize(cross(edge_a, edge_b))


def face_tangent(positions, uvs, triangle, normal_value):
    """UVの解析微分と既知法線から接線の4成分を計算する。"""
    first, second, third = triangle
    edge_a = tuple(positions[second][axis] - positions[first][axis]
                   for axis in range(3))
    edge_b = tuple(positions[third][axis] - positions[first][axis]
                   for axis in range(3))
    uv_a = tuple(uvs[second][axis] - uvs[first][axis] for axis in range(2))
    uv_b = tuple(uvs[third][axis] - uvs[first][axis] for axis in range(2))
    determinant = uv_a[0] * uv_b[1] - uv_b[0] * uv_a[1]
    if determinant == 0.0:
        raise ValueError("reference UV triangle is degenerate")
    tangent = tuple((edge_a[axis] * uv_b[1] - edge_b[axis] * uv_a[1]) /
                    determinant for axis in range(3))
    projection = sum(tangent[axis] * normal_value[axis]
                     for axis in range(3))
    tangent = tuple(tangent[axis] - projection * normal_value[axis]
                    for axis in range(3))
    tangent = normalize(tangent)
    bitangent = tuple((edge_b[axis] * uv_a[0] - edge_a[axis] * uv_b[0]) /
                      determinant for axis in range(3))
    cross_normal_tangent = cross(normal_value, tangent)
    handedness = -1.0 if sum(cross_normal_tangent[axis] * bitangent[axis]
                              for axis in range(3)) < 0.0 else 1.0
    return tangent + (handedness,)


def write_model(output, name, positions, uv0, uv1, indices,
                normals=None, tangents=None, normal_map=True,
                normal_texcoord=0, node=None, include_normal=True):
    """共通writerでnormal/TANGENTの有無を指定したGLBを保存する。"""
    tangent.make_document(output, f"{name}.glb", positions, uv0, uv1,
                          indices, tangents, normal_texcoord, node,
                          normal_map, include_normal, normals)


def make_flat_pair(output, name, uv0=FLAT_UV0, uv1=FLAT_UV1,
                   normal_texcoord=0, positions=FLAT_POSITIONS,
                   indices=FLAT_INDICES, node=None,
                   actual_tangents=None, normal_map=True,
                   reference_normal=FLAT_NORMAL,
                   reference_tangent=FLAT_TANGENT):
    """平らな面の自動法線と明示参照を作る。"""
    write_model(output, name, positions, uv0, uv1, indices,
                tangents=actual_tangents, normal_map=normal_map,
                normal_texcoord=normal_texcoord, node=node,
                include_normal=False)
    reference_normals = [reference_normal] * len(positions)
    reference_tangents = reference_tangent
    write_model(output, f"{name}-reference", positions, uv0, uv1,
                indices, reference_normals, reference_tangents,
                normal_map, normal_texcoord, node)


def make_sharp_fold(output):
    """共有edgeのflat normal生成をcornerごとの参照と比較する。"""
    positions = ((0.0, 0.0, 0.0), (0.35, 0.0, 0.0),
                 (0.0, 0.35, 0.0), (0.0, -0.35, 0.26))
    uvs = ((0.0, 1.0), (1.0, 1.0), (0.0, 0.0), (1.0, 0.0))
    indices = (0, 2, 1, 0, 1, 3)
    first_normal = face_normal(positions, indices[:3])
    second_normal = face_normal(positions, indices[3:])
    write_model(output, "generated-normal-sharp-fold", positions, uvs,
                uvs, indices, include_normal=False)
    corner_indices = list(indices)
    split_positions = [positions[index] for index in corner_indices]
    split_uvs = [uvs[index] for index in corner_indices]
    split_normals = [first_normal] * 3 + [second_normal] * 3
    first_tangent = face_tangent(positions, uvs, indices[:3], first_normal)
    second_tangent = face_tangent(positions, uvs, indices[3:], second_normal)
    split_tangents = [first_tangent] * 3 + [second_tangent] * 3
    write_model(output, "generated-normal-sharp-fold-reference",
                split_positions, split_uvs, split_uvs, None, split_normals,
                lambda index: split_tangents[index])


def generate(output):
    """positive pair、control、negative fixtureを保存する。"""
    output.mkdir(parents=True, exist_ok=True)
    make_flat_pair(output, "generated-normal-flat")
    make_flat_pair(output, "generated-normal-tangent-ignored",
                   actual_tangents=(0.0, 1.0, 0.0, -1.0))
    make_flat_pair(output, "generated-normal-uv1",
                   normal_texcoord=1,
                   reference_tangent=(-1.0, 0.0, 0.0, -1.0))
    make_flat_pair(output, "generated-normal-coplanar-shared")
    make_flat_pair(output, "generated-normal-node-transform",
                   node={"scale": [-1.5, 0.5, 1.0],
                         "rotation": [0.0, 0.0, 0.3826834324,
                                      0.9238795325]})
    tiny_positions = [tuple(value * 100000.0 for value in position)
                      for position in FLAT_POSITIONS]
    make_flat_pair(output, "generated-normal-tiny-node",
                   positions=tiny_positions,
                   node={"scale": [0.00001, 0.00001, 0.00001]})
    nonindexed_positions = [FLAT_POSITIONS[index] for index in FLAT_INDICES]
    nonindexed_uv0 = [FLAT_UV0[index] for index in FLAT_INDICES]
    nonindexed_uv1 = [FLAT_UV1[index] for index in FLAT_INDICES]
    make_flat_pair(output, "generated-normal-non-indexed",
                   positions=nonindexed_positions, uv0=nonindexed_uv0,
                   uv1=nonindexed_uv1, indices=None)
    make_flat_pair(output, "generated-normal-no-map", normal_map=False)
    make_flat_pair(output, "generated-normal-no-map-tangent-ignored",
                   actual_tangents=(0.0, 1.0, 0.0, -1.0),
                   normal_map=False)
    degenerate_uv = ((0.5, 0.5),) * len(FLAT_POSITIONS)
    make_flat_pair(output, "generated-normal-degenerate-uv-no-map",
                   uv0=degenerate_uv, uv1=degenerate_uv,
                   normal_map=False)
    make_unused_vertex_pair(output)
    make_sharp_fold(output)
    make_sharp_fold_averaged_control(output)
    make_singular_node_cases(output)
    generate_negative(output)
    return len(tuple(output.glob("generated-normal-*.glb")))


def make_unused_vertex_pair(output):
    """未参照の大きな位置を出力頂点から除くpairを作る。"""
    positions = list(FLAT_POSITIONS) + [(1.0e20, 1.0e20, 0.0)]
    uv0 = list(FLAT_UV0) + [(0.0, 0.0)]
    uv1 = list(FLAT_UV1) + [(0.0, 0.0)]
    write_model(output, "generated-normal-unused-source-vertex",
                positions, uv0, uv1, FLAT_INDICES, include_normal=False)
    write_model(output, "generated-normal-unused-source-vertex-reference",
                FLAT_POSITIONS, FLAT_UV0, FLAT_UV1, FLAT_INDICES,
                [FLAT_NORMAL] * 4, FLAT_TANGENT)


def make_singular_node_cases(output):
    """法線自動生成と既存法線経路の特異node動作を分けて保存する。"""
    node = {"scale": [0.0, 1.0, 1.0]}
    write_model(output, "generated-normal-singular-no-map",
                FLAT_POSITIONS, FLAT_UV0, FLAT_UV1, FLAT_INDICES,
                normal_map=False, node=node, include_normal=False)
    write_model(output, "generated-normal-authored-singular-no-map",
                FLAT_POSITIONS, FLAT_UV0, FLAT_UV1, FLAT_INDICES,
                [FLAT_NORMAL] * 4, None, False, 0, node)


def make_sharp_fold_averaged_control(output):
    """正しい面法線をedgeで平均した対照GLBを作る。"""
    positions = ((0.0, 0.0, 0.0), (0.35, 0.0, 0.0),
                 (0.0, 0.35, 0.0), (0.0, -0.35, 0.26))
    uvs = ((0.0, 1.0), (1.0, 1.0), (0.0, 0.0), (1.0, 0.0))
    indices = (0, 2, 1, 0, 1, 3)
    first_normal = face_normal(positions, indices[:3])
    second_normal = face_normal(positions, indices[3:])
    shared_normal = normalize(tuple(first_normal[axis] + second_normal[axis]
                                    for axis in range(3)))
    split_indices = [index for index in indices]
    split_positions = [positions[index] for index in split_indices]
    split_uvs = [uvs[index] for index in split_indices]
    smooth_normals = ([shared_normal, first_normal, shared_normal] +
                      [shared_normal, shared_normal, second_normal])
    first_tangent = face_tangent(positions, uvs, indices[:3], first_normal)
    second_tangent = face_tangent(positions, uvs, indices[3:], second_normal)
    split_tangents = [first_tangent] * 3 + [second_tangent] * 3
    write_model(output, "generated-normal-sharp-fold-averaged",
                split_positions, split_uvs, split_uvs, None,
                smooth_normals, lambda index: split_tangents[index])


def generate_negative(output):
    """法線生成時に拒否する幾何と明示法線を保存する。"""
    zero_area = ((0.0, 0.0, 0.0),) * 4
    write_model(output, "generated-normal-degenerate-position",
                zero_area, FLAT_UV0, FLAT_UV1, FLAT_INDICES,
                include_normal=False)
    degenerate_uv = ((0.5, 0.5),) * 4
    write_model(output, "generated-normal-degenerate-uv-with-map",
                FLAT_POSITIONS, degenerate_uv, degenerate_uv,
                FLAT_INDICES, include_normal=False)
    write_model(output, "generated-normal-authored-normal-zero",
                FLAT_POSITIONS, FLAT_UV0, FLAT_UV1, FLAT_INDICES,
                [(0.0, 0.0, 0.0)] * 4, None)
    nan_normals = [FLAT_NORMAL] * 3 + [(float("nan"), 0.0, -1.0)]
    write_model(output, "generated-normal-authored-normal-nonfinite",
                FLAT_POSITIONS, FLAT_UV0, FLAT_UV1, FLAT_INDICES,
                nan_normals, None)


def main():
    """fixture出力先を受け取る。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    print(generate(args.output_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
