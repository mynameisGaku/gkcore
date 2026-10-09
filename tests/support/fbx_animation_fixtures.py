#!/usr/bin/env python3
"""FBX clip sampling and deformation tests向けのASCII fixtureを作る。"""

import argparse
from pathlib import Path


def add_translation_clip(text, target_id, clip_name, curve_name, first_id, rest_x):
    """指定nodeのX/Y/Zを明示した移動clipをASCII FBXへ追加する。"""
    objects_end = text.index("\n}\nConnections: {")
    objects = '''
    AnimationStack: @STACK_ID@, "AnimStack::@CLIP_NAME@", "" {
        Properties70: {
            P: "LocalStart", "KTime", "Time", "", 0
            P: "LocalStop", "KTime", "Time", "", 46186158000
        }
    }
    AnimationLayer: @LAYER_ID@, "AnimLayer::@CLIP_NAME@Layer", "" {
        Version: 100
    }
    AnimationCurveNode: @NODE_ID@, "AnimCurveNode::@CURVE_NAME@", "" {
        Properties70: {
            P: "d|X", "Number", "", "A", @REST_X@
            P: "d|Y", "Number", "", "A", 0
            P: "d|Z", "Number", "", "A", 0
        }
    }
    AnimationCurve: @X_CURVE_ID@, "AnimCurve::@CURVE_NAME@X", "" {
        Default: @REST_X@
        KeyVer: 4008
        KeyTime: *2 { a: 0, 46186158000 }
        KeyValueFloat: *2 { a: @REST_X@, @REST_X@ }
        KeyAttrFlags: *2 { a: 4, 4 }
        KeyAttrDataFloat: *8 { a: 0, 0, 0, 0, 0, 0, 0, 0 }
        KeyAttrRefCount: *2 { a: 1, 1 }
    }
    AnimationCurve: @Y_CURVE_ID@, "AnimCurve::@CURVE_NAME@Y", "" {
        Default: 0
        KeyVer: 4008
        KeyTime: *2 { a: 0, 46186158000 }
        KeyValueFloat: *2 { a: 0, 4 }
        KeyAttrFlags: *2 { a: 4, 4 }
        KeyAttrDataFloat: *8 { a: 0, 0, 0, 0, 0, 0, 0, 0 }
        KeyAttrRefCount: *2 { a: 1, 1 }
    }
    AnimationCurve: @Z_CURVE_ID@, "AnimCurve::@CURVE_NAME@Z", "" {
        Default: 0
        KeyVer: 4008
        KeyTime: *2 { a: 0, 46186158000 }
        KeyValueFloat: *2 { a: 0, 0 }
        KeyAttrFlags: *2 { a: 4, 4 }
        KeyAttrDataFloat: *8 { a: 0, 0, 0, 0, 0, 0, 0, 0 }
        KeyAttrRefCount: *2 { a: 1, 1 }
    }
'''
    for token, value in {
        "@STACK_ID@": str(first_id),
        "@LAYER_ID@": str(first_id + 1),
        "@NODE_ID@": str(first_id + 2),
        "@CURVE_ID@": str(first_id + 3),
        "@X_CURVE_ID@": str(first_id + 3),
        "@Y_CURVE_ID@": str(first_id + 4),
        "@Z_CURVE_ID@": str(first_id + 5),
        "@CLIP_NAME@": clip_name,
        "@CURVE_NAME@": curve_name,
        "@REST_X@": str(rest_x),
    }.items():
        objects = objects.replace(token, value)
    text = text[:objects_end] + objects + text[objects_end:]
    connections_end = text.rfind("\n}")
    connections = '''
    C: "OO", @LAYER_ID@, @STACK_ID@
    C: "OO", @NODE_ID@, @LAYER_ID@
    C: "OP", @NODE_ID@, @TARGET_ID@, "Lcl Translation"
    C: "OP", @X_CURVE_ID@, @NODE_ID@, "d|X"
    C: "OP", @Y_CURVE_ID@, @NODE_ID@, "d|Y"
    C: "OP", @Z_CURVE_ID@, @NODE_ID@, "d|Z"
'''
    for token, value in {
        "@STACK_ID@": str(first_id),
        "@LAYER_ID@": str(first_id + 1),
        "@NODE_ID@": str(first_id + 2),
        "@CURVE_ID@": str(first_id + 3),
        "@X_CURVE_ID@": str(first_id + 3),
        "@Y_CURVE_ID@": str(first_id + 4),
        "@Z_CURVE_ID@": str(first_id + 5),
        "@TARGET_ID@": str(target_id),
    }.items():
        connections = connections.replace(token, value)
    text = text[:connections_end] + connections + text[connections_end:]
    return text


