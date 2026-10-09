#!/usr/bin/env python3
"""埋め込みJPEGをPNGへ変え、検証用GLBの他のデータを保つ。"""

import argparse
from io import BytesIO
import json
from pathlib import Path
import struct
import hashlib

from PIL import Image


GLB_MAGIC = b"glTF"
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


def _read_glb(path):
    """GLB v2のchunkを順序を保って読み取る。"""
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError("GLB header is truncated")
    magic, version, total_length = struct.unpack_from("<4sII", data, 0)
    if magic != GLB_MAGIC or version != 2 or total_length != len(data):
        raise ValueError("input is not a complete GLB v2 file")
    chunks = []
    offset = 12
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("GLB chunk header is truncated")
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        end = offset + chunk_length
        if end > len(data):
            raise ValueError("GLB chunk exceeds the file length")
        chunks.append((chunk_type, data[offset:end]))
        offset = end
    json_chunks = [payload for kind, payload in chunks if kind == JSON_CHUNK]
    if len(json_chunks) != 1:
        raise ValueError("GLB must contain exactly one JSON chunk")
    document = json.loads(json_chunks[0].decode("utf-8").rstrip(" \t\r\n\0"))
    binary_chunks = [payload for kind, payload in chunks if kind == BIN_CHUNK]
    if len(binary_chunks) > 1:
        raise ValueError("GLB contains more than one BIN chunk")
    return document, chunks, binary_chunks[0] if binary_chunks else bytearray()


def _extract_view(document, binary, view_index):
    """埋め込み画像bufferViewの範囲を検証して切り出す。"""
    views = document.get("bufferViews", [])
    if not isinstance(view_index, int) or view_index < 0 or view_index >= len(views):
        raise ValueError("image bufferView index is invalid")
    view = views[view_index]
    if view.get("buffer", 0) != 0:
        raise ValueError("image does not refer to the embedded GLB buffer")
    offset = view.get("byteOffset", 0)
    length = view.get("byteLength")
    if not isinstance(offset, int) or not isinstance(length, int) or offset < 0 or length <= 0 or offset > len(binary) or length > len(binary) - offset:
        raise ValueError("image bufferView exceeds the embedded GLB buffer")
    return bytes(binary[offset:offset + length])


def convert_glb(source_path, output_path):
    """embedded JPEG画像だけをPNGへ置き換え、既存BIN範囲は変更しない。"""
    document, chunks, source_binary = _read_glb(source_path)
    buffers = document.get("buffers", [])
    if len(buffers) != 1 or buffers[0].get("uri") is not None:
        raise ValueError("input must use one embedded GLB buffer")
    binary = bytearray(source_binary)
    views = document.setdefault("bufferViews", [])
    converted = []
    for image_index, image_data in enumerate(document.get("images", [])):
        mime_type = image_data.get("mimeType", "").lower()
        if mime_type not in ("image/jpeg", "image/jpg"):
            raise ValueError(f"image {image_index} is not an embedded JPEG")
        if "uri" in image_data or "bufferView" not in image_data:
            raise ValueError(f"image {image_index} is not embedded in a bufferView")
        original_bytes = _extract_view(document, source_binary, image_data["bufferView"])
        with Image.open(BytesIO(original_bytes)) as source_image:
            source_image.load()
            source_rgba = source_image.convert("RGBA")
            width, height = source_rgba.size
            png_buffer = BytesIO()
            source_rgba.save(png_buffer, format="PNG")
            png_bytes = png_buffer.getvalue()
        with Image.open(BytesIO(png_bytes)) as verified_image:
            verified_image.load()
            verified_rgba = verified_image.convert("RGBA")
            if verified_rgba.size != (width, height) or verified_rgba.tobytes() != source_rgba.tobytes():
                raise ValueError(f"image {image_index} changed during PNG conversion")
        while len(binary) % 4:
            binary.append(0)
        png_offset = len(binary)
        binary.extend(png_bytes)
        png_view = len(views)
        views.append({"buffer": 0, "byteOffset": png_offset, "byteLength": len(png_bytes)})
        image_data["bufferView"] = png_view
        image_data["mimeType"] = "image/png"
        converted.append({"index": image_index, "width": width, "height": height, "sourceBytes": len(original_bytes), "pngBytes": len(png_bytes)})

    if not converted:
        raise ValueError("input has no embedded JPEG images to convert")
    buffers[0]["byteLength"] = len(binary)
    new_chunks = []
    for chunk_type, payload in chunks:
        if chunk_type == JSON_CHUNK:
            json_bytes = json.dumps(document, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8")
            json_bytes += b" " * ((-len(json_bytes)) % 4)
            new_chunks.append((chunk_type, json_bytes))
        elif chunk_type == BIN_CHUNK:
            binary_bytes = bytes(binary)
            binary_bytes += b"\0" * ((-len(binary_bytes)) % 4)
            new_chunks.append((chunk_type, binary_bytes))
        else:
            new_chunks.append((chunk_type, payload))
    if not any(chunk_type == BIN_CHUNK for chunk_type, _ in chunks):
        raise ValueError("input is missing the embedded BIN chunk")
    body = bytearray()
    for chunk_type, payload in new_chunks:
        body.extend(struct.pack("<II", len(payload), chunk_type))
        body.extend(payload)
    header = struct.pack("<4sII", GLB_MAGIC, 2, 12 + len(body))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(header + body)
    return converted


def main():
    """GLBを変換し、sourceと出力のSHA-256を表示する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    converted = convert_glb(arguments.input, arguments.output)
    source_hash = hashlib.sha256(arguments.input.read_bytes()).hexdigest()
    output_hash = hashlib.sha256(arguments.output.read_bytes()).hexdigest()
    print(json.dumps({"source": str(arguments.input), "sourceSha256": source_hash, "output": str(arguments.output), "outputSha256": output_hash, "convertedImages": converted}, indent=2))


if __name__ == "__main__":
    main()
