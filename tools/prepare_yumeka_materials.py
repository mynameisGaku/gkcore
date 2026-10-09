#!/usr/bin/env python3
"""YUMEKAの材質画像をローカル表示用に縮小し、alpha maskを合成する。"""

import argparse
import binascii
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

import numpy as np


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_PACKAGE_IMAGE_BYTES = 64 * 1024 * 1024


def hash_file(path):
    """入力・出力画像のSHA-256を返す。"""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def decode_rgba(path):
    """filter type 1で保存された8-bit RGBA PNGを復元する。"""
    data = path.read_bytes()
    if len(data) > MAX_PACKAGE_IMAGE_BYTES or not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"PNGが無効か読み込み上限を超えています: {path}")
    width = height = bit_depth = color_type = interlace = None
    compressed = bytearray()
    offset = len(PNG_SIGNATURE)
    while offset + 12 <= len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4 : offset + 8]
        payload_start = offset + 8
        payload_end = payload_start + size
        if payload_end + 4 > len(data):
            raise ValueError(f"PNG chunkが途中で終わっています: {path}")
        payload = data[payload_start:payload_end]
        crc = struct.unpack_from(">I", data, payload_end)[0]
        if (binascii.crc32(kind + payload) & 0xFFFFFFFF) != crc:
            raise ValueError(f"PNG chunkのCRCが一致しません: {path}")
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(">IIBBBBB", payload)
            if compression != 0 or filter_method != 0:
                raise ValueError(f"未対応のPNG圧縮形式です: {path}")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        offset = payload_end + 4
    if bit_depth != 8 or color_type != 6 or interlace != 0 or width <= 0 or height <= 0:
        raise ValueError(f"8-bit RGBA PNG以外は準備できません: {path}")
    row_size = width * 4
    raw = zlib.decompress(compressed)
    if len(raw) != height * (row_size + 1):
        raise ValueError(f"PNG画素byte数がIHDRと一致しません: {path}")
    rows = np.frombuffer(raw, dtype=np.uint8).reshape(height, row_size + 1)
    if not np.all(rows[:, 0] == 1):
        raise ValueError(f"この素材は想定したSub filterではありません: {path}")
    encoded = rows[:, 1:].reshape(height, width, 4).astype(np.uint32)
    pixels = np.bitwise_and(np.cumsum(encoded, axis=1, dtype=np.uint32), 255).astype(np.uint8)
    return pixels