def remove_external_material_images(text):
    """fixtureから外部画像接続を外し、geometryとmaterial slotを保つ。"""
    # 外部画像を使わず、既存のmaterial slotとprimitive順序は保つ。
    text = text.replace('    C: "OP", 3001, 2001, "DiffuseColor"\n', "")
    text = text.replace('    C: "OP", 3002, 2002, "DiffuseColor"\n', "")
    return text


def make_node_translation(source, destination):
    """静的FBXのParent nodeへ1秒間のY移動clipを追加する。"""
    text = source.read_text(encoding="utf-8-sig")
    text = add_translation_clip(text, 1003, "ParentMove", "ParentTranslation", 9001, 10)
    text = remove_external_material_images(text)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8")


def make_skin_translation(source, destination, empty_cluster=False, empty_only=False):
    """bone移動clipへ有効または空のskin clusterを組み合わせる。"""
    text = source.read_text(encoding="utf-8-sig")
    text = text.replace('P: "Lcl Translation", "Lcl Translation", "", "A", 3, 4, 5',
                        'P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0')
    text = text.replace('P: "Lcl Scaling", "Lcl Scaling", "", "A", 2, 1, 1',
                        'P: "Lcl Scaling", "Lcl Scaling", "", "A", 1, 1, 1')
    text = text.replace('P: "Lcl Translation", "Lcl Translation", "", "A", 10, 0, 0',
                        'P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0')
    objects_end = text.index("\n}\nConnections: {")
    skin_objects = '''
    Model: 1004, "Model::SkinBone", "LimbNode" {
        Version: 232
        Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0 }
    }
    Deformer: 5001, "Deformer::QuadSkin", "Skin" {
        Version: 101
        Link_DeformAcuracy: 50
    }
    Deformer: 5002, "SubDeformer::QuadBone", "Cluster" {
        Version: 100
        UserData: "", ""
        Indexes: *4 { a: 0, 1, 2, 3 }
        Weights: *4 { a: 1, 1, 1, 1 }
        Transform: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
        TransformLink: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
    }
'''
    if empty_only:
        skin_objects = skin_objects.replace("        Indexes: *4 { a: 0, 1, 2, 3 }\n", "")
        skin_objects = skin_objects.replace("        Weights: *4 { a: 1, 1, 1, 1 }\n", "")
    elif empty_cluster:
        skin_objects += '''
    Deformer: 5003, "SubDeformer::UnusedBone", "Cluster" {
        Version: 100
        UserData: "", ""
        Transform: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
        TransformLink: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
    }
'''
    text = text[:objects_end] + skin_objects + text[objects_end:]
    connections_end = text.rfind("\n}")
    skin_connections = '''
    C: "OO", 5001, 1001
    C: "OO", 5002, 5001
    C: "OO", 1004, 5002
    C: "OO", 1004, 1003
'''
    if empty_cluster:
        skin_connections += '''
    C: "OO", 5003, 5001
    C: "OO", 1004, 5003
'''
    text = text[:connections_end] + skin_connections + text[connections_end:]
    text = add_translation_clip(text, 1004, "SkinBoneMove", "SkinBoneTranslation", 9101, 0)
    text = remove_external_material_images(text)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8")


