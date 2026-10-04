#!/usr/bin/env python3
"""Generate small deterministic FBX fixtures used by gkcore resource tests."""

from __future__ import annotations

import base64
import struct
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "assets" / "models"
PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLttAAAAABJRU5ErkJggg=="
)


def array_text(values: list[float | int]) -> str:
    return ", ".join(format(value, ".9g") if isinstance(value, float) else str(value) for value in values)


def ascii_fbx(relative_texture: str) -> bytes:
    return f'''; FBX 7.4.0 project file
FBXHeaderExtension: {{
    FBXHeaderVersion: 1003
    FBXVersion: 7400
}}
GlobalSettings: {{
    Version: 1000
    Properties70: {{
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }}
}}
Objects: {{
    Geometry: 1001, "Geometry::Quad", "Mesh" {{
        GeometryVersion: 124
        Vertices: *12 {{ a: {array_text([0.,0.,0., 1.,0.,0., 1.,1.,0., 0.,1.,0.])} }}
        PolygonVertexIndex: *6 {{ a: 0, 1, -3, 0, 2, -4 }}
        LayerElementNormal: 0 {{
            Version: 101
            Name: ""
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            Normals: *18 {{ a: {array_text([0.,0.,1.] * 6)} }}
        }}
        LayerElementUV: 0 {{
            Version: 101
            Name: "UVChannel_1"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            UV: *12 {{ a: {array_text([0.,0., 1.,0., 1.,1., 0.,0., 1.,1., 0.,1.])} }}
        }}
        LayerElementMaterial: 0 {{
            Version: 101
            Name: ""
            MappingInformationType: "ByPolygon"
            ReferenceInformationType: "IndexToDirect"
            Materials: *2 {{ a: 0, 1 }}
        }}
        Layer: 0 {{
            Version: 100
            LayerElement: {{ Type: "LayerElementNormal" TypedIndex: 0 }}
            LayerElement: {{ Type: "LayerElementUV" TypedIndex: 0 }}
            LayerElement: {{ Type: "LayerElementMaterial" TypedIndex: 0 }}
        }}
    }}
    Model: 1002, "Model::Quad", "Mesh" {{
        Version: 232
        Properties70: {{
            P: "Lcl Translation", "Lcl Translation", "", "A", 3, 4, 5
            P: "Lcl Scaling", "Lcl Scaling", "", "A", 2, 1, 1
        }}
        Shading: T
        Culling: "CullingOff"
    }}
    Model: 1003, "Model::Parent", "Null" {{
        Version: 232
        Properties70: {{ P: "Lcl Translation", "Lcl Translation", "", "A", 10, 0, 0 }}
    }}
    Material: 2001, "Material::Warm", "" {{
        Version: 102
        ShadingModel: "phong"
        Properties70: {{
            P: "DiffuseColor", "Color", "", "A", 0.5, 0.6, 0.7
            P: "DiffuseFactor", "double", "Number", "", 0.8
            P: "TransparencyFactor", "double", "Number", "", 0.25
        }}
    }}
    Material: 2002, "Material::Cool", "" {{
        Version: 102
        ShadingModel: "phong"
        Properties70: {{
            P: "DiffuseColor", "Color", "", "A", 0.2, 0.4, 0.6
            P: "DiffuseFactor", "double", "Number", "", 1
        }}
    }}
    Texture: 3001, "Texture::Warm", "" {{
        Type: "TextureVideoClip"
        Properties70: {{ P: "UVSet", "KString", "", "", "UVChannel_1" }}
        FileName: "{relative_texture}"
        RelativeFilename: "{relative_texture}"
        UVSet: "UVChannel_1"
        ModelUVTranslation: 0,0
        ModelUVScaling: 1,1
        Texture_Alpha_Source: "None"
        Cropping: 0,0,0,0
    }}
    Texture: 3002, "Texture::Cool", "" {{
        Type: "TextureVideoClip"
        Properties70: {{ P: "UVSet", "KString", "", "", "UVChannel_1" }}
        FileName: "{relative_texture}"
        RelativeFilename: "{relative_texture}"
        UVSet: "UVChannel_1"
        ModelUVTranslation: 0,0
        ModelUVScaling: 1,1
        Texture_Alpha_Source: "None"
        Cropping: 0,0,0,0
    }}
    Video: 4001, "Video::Warm", "Clip" {{
        Type: "Clip"
        Filename: "{relative_texture}"
        RelativeFilename: "{relative_texture}"
        Content: ,
    }}
    Video: 4002, "Video::Cool", "Clip" {{
        Type: "Clip"
        Filename: "{relative_texture}"
        RelativeFilename: "{relative_texture}"
        Content: ,
    }}
}}
Connections: {{
    C: "OO", 1001, 1002
    C: "OO", 1002, 1003
    C: "OO", 2001, 1002
    C: "OO", 2002, 1002
    C: "OP", 3001, 2001, "DiffuseColor"
    C: "OO", 4001, 3001
    C: "OP", 3002, 2002, "DiffuseColor"
    C: "OO", 4002, 3002
}}
'''.encode("utf-8")


