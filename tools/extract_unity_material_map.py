#!/usr/bin/env python3
"""Unity packageから材質と画像の参照情報だけをJSONへまとめる。"""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tarfile


MAX_PACKAGE_BYTES = 256 * 1024 * 1024
MAX_MEMBER_BYTES = 4 * 1024 * 1024
GUID_PATTERN = re.compile(r"^guid:\s*([0-9a-fA-F]{32})\s*$", re.MULTILINE)
TEXTURE_PATTERN = re.compile(r"m_Texture:\s*\{fileID:\s*(\d+),\s*guid:\s*([0-9a-fA-F]{32})")
COLOR_PATTERN = re.compile(
    r"^\s*-\s*([^:]+):\s*\{r:\s*([^,]+),\s*g:\s*([^,]+),\s*b:\s*([^,]+),\s*a:\s*([^}]+)\}",
    re.MULTILINE,
)
FLOAT_PATTERN = re.compile(r"^\s*-\s*([^:]+):\s*([^\s]+)\s*$", re.MULTILINE)
VECTOR2_PATTERN = re.compile(r"m_(Scale|Offset):\s*\{x:\s*([^,]+),\s*y:\s*([^}]+)\}")


def read_bounded(archive, member, limit=MAX_MEMBER_BYTES):
    """指定サイズ以内のtarメンバーを読み、超過や欠損を拒否する。"""
    if member.size < 0 or member.size > limit:
        raise ValueError(f"tar member exceeds the read limit: {member.name}")
    stream = archive.extractfile(member)
    if stream is None:
        raise ValueError(f"tar member has no file data: {member.name}")
    data = stream.read(limit + 1)
    if len(data) != member.size:
        raise ValueError(f"tar member size does not match its header: {member.name}")
    return data


def parse_material_text(text, package_path, asset_guid, guid_to_path, stage_root):
    """Material YAMLから画像GUIDと見た目に関係する値を読む。"""
    name_match = re.search(r"^\s*m_Name:\s*(.*?)\s*$", text, re.MULTILINE)
    shader_match = re.search(r"^\s*m_Shader:\s*\{fileID:\s*\d+,\s*guid:\s*([0-9a-fA-F]{32})", text, re.MULTILINE)
    name = name_match.group(1) if name_match else PurePosixPath(package_path).stem
    shader_guid = shader_match.group(1).lower() if shader_match else None
    texture_properties = {}
    section = None
    current_property = None
    for line in text.splitlines():
        marker = re.match(r"^\s*(m_TexEnvs|m_Floats|m_Colors|m_Ints):\s*$", line)
        if marker:
            section = marker.group(1)
            current_property = None
            continue
        if section == "m_Ints" and re.match(r"^\s*[A-Za-z0-9_]+:", line):
            section = None
            continue
        if section != "m_TexEnvs":
            continue
        property_match = re.match(r"^\s*-\s+([^:]+):\s*$", line)
        if property_match:
            current_property = property_match.group(1)
            continue
        texture_match = TEXTURE_PATTERN.search(line)
        if texture_match and current_property:
            file_id = int(texture_match.group(1))
            guid = texture_match.group(2).lower()
            source_path = guid_to_path.get(guid)
            staged_path = None
            prefix = "Assets/Yumeka/Texture/"
            if source_path and source_path.startswith(prefix):
                relative_texture = PurePosixPath(source_path[len(prefix):])
                if not relative_texture.is_absolute() and ".." not in relative_texture.parts:
                    candidate = stage_root / "Texture" / "PNG" / Path(*relative_texture.parts)
                    if candidate.is_file():
                        staged_path = str(candidate.resolve())
            texture_properties[current_property] = {
                "fileId": file_id,
                "guid": guid,
                "unityPath": source_path,
                "stagedPath": staged_path,
                "stagedFileExists": staged_path is not None,
            }
        if current_property in texture_properties:
            vector_match = VECTOR2_PATTERN.search(line)
            if vector_match:
                key = "scale" if vector_match.group(1) == "Scale" else "offset"
                texture_properties[current_property][key] = [float(vector_match.group(2)), float(vector_match.group(3))]
    colors = {}
    for match in COLOR_PATTERN.finditer(text):
        colors[match.group(1)] = [float(match.group(i)) for i in range(2, 6)]
    floats = {}
    for match in FLOAT_PATTERN.finditer(text):
        key, value = match.group(1), match.group(2)
        if key in {
            "_AlphaMaskMode", "_AlphaMaskScale", "_Cutoff", "_DstBlend", "_Mode", "_SrcBlend",
            "_TransparentMode", "_ZTest", "_ZWrite",
        }:
            try:
                floats[key] = float(value)
            except ValueError:
                floats[key] = value
    render_queue_match = re.search(r"^\s*m_CustomRenderQueue:\s*(-?\d+)\s*$", text, re.MULTILINE)
    color = colors.get("_Color", colors.get("_BaseColor"))
    return {
        "materialName": name,
        "unityPath": package_path,
        "unityGuid": asset_guid,
        "shaderGuid": shader_guid,
        "baseColor": color,
        "baseColorTexture": next(
            (texture_properties[prop] for prop in ("_MainTex", "_BaseMap", "_BaseColorMap")
             if texture_properties.get(prop, {}).get("fileId", 0)),
            None,
        ),
        "normalTexture": texture_properties.get("_BumpMap"),
        "alphaMaskTexture": texture_properties.get("_AlphaMask"),
        "textureProperties": texture_properties,
        "rawRenderProperties": {
            **floats,
            "m_CustomRenderQueue": int(render_queue_match.group(1)) if render_queue_match else None,
        },
    }