def make_skin_transformed(source, destination):
    """親とgeometry変換、複数clusterの線形skinを組み合わせる。"""
    make_skin_translation(source, destination)
    text = destination.read_text(encoding="utf-8")
    parent_start = text.index('Model: 1003, "Model::Parent"')
    parent_end = text.index("\n    }", parent_start)
    parent = text[parent_start:parent_end]
    parent = parent.replace(
        'Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0 }',
        '''Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A", 0.5, -0.25, 1
            P: "Lcl Rotation", "Lcl Rotation", "", "A", 0, 0, 35
            P: "Lcl Scaling", "Lcl Scaling", "", "A", 2, 0.75, 1.5
        }''')
    text = text[:parent_start] + parent + text[parent_end:]

    quad_start = text.index('Model: 1002, "Model::Quad"')
    quad_end = text.index("\n    }", quad_start)
    quad = text[quad_start:quad_end]
    quad = quad.replace(
        'P: "Lcl Scaling", "Lcl Scaling", "", "A", 1, 1, 1',
        '''P: "Lcl Scaling", "Lcl Scaling", "", "A", 1, 1, 1
            P: "GeometricTranslation", "Vector3D", "Vector", "A", 0.25, -0.1, 0
            P: "GeometricRotation", "Vector3D", "Vector", "A", 0, 0, 15
            P: "GeometricScaling", "Vector3D", "Vector", "A", 1, 1, 1''')
    text = text[:quad_start] + quad + text[quad_end:]

    first_weights = 'Weights: *4 { a: 1, 1, 1, 1 }'
    first_transform = 'Transform: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }'
    text = text.replace(first_weights, 'Weights: *4 { a: 0.25, 0.5, 0.75, 0.4 }', 1)
    text = text.replace(first_transform,
                        'Transform: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.2, -0.1, 0.05, 1 }', 1)

    objects_end = text.index('\n    AnimationStack: 9101')
    extra_objects = '''
    Model: 1005, "Model::SkinBoneSecond", "LimbNode" {
        Version: 232
        Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A", -0.5, 0.25, 0 }
    }
    Deformer: 5003, "SubDeformer::QuadBoneSecond", "Cluster" {
        Version: 100
        UserData: "", ""
        Indexes: *4 { a: 0, 1, 2, 3 }
        Weights: *4 { a: 0.75, 0.5, 0.25, 0.6 }
        Transform: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -0.15, 0.08, 0, 1 }
        TransformLink: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -0.5, 0.25, 1, 1 }
    }
'''
    text = text[:objects_end] + extra_objects + text[objects_end:]
    connections_end = text.rfind("\n}")
    text = text[:connections_end] + '''
    C: "OO", 5003, 5001
    C: "OO", 1005, 5003
    C: "OO", 1005, 1003
''' + text[connections_end:]
    destination.write_text(text, encoding="utf-8")


def make_skin_zero_weight(source, destination):
    """非identity変換とanimationを保ち、最後の頂点だけskinから外す。"""
    make_skin_transformed(source, destination)
    text = destination.read_text(encoding="utf-8")
    text = text.replace("Indexes: *4 { a: 0, 1, 2, 3 }", "Indexes: *3 { a: 0, 1, 2 }")
    text = text.replace("Weights: *4 { a: 0.25, 0.5, 0.75, 0.4 }", "Weights: *3 { a: 0.25, 0.5, 0.75 }")
    text = text.replace("Weights: *4 { a: 0.75, 0.5, 0.25, 0.6 }", "Weights: *3 { a: 0.75, 0.5, 0.25 }")
    destination.write_text(text, encoding="utf-8")