@dataclass
class Node:
    name: str
    props: list[tuple[str, object]] = field(default_factory=list)
    children: list["Node"] = field(default_factory=list)


def prop_string(value: str) -> tuple[str, object]:
    return ("S", value)


def prop_int(value: int) -> tuple[str, object]:
    return ("L", value)


def prop_float(value: float) -> tuple[str, object]:
    return ("D", value)


def prop_array(code: str, values: list[float | int]) -> tuple[str, object]:
    return ("A" + code, values)


def prop_raw(value: bytes) -> tuple[str, object]:
    return ("R", value)


def binary_property(code: str, value: object) -> bytes:
    if code == "S":
        encoded = str(value).encode("utf-8")
        return b"S" + struct.pack("<I", len(encoded)) + encoded
    if code == "L":
        return b"L" + struct.pack("<q", int(value))
    if code == "I":
        return b"I" + struct.pack("<i", int(value))
    if code == "D":
        return b"D" + struct.pack("<d", float(value))
    if code == "R":
        raw = bytes(value)
        return b"R" + struct.pack("<I", len(raw)) + raw
    if code.startswith("A"):
        array_code = code[1:]
        formats = {"d": "d", "f": "f", "i": "i", "l": "q"}
        payload = struct.pack("<" + formats[array_code] * len(value), *value)
        return array_code.encode("ascii") + struct.pack("<III", len(value), 0, len(payload)) + payload
    raise ValueError(f"unsupported FBX property {code}")


def binary_node(node: Node, base_offset: int) -> bytes:
    name = node.name.encode("ascii")
    props = b"".join(binary_property(code, value) for code, value in node.props)
    child_offset = base_offset + 13 + len(name) + len(props)
    body_parts = []
    for child in node.children:
        child_bytes = binary_node(child, child_offset)
        body_parts.append(child_bytes)
        child_offset += len(child_bytes)
    body = b"".join(body_parts)
    if node.children:
        body += bytes(13)
    record_size = 13 + len(name) + len(props) + len(body)
    end_offset = base_offset + record_size
    header = struct.pack("<IIIB", end_offset, len(node.props), len(props), len(name))
    return header + name + props + body