def downsample_2x(image):
    """2x2画素の平均で4096角画像を2048角へ縮小する。"""
    height, width, channels = image.shape
    if width != height or width != 4096 or channels != 4:
        raise ValueError("YUMEKAの準備対象は4096x4096 RGBA PNGである必要があります")
    blocks = image.reshape(height // 2, 2, width // 2, 2, 4).astype(np.uint16)
    summed = blocks.sum(axis=(1, 3), dtype=np.uint16)
    return ((summed + 2) // 4).astype(np.uint8)


def chunk(kind, payload):
    """PNG chunkをCRC付きで組み立てる。"""
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)


def encode_rgba(path, image):
    """filter 0を使ってRGBA配列をPNGへ保存する。"""
    height, width, channels = image.shape
    if channels != 4:
        raise ValueError("RGBA画像ではありません")
    rows = np.concatenate((np.zeros((height, 1), dtype=np.uint8), image.reshape(height, width * 4)), axis=1)
    raw = rows.tobytes()
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    compressed = zlib.compress(raw, level=6)
    payload = PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def require_identity_transform(texture, role, material_name):
    """baseとmaskのUV変換が同じ無変換であることを確認する。"""
    if not texture:
        return
    scale = texture.get("scale", [1.0, 1.0])
    offset = texture.get("offset", [0.0, 0.0])
    if scale != [1.0, 1.0] or offset != [0.0, 0.0]:
        raise ValueError(f"{material_name}の{role} UV変換は画像合成で未対応です: scale={scale}, offset={offset}")


def prepare_material(material, output_root, output_name, max_side):
    """画像を準備し、1材質分の設定と生成情報を返す。"""
    name = material["materialName"]
    base = material.get("baseColorTexture")
    if not base or not base.get("stagedPath") or not base.get("stagedFileExists"):
        raise ValueError(f"基本色画像を解決できません: {name}")
    require_identity_transform(base, "基本色画像", name)
    mask = material.get("alphaMaskTexture")
    props = material["rawRenderProperties"]
    mask_mode = int(props.get("_AlphaMaskMode", 0))
    mask_scale = float(props.get("_AlphaMaskScale", 1.0))
    if not math.isfinite(mask_scale) or mask_scale < 0.0:
        raise ValueError(f"{name}の_alphaMaskScaleが無効です")
    if mask and mask.get("stagedPath"):
        require_identity_transform(mask, "alpha mask", name)
        if mask_mode == 0:
            raise ValueError(f"{name}はalpha mask画像を持ちますがAlphaMaskModeが0です")
        mode = 1
    else:
        mode = 2 if int(props.get("_DstBlend", 0)) == 10 else 0
        mask = None
        mask_scale = 1.0
    cutoff_raw = float(props.get("_Cutoff", 0.5))
    if not math.isfinite(cutoff_raw):
        raise ValueError(f"{name}のcutoffが有限値ではありません")
    cutoff = max(0.0, cutoff_raw)
    base_path = Path(base["stagedPath"])
    pixels = downsample_2x(decode_rgba(base_path))
    alpha_stats = None
    mask_path = None
    if mask:
        mask_path = Path(mask["stagedPath"])
        mask_pixels = downsample_2x(decode_rgba(mask_path))
        alpha = pixels[:, :, 3].astype(np.float64)
        mask_red = mask_pixels[:, :, 0].astype(np.float64)
        alpha = np.clip(np.rint(alpha * mask_red * mask_scale / 255.0), 0, 255).astype(np.uint8)
        pixels[:, :, 3] = alpha
        alpha_stats = {
            "zeroPixels": int(np.count_nonzero(alpha == 0)),
            "partialPixels": int(np.count_nonzero((alpha > 0) & (alpha < 255))),
            "opaquePixels": int(np.count_nonzero(alpha == 255)),
        }
    output_path = (output_root / output_name).resolve()
    encode_rgba(output_path, pixels)
    color = material.get("baseColor") or [1.0, 1.0, 1.0, 1.0]
    if len(color) != 4 or any(not math.isfinite(float(value)) or float(value) < 0.0 or float(value) > 1.0 for value in color):
        raise ValueError(f"{name}の基本色係数が[0,1]外または有限値ではありません")
    return {
        "materialIndex": material["gkcoreModelMaterialIndex"],
        "materialName": name,
        "baseColorFactor": [float(value) for value in color],
        "alphaMode": mode,
        "alphaCutoff": cutoff,
        "sourceAlphaCutoff": cutoff_raw,
        "alphaMaskScale": mask_scale,
        "sourceBaseColorTexture": str(base_path),
        "sourceBaseColorSha256": hash_file(base_path),
        "sourceAlphaMaskTexture": str(mask_path) if mask_path else None,
        "sourceAlphaMaskSha256": hash_file(mask_path) if mask_path else None,
        "outputTexture": str(output_path),
        "outputSha256": hash_file(output_path),
        "outputWidth": max_side,
        "outputHeight": max_side,
        "alphaPixelCounts": alpha_stats,
    }


def main():
    """7材質のlocal PNGとviewer用設定を作成する。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--material-map", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--max-side", type=int, default=2048)
    args = parser.parse_args()
    if args.max_side != 2048:
        raise ValueError("この版は4K素材を2048x2048へ準備します")
    map_path = args.material_map.resolve()
    material_map = json.loads(map_path.read_text(encoding="utf-8-sig"))
    materials = material_map["materials"]
    core_names = material_map["gkcoreModelMaterialOrder"]
    by_name = {item["materialName"]: item for item in materials}
    if len(core_names) != 7 or set(core_names) != set(by_name):
        raise ValueError("model material mapping must contain exactly seven unique entries")
    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    output_files = {
        "Yumeka_Face_Transparent": "mat0.png",
        "Yumeka_Face": "mat1.png",
        "Yumeka_Body": "mat2.png",
        "Yumeka_Clothes": "mat3.png",
        "Yumeka_Hair": "mat4.png",
        "Yumeka_Hair_Transparent": "mat5.png",
        "Yumeka_Wing_Transparent": "mat6.png",
    }
    prepared_by_name = {}
    for name in core_names:
        prepared_by_name[name] = prepare_material(by_name[name], output_root, output_files[name], args.max_side)
    ordered = [prepared_by_name[name] for name in core_names]
    lines = []
    for item in ordered:
        factor = " ".join(f"{value:.8g}" for value in item["baseColorFactor"])
        image_path = item["outputTexture"]
        lines.append(f"{item['materialIndex']} {factor} {item['alphaMode']} {item['alphaCutoff']:.8g} {image_path}")
    config_path = output_root / "material-config.txt"
    config_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    manifest = {
        "sourceMaterialMap": str(map_path),
        "sourceUnitypackageSha256": material_map["source"]["unitypackageSha256"],
        "sourceFbxSha256": material_map["source"]["fbxSha256"],
        "preparation": "4K RGBA inputs downsampled by 2x2 channel average to 2048; alphaMask R multiplied by base alpha; non-identity UV transforms are rejected; RGB is otherwise unchanged apart from downsampling.",
        "viewerConfig": str(config_path.resolve()),
        "uniquePreparedTextureBytes": sum(Path(item["outputTexture"]).stat().st_size for item in {item["outputTexture"]: item for item in ordered}.values()),
        "decodedRgbaBytes": len(ordered) * args.max_side * args.max_side * 4,
        "materials": ordered,
    }
    manifest_path = output_root / "prepared-materials.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote 7 mappings and 7 prepared textures to {output_root}")


if __name__ == "__main__":
    main()
