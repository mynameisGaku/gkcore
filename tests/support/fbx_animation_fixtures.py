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


def make_skin_translation(source, destination):
    """1つのbone clusterとbone移動clipを持つ簡単なskinを作る。"""
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
    text = text[:objects_end] + skin_objects + text[objects_end:]
    connections_end = text.rfind("\n}")
    skin_connections = '''
    C: "OO", 5001, 1001
    C: "OO", 5002, 5001
    C: "OO", 1004, 5002
    C: "OO", 1004, 1003
'''
    text = text[:connections_end] + skin_connections + text[connections_end:]
    text = add_translation_clip(text, 1004, "SkinBoneMove", "SkinBoneTranslation", 9101, 0)
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
    make_morph_animation(args.source_model,
                         args.output_dir / "fbx-animation-morph-weight.fbx")
    make_morph_animation(args.source_model,
                         args.output_dir / "fbx-animation-morph-default-weight.fbx",
                         0.5)


if __name__ == "__main__":
    main()