def make_gpu_degenerate_triangle(source, destination):
    """二本の正常な骨を使い、中点で三角形の二頂点を重ねる。"""
    text = source.read_text(encoding="utf-8-sig")
    def replace_once(original, replacement):
        nonlocal text
        if text.count(original) != 1:
            raise ValueError(f"GPU skinning fixture source did not contain one expected value: {original}")
        text = text.replace(original, replacement, 1)

    def replace_curve_key(curve_id, original, replacement):
        nonlocal text
        marker = f"AnimationCurve: {curve_id},"
        start = text.index(marker)
        end = text.find("\n    AnimationCurve:", start + len(marker))
        if end < 0:
            end = text.index("\n}", start)
        curve = text[start:end]
        if curve.count(original) != 1:
            raise ValueError(f"GPU skinning fixture curve {curve_id} did not contain one expected key")
        text = text[:start] + curve.replace(original, replacement, 1) + text[end:]

    replace_once(
        "Vertices: *12 { a: 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 }",
        "Vertices: *9 { a: 0, 0, 0, 1, 0, 0, 0, 1, 0 }")
    replace_once(
        "PolygonVertexIndex: *6 { a: 0, 1, -3, 0, 2, -4 }",
        "PolygonVertexIndex: *3 { a: 0, 1, -3 }")
    replace_once(
        "Normals: *18 { a: 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 }",
        "Normals: *9 { a: 0, 0, 1, 0, 0, 1, 0, 0, 1 }")
    replace_once(
        "UV: *12 { a: 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 }",
        "UV: *6 { a: 0, 0, 1, 0, 0, 1 }")
    replace_once("Materials: *2 { a: 0, 1 }", "Materials: *1 { a: 0 }")
    replace_once('P: "Lcl Translation", "Lcl Translation", "", "A", 3, 4, 5',
                 'P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0')
    replace_once('P: "Lcl Scaling", "Lcl Scaling", "", "A", 2, 1, 1',
                 'P: "Lcl Scaling", "Lcl Scaling", "", "A", 1, 1, 1')
    replace_once('P: "Lcl Translation", "Lcl Translation", "", "A", 10, 0, 0',
                 'P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0')
    objects_end = text.index("\n}\nConnections: {")
    objects = '''
    Deformer: 5001, "Deformer::TriangleSkin", "Skin" {
        Version: 101
        Link_DeformAcuracy: 50
    }
    Model: 1004, "Model::CollapseBone", "LimbNode" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0
            P: "Lcl Scaling", "Lcl Scaling", "", "A", 1, 1, 1
        }
    }
    Model: 1005, "Model::FixedBone", "LimbNode" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0
            P: "Lcl Scaling", "Lcl Scaling", "", "A", 1, 1, 1
        }
    }
    Deformer: 5002, "SubDeformer::CollapseCluster", "Cluster" {
        Version: 100
        UserData: "", ""
        Indexes: *1 { a: 1 }
        Weights: *1 { a: 1 }
        Transform: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
        TransformLink: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
    }
    Deformer: 5003, "SubDeformer::FixedCluster", "Cluster" {
        Version: 100
        UserData: "", ""
        Indexes: *2 { a: 0, 2 }
        Weights: *2 { a: 1, 1 }
        Transform: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
        TransformLink: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
    }
'''
    text = text[:objects_end] + objects + text[objects_end:]
    connections_end = text.rfind("\n}")
    text = text[:connections_end] + '''
    C: "OO", 5001, 1001
    C: "OO", 5002, 5001
    C: "OO", 1004, 5002
    C: "OO", 1004, 1003
    C: "OO", 5003, 5001
    C: "OO", 1005, 5003
    C: "OO", 1005, 1003
''' + text[connections_end:]
    text = add_translation_clip(text, 1004, "CollapseAtHalf", "CollapseTranslation", 9301, 0)
    # 1秒後に-2移動するため、clip時刻0.5秒で頂点1が頂点0へ重なる。
    replace_curve_key(9304, "KeyValueFloat: *2 { a: 0, 0 }", "KeyValueFloat: *2 { a: 0, -2 }")
    replace_curve_key(9305, "KeyValueFloat: *2 { a: 0, 4 }", "KeyValueFloat: *2 { a: 0, 0 }")
    text = remove_external_material_images(text)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8")


