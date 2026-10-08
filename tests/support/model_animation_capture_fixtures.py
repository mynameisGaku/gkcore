#!/usr/bin/env python3
"""GPU animation capture用の小さなGLB fixtureを生成する。"""

import argparse
import json
from pathlib import Path
import struct


def make_glb(path, positions, animations=(), skin=False, morph=False, default_morph=0.0):
    """指定形状、skin、morph、animationを持つ自己完結GLBを保存する。"""
    binary = bytearray()
    views = []
    accessors = []

    def add_accessor(values, components, kind, component_type=5126, target=None):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        fmt = {5123: "H", 5126: "f"}[component_type]
        binary.extend(struct.pack("<" + fmt * len(values), *values))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(binary) - offset}
        if target is not None:
            view["target"] = target
        views.append(view)
        accessor = {"bufferView": len(views) - 1, "componentType": component_type,
                    "count": len(values) // components, "type": kind}
        if kind == "VEC3" and values:
            accessor["min"] = [min(values[index::3]) for index in range(3)]
            accessor["max"] = [max(values[index::3]) for index in range(3)]
        accessors.append(accessor)
        return len(accessors) - 1

    position_accessor = add_accessor(tuple(value for point in positions for value in point), 3, "VEC3", target=34962)
    normal_accessor = add_accessor((0.0, 0.0, 1.0) * len(positions), 3, "VEC3", target=34962)
    indices_accessor = add_accessor((0, 1, 2), 1, "SCALAR", 5123, 34963)
    attributes = {"POSITION": position_accessor, "NORMAL": normal_accessor}
    primitive = {"attributes": attributes, "indices": indices_accessor, "material": 0}
    mesh = {"primitives": [primitive]}
    nodes = [{"name": "AnimatedRoot", "children": [1]}, {"name": "Mesh", "mesh": 0}]
    skins = []
    if skin:
        joints_accessor = add_accessor((0, 0, 0, 0) * len(positions), 4, "VEC4", 5123, 34962)
        weights_accessor = add_accessor((1.0, 0.0, 0.0, 0.0) * len(positions), 4, "VEC4", target=34962)
        attributes["JOINTS_0"] = joints_accessor
        attributes["WEIGHTS_0"] = weights_accessor
        nodes[1]["skin"] = 0
        identity = add_accessor((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1), 16, "MAT4")
        skins = [{"joints": [0], "inverseBindMatrices": identity}]
    if morph:
        delta = add_accessor((0.0, 0.4, 0.0) * len(positions), 3, "VEC3", target=34962)
        primitive["targets"] = [{"POSITION": delta}]
        mesh["weights"] = [default_morph]
        mesh["extras"] = {"targetNames": ["Lift"]}
        nodes[1]["weights"] = [default_morph]

    animation_records = []
    for name, path_name, values, kind in animations:
        time_accessor = add_accessor((0.0, 2.0), 1, "SCALAR")
        output_accessor = add_accessor(values, 1 if kind == "SCALAR" else 3, kind)
        record = next((item for item in animation_records if item["name"] == name), None)
        if record is None:
            record = {"name": name, "samplers": [], "channels": []}
            animation_records.append(record)
        sampler = len(record["samplers"])
        record["samplers"].append({"input": time_accessor, "output": output_accessor, "interpolation": "LINEAR"})
        record["channels"].append({"sampler": sampler, "target": {"node": 1 if path_name == "weights" else 0, "path": path_name}})

    document = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views, "accessors": accessors, "materials": [{"pbrMetallicRoughness": {
            "baseColorFactor": [0.9, 0.15, 0.08, 1.0], "metallicFactor": 0.0, "roughnessFactor": 1.0}}],
        "meshes": [mesh], "nodes": nodes, "scenes": [{"nodes": [0]}], "scene": 0}
    if skins:
        document["skins"] = skins
    if animation_records:
        document["animations"] = animation_records
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, total) +
        struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload +
        struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)