def hash_file(path):
    """ローカル入力の識別用SHA-256を計算する。"""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    """package内のmaterial情報をローカル検証JSONへ保存する。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--unitypackage", type=Path, required=True)
    parser.add_argument("--stage-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fbx", type=Path, required=True)
    parser.add_argument("--fbx-scene-material-order", nargs="+", required=True)
    parser.add_argument("--gkcore-material-order", nargs="+", required=True)
    args = parser.parse_args()
    package_path = args.unitypackage.resolve()
    stage_root = args.stage_root.resolve()
    fbx_path = args.fbx.resolve()
    with tarfile.open(package_path, "r:*") as archive:
        members = {member.name: member for member in archive.getmembers()}
        assets = {}
        guid_to_path = {}
        for member_name, member in members.items():
            if not member_name.endswith("/pathname"):
                continue
            key = member_name[:-len("/pathname")]
            unity_path = read_bounded(archive, member, 8192).decode("utf-8").strip()
            safe_path = PurePosixPath(unity_path)
            if safe_path.is_absolute() or ".." in safe_path.parts:
                raise ValueError(f"unsafe package pathname: {unity_path}")
            meta = members.get(key + "/asset.meta")
            guid = None
            if meta:
                text = read_bounded(archive, meta).decode("utf-8", "replace")
                match = GUID_PATTERN.search(text)
                if match:
                    guid = match.group(1).lower()
                    if guid in guid_to_path:
                        raise ValueError(f"duplicate Unity asset GUID: {guid}")
                    guid_to_path[guid] = unity_path
            assets[unity_path] = {"key": key, "guid": guid, "asset": members.get(key + "/asset")}
        materials = []
        for unity_path, entry in assets.items():
            if not unity_path.lower().endswith(".mat") or entry["asset"] is None:
                continue
            text = read_bounded(archive, entry["asset"]).decode("utf-8", "replace")
            materials.append(parse_material_text(text, unity_path, entry["guid"], guid_to_path, stage_root))
    materials.sort(key=lambda material: material["materialName"])
    names = [material["materialName"] for material in materials]
    if len(names) != len(set(names)):
        raise ValueError("Unity package contains duplicate material names")
    scene_order = args.fbx_scene_material_order
    core_order = args.gkcore_material_order
    if len(scene_order) != len(set(scene_order)) or set(scene_order) != set(names):
        raise ValueError("FBX scene material order does not match package material names")
    if len(core_order) != len(set(core_order)) or set(core_order) != set(names):
        raise ValueError("gkcore material order does not match package material names")
    scene_indices = {name: index for index, name in enumerate(scene_order)}
    core_indices = {name: index for index, name in enumerate(core_order)}
    for material in materials:
        material["fbxSceneMaterialIndex"] = scene_indices[material["materialName"]]
        material["gkcoreModelMaterialIndex"] = core_indices[material["materialName"]]
    result = {
        "source": {
            "unitypackage": package_path.name,
            "unitypackageSha256": hash_file(package_path),
            "fbx": str(fbx_path),
            "fbxSha256": hash_file(fbx_path),
            "mappingNote": "FBX scene order and gkcore first-use order were measured separately with the native ufbx probe for this exact FBX hash.",
        },
        "fbxSceneMaterialOrder": scene_order,
        "gkcoreModelMaterialOrder": core_order,
        "materials": materials,
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(materials)} material mappings to {output}")


if __name__ == "__main__":
    main()
