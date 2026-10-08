#!/usr/bin/env python3
"""共通GLB animation sourceのCPU契約fixtureを生成する。"""

import argparse
import json
from pathlib import Path
import struct

from model_normal_fixtures import uniform_png


def append_view(binary, views, payload, target=None):
    """binaryへ4byte整列したbufferViewを追加する。"""
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    binary.extend(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
    if target is not None:
        view["target"] = target
    views.append(view)
    return len(views) - 1


def append_accessor(binary, views, accessors, values, components, kind,
                    target=None, component_type=5126, normalized=False):
    """値をaccessorへ追加し、そのindexを返す。"""
    fmt = {5121: "B", 5123: "H", 5126: "f"}[component_type]
    payload = struct.pack("<" + fmt * len(values), *values)
    view = append_view(binary, views, payload, target)
    accessor = {"bufferView": view, "componentType": component_type,
                "count": len(values) // components, "type": kind}
    if normalized:
        accessor["normalized"] = True
    accessors.append(accessor)
    return len(accessors) - 1


def make_glb(output, malformed=None, mixed_skin_instance=False):
    """node TRS、skin、morph、3種interpolationを持つGLBを保存する。"""
    binary = bytearray()
    views = []
    accessors = []

    positions = append_accessor(
        binary, views, accessors,
        (0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
         0.0, 1.0, 0.0, 0.0, 0.0, 1.0),
        3, "VEC3", 34962)
    joints = append_accessor(
        binary, views, accessors,
        (0, 1, 0, 0, 0, 1, 0, 0,
         0, 1, 0, 0, 0, 1, 0, 0),
        4, "VEC4", 34962, component_type=5121)
    weights = append_accessor(
        binary, views, accessors,
        (0.5, 0.5, 0.0, 0.0) * 4, 4, "VEC4", 34962)
    indices = append_accessor(
        binary, views, accessors, (0, 1, 2, 0, 3, 1), 1, "SCALAR", 34963,
        component_type=5123)
    morph_position = append_accessor(
        binary, views, accessors,
        (0.0, 0.5, 0.0) * 4, 3, "VEC3", 34962)

    times = append_accessor(binary, views, accessors, (0.0, 2.0), 1, "SCALAR")
    translation_values = append_accessor(
        binary, views, accessors,
        (0.0, 0.0, 0.0, 2.0, 0.0, 0.0), 3, "VEC3")
    rotation_values = append_accessor(
        binary, views, accessors,
        (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0), 4, "VEC4")
    cubic_scale_values = append_accessor(
        binary, views, accessors,
        (0.0, 0.0, 0.0, 1.0, 1.0, 1.0,
         0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
         3.0, 3.0, 3.0, 0.0, 0.0, 0.0), 3, "VEC3")
    morph_values = append_accessor(
        binary, views, accessors, (0.0, 1.0), 1, "SCALAR")

    if malformed == "bad-time":
        time_view = views[accessors[times]["bufferView"]]
        struct.pack_into("<ff", binary, time_view["byteOffset"], 0.0, 0.0)
    elif malformed == "bad-translation-count":
        accessors[translation_values]["count"] = 1

    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
        "meshes": [{"weights": [0.0], "extras": {"targetNames": ["lift"]}, "primitives": [{
            "attributes": {"POSITION": positions,
                          "JOINTS_0": joints, "WEIGHTS_0": weights},
            "indices": indices,
            "targets": [{"POSITION": morph_position}],
        }]}],
        "nodes": [
            {"name": "scene_root", "children": [1, 2]},
            {"name": "mesh_node", "mesh": 0, "skin": 0,
             "weights": [0.0]},
            {"name": "joint_root", "children": [3]},
            {"name": "joint_child", "translation": [0.0, 0.0, 0.0]},
        ],
        "skins": [{"name": "two_joint_skin", "joints": [2, 3],
                   "skeleton": 2}],
        "scenes": [{"nodes": [0]}], "scene": 0,
        "animations": [{"name": "mixed_interpolation",
            "samplers": [
                {"input": times, "output": translation_values,
                 "interpolation": "LINEAR"},
                {"input": times, "output": rotation_values,
                 "interpolation": "STEP"},
                {"input": times, "output": cubic_scale_values,
                 "interpolation": "CUBICSPLINE"},
                {"input": times, "output": morph_values,
                 "interpolation": "LINEAR"},
            ],
            "channels": [
                {"sampler": 0, "target": {"node": 2,
                                             "path": "translation"}},
                {"sampler": 1, "target": {"node": 3, "path": "rotation"}},
                {"sampler": 2, "target": {"node": 3, "path": "scale"}},
                {"sampler": 3, "target": {"node": 1, "path": "weights"}},
            ]}],
    }
    if mixed_skin_instance:
        document["nodes"][0]["children"].append(4)
        document["nodes"].append({"name": "unskinned_mesh_node", "mesh": 0,
                                  "translation": [3.0, 0.0, 0.0],
                                  "weights": [0.0]})
    json_payload = json.dumps(document, separators=(",", ":"),
                              allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    data = (struct.pack("<4sII", b"glTF", 2, total) +
            struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
            struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)
    output.write_bytes(data)


def make_generated_normal_morph(output):
    """法線なしprimitiveをmorphで傾け、生成frameを検査するGLBを作る。"""
    binary = bytearray()
    views = []
    accessors = []
    positions = append_accessor(binary, views, accessors,
                                (0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                 0.0, 1.0, 0.0), 3, "VEC3", 34962)
    tangents = append_accessor(binary, views, accessors,
                               (0.0, 1.0, 0.0, 1.0) * 3, 4, "VEC4", 34962)
    texcoords = append_accessor(binary, views, accessors,
                                (0.0, 0.0, 1.0, 0.0, 0.0, 1.0), 2, "VEC2", 34962)
    indices = append_accessor(binary, views, accessors, (0, 1, 2), 1,
                              "SCALAR", 34963, component_type=5123)
    morph_position = append_accessor(binary, views, accessors,
                                     (0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                      0.0, 0.0, 1.0), 3, "VEC3", 34962)
    morph_position_second = append_accessor(binary, views, accessors,
                                            (0.0, 0.0, 0.0, 0.0, 0.0, 1.0,
                                             0.0, 0.0, 0.0), 3, "VEC3", 34962)
    morph_tangent = append_accessor(binary, views, accessors,
                                    (0.0, 4.0, 0.0) * 3, 3, "VEC3", 34962)
    morph_tangent_second = append_accessor(binary, views, accessors,
                                           (0.0, 4.0, 0.0) * 3, 3, "VEC3", 34962)
    time = append_accessor(binary, views, accessors, (0.0, 1.0), 1, "SCALAR")
    weight = append_accessor(binary, views, accessors,
                             (0.0, 0.0, 1.0, 0.0), 1, "SCALAR")
    image_view = append_view(binary, views,
                             uniform_png((128, 128, 255, 255)))
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
        "images": [{"bufferView": image_view, "mimeType": "image/png"}],
        "textures": [{"source": 0}],
        "materials": [{"normalTexture": {"index": 0}}],
        "meshes": [{"weights": [0.0, 0.0], "primitives": [{
            "attributes": {"POSITION": positions, "TANGENT": tangents,
                          "TEXCOORD_0": texcoords},
            "indices": indices,
            "material": 0,
            "targets": [{"POSITION": morph_position,
                         "TANGENT": morph_tangent},
                        {"POSITION": morph_position_second,
                         "TANGENT": morph_tangent_second}],
        }]}],
        "nodes": [{"name": "morph_mesh", "mesh": 0,
                   "weights": [0.0, 0.0]}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
        "animations": [{"name": "bend", "samplers": [
            {"input": time, "output": weight, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 0,
                                                           "path": "weights"}}]}],
    }
    json_payload = json.dumps(document, separators=(",", ":"),
                              allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    output.write_bytes(struct.pack("<4sII", b"glTF", 2, total) +
                       struct.pack("<II", len(json_payload), 0x4E4F534A) +
                       json_payload +
                       struct.pack("<II", len(binary_payload), 0x004E4942) +
                       binary_payload)


def make_generated_tangent_morph(output):
    """NORMALありTANGENTなしprimitiveでmorph別の接線を生成するGLBを作る。"""
    binary = bytearray()
    views = []
    accessors = []
    positions = append_accessor(binary, views, accessors,
                                (0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                 0.0, 1.0, 0.0), 3, "VEC3", 34962)
    normals = append_accessor(binary, views, accessors,
                              (0.0, 0.0, 1.0) * 3, 3, "VEC3", 34962)
    texcoords = append_accessor(binary, views, accessors,
                                (0.0, 0.0, 1.0, 0.0, 0.0, 1.0), 2, "VEC2", 34962)
    indices = append_accessor(binary, views, accessors, (0, 1, 2), 1,
                              "SCALAR", 34963, component_type=5123)
    morph_position = append_accessor(binary, views, accessors,
                                     (0.0, 0.0, 0.0, -1.0, 1.0, 0.0,
                                      0.0, -1.0, 1.0), 3, "VEC3", 34962)
    morph_normal = append_accessor(binary, views, accessors,
                                   (1.0, 0.0, -1.0) * 3, 3, "VEC3", 34962)
    time = append_accessor(binary, views, accessors, (0.0, 1.0), 1, "SCALAR")
    weight = append_accessor(binary, views, accessors, (0.0, 1.0), 1, "SCALAR")
    image_view = append_view(binary, views,
                             uniform_png((128, 128, 255, 255)))
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
        "images": [{"bufferView": image_view, "mimeType": "image/png"}],
        "textures": [{"source": 0}],
        "materials": [{"normalTexture": {"index": 0}}],
        "meshes": [{"weights": [0.0], "primitives": [{
            "attributes": {"POSITION": positions, "NORMAL": normals,
                          "TEXCOORD_0": texcoords},
            "indices": indices,
            "material": 0,
            "targets": [{"POSITION": morph_position,
                         "NORMAL": morph_normal}],
        }]}],
        "nodes": [{"name": "morph_mesh", "mesh": 0, "weights": [0.0]}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
        "animations": [{"name": "bend", "samplers": [
            {"input": time, "output": weight, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 0,
                                                          "path": "weights"}}]}],
    }
    json_payload = json.dumps(document, separators=(",", ":"),
                              allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    output.write_bytes(struct.pack("<4sII", b"glTF", 2, total) +
                       struct.pack("<II", len(json_payload), 0x4E4F534A) +
                       json_payload +
                       struct.pack("<II", len(binary_payload), 0x004E4942) +
                       binary_payload)


def make_optional_morph_attributes(output):
    """targetごとの属性省略をゼロ差分として扱うGLBを作る。"""
    binary = bytearray()
    views = []
    accessors = []
    positions = append_accessor(binary, views, accessors,
                                (0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                 0.0, 1.0, 0.0), 3, "VEC3", 34962)
    normals = append_accessor(binary, views, accessors,
                              (0.0, 0.0, 1.0) * 3, 3, "VEC3", 34962)
    indices = append_accessor(binary, views, accessors, (0, 1, 2), 1,
                              "SCALAR", 34963, component_type=5123)
    first_position = append_accessor(binary, views, accessors,
                                     (0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                      0.0, 0.0, 1.0), 3, "VEC3", 34962)
    second_position = append_accessor(binary, views, accessors,
                                      (0.0, 0.0, 0.0) * 3, 3, "VEC3", 34962)
    second_normal = append_accessor(binary, views, accessors,
                                    (0.0, 1.0, 0.0) * 3, 3, "VEC3", 34962)
    time = append_accessor(binary, views, accessors, (0.0, 1.0), 1, "SCALAR")
    weight = append_accessor(binary, views, accessors,
                             (0.0, 0.0, 1.0, 1.0), 1, "SCALAR")
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
        "meshes": [{"weights": [0.0, 0.0], "primitives": [{
            "attributes": {"POSITION": positions, "NORMAL": normals},
            "indices": indices,
            "targets": [{"POSITION": first_position},
                        {"POSITION": second_position,
                         "NORMAL": second_normal}],
        }]}],
        "nodes": [{"name": "morph_mesh", "mesh": 0,
                   "weights": [0.0, 0.0]}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
        "animations": [{"name": "optional_target_attributes", "samplers": [
            {"input": time, "output": weight, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 0,
                                                          "path": "weights"}}]}],
    }
    json_payload = json.dumps(document, separators=(",", ":"),
                              allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    output.write_bytes(struct.pack("<4sII", b"glTF", 2, total) +
                       struct.pack("<II", len(json_payload), 0x4E4F534A) +
                       json_payload +
                       struct.pack("<II", len(binary_payload), 0x004E4942) +
                       binary_payload)


def main():
    """指定directoryへfixture群を生成する。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    make_glb(args.output_dir / "glb-animation-full.glb")
    make_glb(args.output_dir / "glb-animation-bad-time.glb", "bad-time")
    make_glb(args.output_dir / "glb-animation-bad-output.glb",
             "bad-translation-count")
    make_glb(args.output_dir / "glb-animation-mixed-skin-instances.glb",
             mixed_skin_instance=True)
    make_generated_normal_morph(args.output_dir /
                                "glb-animation-generated-normal-morph.glb")
    make_generated_tangent_morph(args.output_dir /
                                 "glb-animation-generated-tangent-morph.glb")
    make_optional_morph_attributes(args.output_dir /
                                   "glb-animation-optional-morph-attributes.glb")


if __name__ == "__main__":
    main()