def main(argv=None):
    """fixture directoryへ実形状と独立した静的参照形状を作る。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args(argv)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    triangle = ((0.7, 0.25, 0.0), (1.3, 0.25, 0.0), (1.0, 0.85, 0.0))
    write_obj_sequence(args.output_dir)
    make_glb(args.output_dir / "translation-actual.glb", triangle,
        (("move_x", "translation", (0, 0, 0, 0.5, 0, 0), "VEC3"),))
    make_glb(args.output_dir / "translation-reference.glb", triangle)
    make_glb(args.output_dir / "blend-actual.glb", triangle,
        (("move_x", "translation", (0, 0, 0, 0.5, 0, 0), "VEC3"),
         ("move_y", "translation", (0, 0, 0, 0, 0.5, 0), "VEC3")))
    make_glb(args.output_dir / "blend-reference.glb", triangle)
    morph_triangle = triangle
    make_glb(args.output_dir / "skin-morph-actual.glb", morph_triangle,
         (("skin_morph", "translation", (0, 0, 0, 0.5, 0, 0), "VEC3"),
         ("skin_morph", "weights", (0, 1), "SCALAR")), skin=True, morph=True)
    make_glb(args.output_dir / "skin-morph-reference.glb",
        tuple((x + 0.25, y + 0.2, z) for x, y, z in morph_triangle))
    make_glb(args.output_dir / "rest-morph-actual.glb", triangle, morph=True, default_morph=0.5)
    make_glb(args.output_dir / "rest-morph-reference.glb",
        tuple((x, y + 0.2, z) for x, y, z in triangle))
    make_glb(args.output_dir / "rest-morph-zero.glb", triangle, morph=True)
    ik_shape = ((1.7, -0.3, 0.0), (2.3, -0.3, 0.0), (2.0, 0.3, 0.0))
    make_ik_glb(args.output_dir / "ik-actual.glb", ik_shape)
    make_glb(args.output_dir / "ik-reference.glb",
        tuple((x - 1.0, y + 1.0, z) for x, y, z in ik_shape))


def write_obj_sequence(directory):
    """頂点順を保ったframe0とXへ0.5移動するframe1を保存する。"""
    first = ((-0.3, -0.25, 0.0), (0.3, -0.25, 0.0), (0.0, 0.35, 0.0))
    frames = (first, tuple((x + 0.5, y, z) for x, y, z in first))
    for frame_index, vertices in enumerate(frames):
        lines = ["v %.6f %.6f %.6f" % vertex for vertex in vertices]
        lines.extend(("vn 0 0 1", "f 1//1 2//1 3//1", ""))
        (directory / f"sequence-{frame_index}.obj").write_text("\n".join(lines), encoding="utf-8")


def make_ik_glb(path, positions):
    """Root/Middle/Endの3関節とEndへ全頂点を結ぶskinを保存する。"""
    binary = bytearray()
    views = []
    accessors = []

    def add_accessor(values, components, kind, component_type=5126, target=None):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        fmt = {5123: "H", 5126: "f"}[component_type]
        binary.extend(struct.pack("<" + fmt * len(values), *values))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(binary) - offset}
        if target is not None:
            view["target"] = target
        views.append(view)
        accessors.append({"bufferView": len(views) - 1, "componentType": component_type,
                          "count": len(values) // components, "type": kind})
        return len(accessors) - 1

    position = add_accessor(tuple(value for point in positions for value in point), 3, "VEC3", target=34962)
    normal = add_accessor((0.0, 0.0, 1.0) * len(positions), 3, "VEC3", target=34962)
    joints = add_accessor((2, 0, 0, 0) * len(positions), 4, "VEC4", 5123, 34962)
    weights = add_accessor((1.0, 0.0, 0.0, 0.0) * len(positions), 4, "VEC4", target=34962)
    indices = add_accessor((0, 1, 2), 1, "SCALAR", 5123, 34963)
    inverse = add_accessor((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, 0, 0, 1,
                            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, 0, 0, 1), 16, "MAT4")
    document = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views, "accessors": accessors,
        "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.15, 0.08, 1.0], "metallicFactor": 0.0, "roughnessFactor": 1.0}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": position, "NORMAL": normal, "JOINTS_0": joints, "WEIGHTS_0": weights}, "indices": indices, "material": 0}]}],
        "nodes": [{"name": "Root", "children": [1]}, {"name": "Middle", "translation": [1, 0, 0], "children": [2]},
                  {"name": "End", "translation": [1, 0, 0]}, {"name": "Mesh", "mesh": 0, "skin": 0}],
        "skins": [{"joints": [0, 1, 2], "skeleton": 0, "inverseBindMatrices": inverse}],
        "scenes": [{"nodes": [0, 3]}], "scene": 0}
    json_payload = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_payload += b" " * ((-len(json_payload)) % 4)
    binary_payload = bytes(binary) + b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, total) + struct.pack("<II", len(json_payload), 0x4E4F534A) + json_payload + struct.pack("<II", len(binary_payload), 0x004E4942) + binary_payload)


if __name__ == "__main__":
    main()