def binary_fbx(embedded: bytes) -> bytes:
    positions = [0.,0.,0., 1.,0.,0., 1.,1.,0., 0.,1.,0.]
    root = Node("", children=[
        Node("FBXHeaderExtension", children=[Node("FBXHeaderVersion", [("I", 1003)]), Node("FBXVersion", [("I", 7400)])]),
        Node("GlobalSettings", children=[
            Node("Version", [("I", 1000)]),
            Node("Properties70", children=[
                Node("P", [prop_string("UpAxis"), prop_string("int"), prop_string("Integer"), prop_string(""), ("I", 1)]),
                Node("P", [prop_string("UpAxisSign"), prop_string("int"), prop_string("Integer"), prop_string(""), ("I", 1)]),
                Node("P", [prop_string("FrontAxis"), prop_string("int"), prop_string("Integer"), prop_string(""), ("I", 2)]),
                Node("P", [prop_string("FrontAxisSign"), prop_string("int"), prop_string("Integer"), prop_string(""), ("I", 1)]),
                Node("P", [prop_string("CoordAxis"), prop_string("int"), prop_string("Integer"), prop_string(""), ("I", 0)]),
                Node("P", [prop_string("CoordAxisSign"), prop_string("int"), prop_string("Integer"), prop_string(""), ("I", 1)]),
                Node("P", [prop_string("UnitScaleFactor"), prop_string("double"), prop_string("Number"), prop_string(""), prop_float(1)]),
            ]),
        ]),
        Node("Objects", children=[
            Node("Geometry", [prop_int(1001), prop_string("Geometry::Quad"), prop_string("Mesh")], [
                Node("GeometryVersion", [("I", 124)]),
                Node("Vertices", [prop_array("d", positions)]),
                Node("PolygonVertexIndex", [prop_array("i", [0, 1, -3, 0, 2, -4])]),
                Node("LayerElementNormal", [("I", 0)], [
                    Node("Version", [("I", 101)]), Node("Name", [prop_string("")]),
                    Node("MappingInformationType", [prop_string("ByPolygonVertex")]),
                    Node("ReferenceInformationType", [prop_string("Direct")]),
                    Node("Normals", [prop_array("d", [0., 0., 1.] * 6)]),
                ]),
                Node("LayerElementUV", [("I", 0)], [
                    Node("Version", [("I", 101)]), Node("Name", [prop_string("UVChannel_1")]),
                    Node("MappingInformationType", [prop_string("ByPolygonVertex")]),
                    Node("ReferenceInformationType", [prop_string("Direct")]),
                    Node("UV", [prop_array("d", [0.,0., 1.,0., 1.,1., 0.,0., 1.,1., 0.,1.])]),
                ]),
                Node("LayerElementMaterial", [("I", 0)], [
                    Node("Version", [("I", 101)]), Node("Name", [prop_string("")]),
                    Node("MappingInformationType", [prop_string("ByPolygon")]),
                    Node("ReferenceInformationType", [prop_string("IndexToDirect")]),
                    Node("Materials", [prop_array("i", [0, 1])]),
                ]),
                Node("Layer", [("I", 0)], [
                    Node("Version", [("I", 100)]),
                    Node("LayerElement", children=[Node("Type", [prop_string("LayerElementNormal")]), Node("TypedIndex", [("I", 0)])]),
                    Node("LayerElement", children=[Node("Type", [prop_string("LayerElementUV")]), Node("TypedIndex", [("I", 0)])]),
                    Node("LayerElement", children=[Node("Type", [prop_string("LayerElementMaterial")]), Node("TypedIndex", [("I", 0)])]),
                ]),
            ]),
            Node("Model", [prop_int(1002), prop_string("Model::Quad"), prop_string("Mesh")], [
                Node("Version", [("I", 232)]),
                Node("Properties70", children=[
                    Node("P", [prop_string("Lcl Translation"), prop_string("Lcl Translation"), prop_string(""), prop_string("A"), prop_float(3), prop_float(4), prop_float(5)]),
                    Node("P", [prop_string("Lcl Scaling"), prop_string("Lcl Scaling"), prop_string(""), prop_string("A"), prop_float(2), prop_float(1), prop_float(1)]),
                ]),
            ]),
            Node("Model", [prop_int(1003), prop_string("Model::Parent"), prop_string("Null")], [
                Node("Properties70", children=[
                    Node("P", [prop_string("Lcl Translation"), prop_string("Lcl Translation"), prop_string(""), prop_string("A"), prop_float(10), prop_float(0), prop_float(0)]),
                ]),
            ]),
            Node("Material", [prop_int(2001), prop_string("Material::Warm"), prop_string("")], [
                Node("Properties70", children=[
                    Node("P", [prop_string("DiffuseColor"), prop_string("Color"), prop_string(""), prop_string("A"), prop_float(.5), prop_float(.6), prop_float(.7)]),
                    Node("P", [prop_string("DiffuseFactor"), prop_string("double"), prop_string("Number"), prop_string(""), prop_float(.8)]),
                    Node("P", [prop_string("TransparencyFactor"), prop_string("double"), prop_string("Number"), prop_string(""), prop_float(.25)]),
                ]),
            ]),
            Node("Material", [prop_int(2002), prop_string("Material::Cool"), prop_string("")], [
                Node("Properties70", children=[
                    Node("P", [prop_string("DiffuseColor"), prop_string("Color"), prop_string(""), prop_string("A"), prop_float(.2), prop_float(.4), prop_float(.6)]),
                ]),
            ]),
            Node("Texture", [prop_int(3001), prop_string("Texture::Warm"), prop_string("")], [
                Node("Type", [prop_string("TextureVideoClip")]),
                Node("Properties70", children=[Node("P", [prop_string("UVSet"), prop_string("KString"), prop_string(""), prop_string(""), prop_string("UVChannel_1")])]),
                Node("FileName", [prop_string("gkcore_fbx_textures/shared.png")]),
                Node("RelativeFilename", [prop_string("gkcore_fbx_textures/shared.png")]),
                Node("UVSet", [prop_string("UVChannel_1")]),
                Node("ModelUVTranslation", [("D", 0.), ("D", 0.)]),
                Node("ModelUVScaling", [("D", 1.), ("D", 1.)]),
            ]),
            Node("Video", [prop_int(4001), prop_string("Video::Warm"), prop_string("Clip")], [
                Node("Type", [prop_string("Clip")]),
                Node("Filename", [prop_string("gkcore_fbx_textures/shared.png")]),
                Node("RelativeFilename", [prop_string("gkcore_fbx_textures/shared.png")]),
                Node("Content", [prop_raw(embedded)]),
            ]),
        ]),
        Node("Connections", children=[
            Node("C", [prop_string("OO"), prop_int(1001), prop_int(1002)]),
            Node("C", [prop_string("OO"), prop_int(1002), prop_int(1003)]),
            Node("C", [prop_string("OO"), prop_int(2001), prop_int(1002)]),
            Node("C", [prop_string("OO"), prop_int(2002), prop_int(1002)]),
            Node("C", [prop_string("OP"), prop_int(3001), prop_int(2001), prop_string("DiffuseColor")]),
            Node("C", [prop_string("OO"), prop_int(4001), prop_int(3001)]),
        ]),
    ])
    # A 7400 node record uses absolute 32-bit end offsets from the file beginning.
    body_offset = 27
    body_parts = []
    offset = body_offset
    for child in root.children:
        child_bytes = binary_node(child, offset)
        body_parts.append(child_bytes)
        offset += len(child_bytes)
    body = b"".join(body_parts)
    body += bytes(13)
    return b"Kaydara FBX Binary  \x00\x1a\x00" + struct.pack("<I", 7400) + body + bytes(16)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    external_dir = OUT / "gkcore_ascii_textures"
    external_dir.mkdir(exist_ok=True)
    (external_dir / "diffuse_日本.png").write_bytes(PNG)
    (OUT / "gkcore_ascii.fbx").write_bytes(ascii_fbx("gkcore_ascii_textures/diffuse_日本.png"))
    (OUT / "gkcore_binary.fbx").write_bytes(binary_fbx(PNG))
    (OUT / "gkcore_ascii_upper.FBX").write_bytes(ascii_fbx("gkcore_ascii_textures/diffuse_日本.png"))
    (OUT / "gkcore_ascii_magic.bin").write_bytes(ascii_fbx("gkcore_ascii_textures/diffuse_日本.png"))
    (OUT / "gkcore_binary_magic.bin").write_bytes(binary_fbx(PNG))
    (OUT / "gkcore_obj_renamed.fbx").write_text(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii")
    ascii_fixture = ascii_fbx("gkcore_ascii_textures/diffuse_日本.png")
    (OUT / "gkcore_uv_transform.fbx").write_bytes(
        ascii_fixture.replace(
            b'P: "UVSet", "KString", "", "", "UVChannel_1"',
            b'P: "UVSet", "KString", "", "", "UVChannel_1" P: "Translation", "Vector3D", "Vector", "", 0.25, 0, 0', 1))
    (OUT / "gkcore_legacy_uv_transform.fbx").write_bytes(
        ascii_fixture.replace(b"ModelUVScaling: 1,1", b"ModelUVScaling: 2,1", 1))
    (OUT / "gkcore_uv_set_mismatch.fbx").write_bytes(
        ascii_fixture.replace(
            b'P: "UVSet", "KString", "", "", "UVChannel_1"',
            b'P: "UVSet", "KString", "", "", "UVChannel_Missing"', 1))
    layered = ascii_fixture.replace(
        b'Texture: 3001, "Texture::Warm", ""',
        b'LayeredTexture: 3001, "LayeredTexture::Warm", ""', 1)
    (OUT / "gkcore_layered_texture.fbx").write_bytes(layered)
    (OUT / "gkcore_malformed_ascii.fbx").write_text(
        '; FBX 7.4.0 project file\nFBXHeaderExtension: { FBXVersion: 7400 }\nObjects: { Geometry: { Vertices: *3 { a: 0, 0, }',
        encoding="utf-8")
    binary = binary_fbx(PNG)
    (OUT / "gkcore_truncated_binary.fbx").write_bytes(binary[:-37])
    (OUT / "gkcore_nonfinite_ascii.fbx").write_bytes(
        ascii_fbx("gkcore_ascii_textures/diffuse_日本.png").replace(b"1, 0, 0", b"nan, 0, 0", 1))
    missing_texture = ascii_fbx("gkcore_ascii_textures/not_found.png")
    (OUT / "gkcore_missing_texture.fbx").write_bytes(missing_texture)
    skin = ascii_fbx("gkcore_ascii_textures/diffuse_日本.png")
    skin = skin.replace(
        b'    Material: 2001,',
        b'    Deformer: 5001, "Deformer::Skin", "Skin" { Type: "Skin" }\n    Material: 2001,', 1)
    skin = skin.replace(b'    C: "OO", 1001, 1002',
                        b'    C: "OO", 1001, 1002\n    C: "OO", 5001, 1001', 1)
    (OUT / "gkcore_skin_ascii.fbx").write_bytes(skin)


if __name__ == "__main__":
    main()