def make_gpu_overflow_triangle(source, destination, coordinate="1e22"):
    """有限な大座標とbone scaleの積でfloat範囲を超える三角形を作る。"""
    text = source.read_text(encoding="utf-8-sig")

    def replace_once(original, replacement):
        nonlocal text
        if text.count(original) != 1:
            raise ValueError(f"GPU overflow fixture source did not contain one expected value: {original}")
        text = text.replace(original, replacement, 1)

    replace_once(
        "Vertices: *12 { a: 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 }",
        f"Vertices: *9 {{ a: 0, 0, 0, {coordinate}, 0, 0, 0, {coordinate}, 0 }}")
    replace_once(
        "PolygonVertexIndex: *6 { a: 0, 1, -3, 0, 2, -4 }",
        "PolygonVertexIndex: *3 { a: 0, 1, -3 }")
    replace_once(
        "Normals: *18 { a: 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 }",
        "Normals: *9 { a: 0, 0, 1, 0, 0, 1, 0, 0, 1 }")
    replace_once(
        "UV: *12 { a: 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 }",
        "UV: *6 { a: 0, 0, 1, 0, 0, 1 }")
    replace_once("Materials: *2 { a: 0, 1 }", "Materials: *1 { a: 0 }")
    replace_once('P: "Lcl Translation", "Lcl Translation", "", "A", 3, 4, 5',
                 'P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0')
    replace_once('P: "Lcl Scaling", "Lcl Scaling", "", "A", 2, 1, 1',
                 'P: "Lcl Scaling", "Lcl Scaling", "", "A", 1, 1, 1')
    replace_once('P: "Lcl Translation", "Lcl Translation", "", "A", 10, 0, 0',
                 'P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0')
    objects_end = text.index("\n}\nConnections: {")
    objects = '''
    Deformer: 5101, "Deformer::OverflowSkin", "Skin" {
        Version: 101
        Link_DeformAcuracy: 50
    }
    Model: 1104, "Model::OverflowBone", "LimbNode" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A", 0, 0, 0
            P: "Lcl Scaling", "Lcl Scaling", "", "A", 1, 1, 1
        }
    }
    Deformer: 5102, "SubDeformer::OverflowCluster", "Cluster" {
        Version: 100
        UserData: "", ""
        Indexes: *3 { a: 0, 1, 2 }
        Weights: *3 { a: 1, 1, 1 }
        Transform: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
        TransformLink: *16 { a: 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
    }
    AnimationStack: 9401, "AnimStack::OverflowAtHalf", "" {
        Properties70: {
            P: "LocalStart", "KTime", "Time", "", 0
            P: "LocalStop", "KTime", "Time", "", 46186158000
        }
    }
    AnimationLayer: 9402, "AnimLayer::OverflowAtHalfLayer", "" {
        Version: 100
    }
    AnimationCurveNode: 9403, "AnimCurveNode::OverflowScale", "" {
        Properties70: {
            P: "d|X", "Number", "", "A", 1
            P: "d|Y", "Number", "", "A", 1
            P: "d|Z", "Number", "", "A", 1
        }
    }
    AnimationCurve: 9404, "AnimCurve::OverflowScaleX", "" {
        Default: 1
        KeyVer: 4008
        KeyTime: *2 { a: 0, 46186158000 }
        KeyValueFloat: *2 { a: 1, 1e20 }
        KeyAttrFlags: *2 { a: 4, 4 }
        KeyAttrDataFloat: *8 { a: 0, 0, 0, 0, 0, 0, 0, 0 }
        KeyAttrRefCount: *2 { a: 1, 1 }
    }
    AnimationCurve: 9405, "AnimCurve::OverflowScaleY", "" {
        Default: 1
        KeyVer: 4008
        KeyTime: *2 { a: 0, 46186158000 }
        KeyValueFloat: *2 { a: 1, 1e20 }
        KeyAttrFlags: *2 { a: 4, 4 }
        KeyAttrDataFloat: *8 { a: 0, 0, 0, 0, 0, 0, 0, 0 }
        KeyAttrRefCount: *2 { a: 1, 1 }
    }
    AnimationCurve: 9406, "AnimCurve::OverflowScaleZ", "" {
        Default: 1
        KeyVer: 4008
        KeyTime: *2 { a: 0, 46186158000 }
        KeyValueFloat: *2 { a: 1, 1e20 }
        KeyAttrFlags: *2 { a: 4, 4 }
        KeyAttrDataFloat: *8 { a: 0, 0, 0, 0, 0, 0, 0, 0 }
        KeyAttrRefCount: *2 { a: 1, 1 }
    }
'''
    text = text[:objects_end] + objects + text[objects_end:]
    connections_end = text.rfind("\n}")
    text = text[:connections_end] + '''
    C: "OO", 5101, 1001
    C: "OO", 5102, 5101
    C: "OO", 1104, 5102
    C: "OO", 1104, 1003
    C: "OO", 9402, 9401
    C: "OO", 9403, 9402
    C: "OP", 9403, 1104, "Lcl Scaling"
    C: "OP", 9404, 9403, "d|X"
    C: "OP", 9405, 9403, "d|Y"
    C: "OP", 9406, 9403, "d|Z"
''' + text[connections_end:]
    text = remove_external_material_images(text)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8")


