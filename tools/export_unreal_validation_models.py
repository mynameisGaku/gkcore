#!/usr/bin/env python3
"""Unreal EditorのPython commandletで、ローカル検証用のモデルと画像を出力する。"""

import json
import os
from pathlib import Path

import unreal


def export_asset(asset, path, exporter, options=None):
    """画面や確認ダイアログを開かず、指定assetを一つ書き出す。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(path)
    task.exporter = exporter
    task.options = options
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.write_empty_files = False
    success = unreal.Exporter.run_asset_export_task(task)
    if not success or not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError(f"書き出し失敗: {asset.get_path_name()}: {list(task.errors)}")
    return str(path)


def fbx_options(preview_mesh=False):
    """モデルはLOD0、motionは骨格だけを出力する設定を作る。"""
    options = unreal.FbxExportOption()
    options.ascii = False
    options.level_of_detail = False
    options.collision = False
    options.export_morph_targets = True
    options.export_preview_mesh = preview_mesh
    options.force_front_x_axis = False
    return options


def base_color_score(parameter, texture):
    """基本色のparameter名と画像名だけを候補とし、normalやORMを選ばない。"""
    name = str(parameter).lower().replace("_", "").replace(" ", "")
    texture_name = texture.get_name().lower()
    if any(term in name for term in ("normal", "roughness", "metallic", "occlusion", "emissive")):
        return 0
    if "basecolor" in name or "albedo" in name or "diffuse" in name:
        return 3
    if "basecolor" in texture_name or texture_name.endswith("_a"):
        return 2
    return 0


def export_model(entry, output):
    """骨格つきFBXと、材質slotごとの基本色画像・対応情報を保存する。"""
    asset = unreal.load_asset(entry["asset"])
    if not isinstance(asset, unreal.SkeletalMesh):
        raise RuntimeError(f"SkeletalMeshを読み込めません: {entry['asset']}")
    directory = output / entry["label"]
    # 非parameterの基本色はmanifestの明示対応で補う。
    textures = {}
    explicit_colors = {}
    for slot_index, texture_asset in entry.get("baseColorAssets", {}).items():
        texture = unreal.load_asset(texture_asset)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError(f"基本色画像を読み込めません: {texture_asset}")
        path = export_asset(texture, directory / "textures" / (texture.get_name() + ".png"), unreal.TextureExporterPNG())
        textures[texture.get_path_name()] = path
        explicit_colors[int(slot_index)] = path
        # FBXが作者PCの旧画像pathを参照しないよう、コピー側の読込情報だけを更新する。
        import_data = texture.get_editor_property("asset_import_data")
        if import_data:
            import_data.scripted_add_filename(path, 0, "gkcore local validation")
    model_path = directory / (entry["label"] + ".fbx")
    result = {"label": entry["label"], "asset": entry["asset"], "fbx": str(model_path), "materials": []}
    for index, slot in enumerate(asset.get_editor_property("materials")):
        material = slot.get_editor_property("material_interface")
        record = {"index": index, "slot": str(slot.get_editor_property("material_slot_name")), "material": material.get_path_name() if material else None, "parameters": [], "baseColor": None}
        candidates = []
        if isinstance(material, unreal.MaterialInstanceConstant):
            for parameter in unreal.MaterialEditingLibrary.get_texture_parameter_names(material):
                texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material, parameter)
                if not texture:
                    continue
                record["parameters"].append({"name": str(parameter), "texture": texture.get_path_name()})
                score = base_color_score(parameter, texture)
                if score:
                    candidates.append((score, str(parameter), texture))
        if candidates:
            candidates.sort(key=lambda candidate: (-candidate[0], candidate[1]))
            texture = candidates[0][2]
            key = texture.get_path_name()
            if key not in textures:
                textures[key] = export_asset(texture, directory / "textures" / (texture.get_name() + ".png"), unreal.TextureExporterPNG())
            record["baseColor"] = textures[key]
        if not record["baseColor"] and index in explicit_colors:
            record["baseColor"] = explicit_colors[index]
        result["materials"].append(record)
    export_asset(asset, model_path, unreal.SkeletalMeshExporterFBX(), fbx_options())
    return result


def main():
    """環境変数が指すmanifestだけを読み、個別の成否をJSONへ記録する。"""
    manifest_path = Path(os.environ["GKCORE_UE_EXPORT_MANIFEST"])
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    output = Path(manifest["output"])
    output.mkdir(parents=True, exist_ok=True)
    report = {"engineVersion": unreal.SystemLibrary.get_engine_version(), "models": [], "animations": [], "errors": []}
    for entry in manifest.get("models", []):
        try:
            report["models"].append(export_model(entry, output))
        except Exception as error:
            report["errors"].append({"asset": entry["asset"], "error": str(error)})
            unreal.log_error(str(error))
    for entry in manifest.get("animations", []):
        try:
            asset = unreal.load_asset(entry["asset"])
            if not isinstance(asset, unreal.AnimSequence):
                raise RuntimeError(f"AnimSequenceを読み込めません: {entry['asset']}")
            path = output / "motions" / (entry["label"] + ".fbx")
            export_asset(asset, path, unreal.AnimSequenceExporterFBX(), fbx_options())
            report["animations"].append({"label": entry["label"], "asset": entry["asset"], "fbx": str(path)})
        except Exception as error:
            report["errors"].append({"asset": entry["asset"], "error": str(error)})
            unreal.log_error(str(error))
    (output / "export-report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("GKCORE_UE_EXPORT_COMPLETE: " + json.dumps({"models": len(report["models"]), "animations": len(report["animations"]), "errors": report["errors"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