def make_morph_animation(source, destination, rest_weight=0):
    """quad全体をYへ動かすblend shapeとその係数clipを作る。"""
    text = source.read_text(encoding="utf-8-sig")
    objects_end = text.index("\n}\nConnections: {")
    morph_objects = '''
    Geometry: 6001, "Geometry::QuadLift", "Shape" {
        Version: 100
        Indexes: *4 { a: 0, 1, 2, 3 }
        Vertices: *12 { a: 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 }
    }
    Deformer: 6002, "Deformer::QuadBlend", "BlendShape" {
        Version: 100
    }
    Deformer: 6003, "SubDeformer::QuadLift", "BlendShapeChannel" {
        Version: 100
        DeformPercent: 0
        FullWeights: *1 { a: 100 }
    }
    AnimationStack: 9201, "AnimStack::MorphLift", "" {
        Properties70: {
            P: "LocalStart", "KTime", "Time", "", 0
            P: "LocalStop", "KTime", "Time", "", 46186158000
        }
    }
    AnimationLayer: 9202, "AnimLayer::MorphLiftLayer", "" {
        Version: 100
    }
    AnimationCurveNode: 9203, "AnimCurveNode::MorphDeform", "" {
        Properties70: { P: "d|DeformPercent", "Number", "", "A", 0 }
    }
    AnimationCurve: 9204, "AnimCurve::MorphDeformPercent", "" {
        Default: 0
        KeyVer: 4008
        KeyTime: *2 { a: 0, 46186158000 }
        KeyValueFloat: *2 { a: 0, 100 }
        KeyAttrFlags: *2 { a: 4, 4 }
        KeyAttrDataFloat: *8 { a: 0, 0, 0, 0, 0, 0, 0, 0 }
        KeyAttrRefCount: *2 { a: 1, 1 }
    }
'''
    morph_objects = morph_objects.replace("DeformPercent: 0", f"DeformPercent: {rest_weight * 100:g}")
    text = text[:objects_end] + morph_objects + text[objects_end:]
    connections_end = text.rfind("\n}")
    morph_connections = '''
    C: "OO", 6002, 1001
    C: "OO", 6003, 6002
    C: "OO", 6001, 6003
    C: "OO", 9202, 9201
    C: "OO", 9203, 9202
    C: "OP", 9203, 6003, "DeformPercent"
    C: "OP", 9204, 9203, "d|DeformPercent"
'''
    text = text[:connections_end] + morph_connections + text[connections_end:]
    text = remove_external_material_images(text)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8")


def main():
    """アニメーションfixtureを指定した出力先へ作る。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-model", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    make_node_translation(args.source_model,
                          args.output_dir / "fbx-animation-node-translation.fbx")
    make_skin_translation(args.source_model,
                          args.output_dir / "fbx-animation-skin-translation.fbx")
    make_skin_transformed(args.source_model,
                          args.output_dir / "fbx-animation-skin-transformed.fbx")
    make_skin_zero_weight(args.source_model,
                          args.output_dir / "fbx-animation-skin-zero-weight.fbx")
    make_gpu_degenerate_triangle(args.source_model,
                                 args.output_dir / "fbx-animation-gpu-degenerate-triangle.fbx")
    make_gpu_overflow_triangle(args.source_model,
                               args.output_dir / "fbx-animation-gpu-overflow-triangle.fbx")
    make_gpu_overflow_triangle(args.source_model,
                               args.output_dir / "fbx-animation-gpu-large-finite-triangle.fbx",
                               "1e20")
    make_skin_translation(args.source_model,
                          args.output_dir / "fbx-animation-skin-empty-cluster.fbx",
                          empty_cluster=True)
    make_skin_translation(args.source_model,
                          args.output_dir / "fbx-animation-skin-empty-only.fbx",
                          empty_only=True)
    make_morph_animation(args.source_model,
                         args.output_dir / "fbx-animation-morph-weight.fbx")
    make_morph_animation(args.source_model,
                         args.output_dir / "fbx-animation-morph-default-weight.fbx",
                         0.5)


if __name__ == "__main__":
    main()
