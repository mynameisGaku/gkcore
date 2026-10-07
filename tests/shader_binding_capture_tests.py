#!/usr/bin/env python3
"""実GPU画像で複数texture bindingと描画snapshotの保持を検査する。"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import zlib


WIDTH = 640
HEIGHT = 480
PATTERN_A = (
    ((255, 0, 0), (0, 255, 0)),
    ((0, 0, 255), (255, 255, 255)),
)
PATTERN_B = (
    ((255, 255, 0), (0, 255, 255)),
    ((255, 0, 255), (0, 0, 0)),
)


def png_chunk(kind, payload):
    """PNG chunkの長さとCRCを付けて組み立てる。"""
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def write_pattern_png(path, pattern):
    """不透明な64x32四象限テスト画像を書き出す。"""
    rows = bytearray()
    for y in range(32):
        rows.append(0)
        for x in range(64):
            color = pattern[y // 16][x // 32]
            rows.extend((*color, 255))
    header = struct.pack(">IIBBBBB", 64, 32, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
    png += png_chunk(b"IDAT", zlib.compress(bytes(rows))) + png_chunk(b"IEND", b"")
    path.write_bytes(png)


def verify_pattern_png(path, pattern):
    """生成したPNGの寸法、四象限、全画素の不透明度を自己検査する。"""
    data = path.read_bytes()
    assert data.startswith(b"\x89PNG\r\n\x1a\n"), (path.name, "invalid PNG signature")
    offset = 8
    compressed = bytearray()
    dimensions = None
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        assert crc == zlib.crc32(kind + payload), (path.name, "invalid PNG chunk CRC")
        if kind == b"IHDR":
            width, height, depth, color_type, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            dimensions = (width, height, depth, color_type, compression, filtering, interlace)
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        offset += 12 + length
    assert dimensions == (64, 32, 8, 6, 0, 0, 0), (path.name, dimensions)
    decoded = zlib.decompress(compressed)
    assert len(decoded) == 32 * (1 + 64 * 4), (path.name, "invalid PNG pixel length")
    for y in range(32):
        row = decoded[y * 257:(y + 1) * 257]
        assert row[0] == 0, (path.name, "unexpected PNG row filter")
        for x in range(64):
            offset = 1 + x * 4
            expected = (*pattern[y // 16][x // 32], 255)
            assert tuple(row[offset:offset + 4]) == expected, (path.name, x, y, expected)


def read_capture(path):
    """GPUから取得したP6画像の寸法とRGB byte数を検査する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    assert magic == b"P6" and maximum == b"255", (path.name, "unexpected PPM header")
    width, height = map(int, dimensions.split())
    assert (width, height) == (WIDTH, HEIGHT), (path.name, width, height)
    assert len(pixels) == WIDTH * HEIGHT * 3, (path.name, "truncated PPM image")
    return pixels


def pixel(pixels, x, y):
    """640幅のRGB画像から一画素を読む。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def region(pixels, bounds):
    """右下を含まない矩形のRGBを行ごとに取り出す。"""
    x0, y0, x1, y1 = bounds
    return b"".join(pixels[(y * WIDTH + x0) * 3:(y * WIDTH + x1) * 3] for y in range(y0, y1))


def assert_color(actual, expected, label, tolerance=2):
    """RGB各成分が期待値の許容範囲にあることを確かめる。"""
    assert all(abs(value - target) <= tolerance for value, target in zip(actual, expected)), (label, actual, expected)


def inspect_quadrants(pixels, origin, pattern, name):
    """四象限それぞれの中心付近4画素が期待色か検査する。"""
    x0, y0 = origin
    results = {}
    for row in range(2):
        for column in range(2):
            center_x = x0 + column * 32 + 16
            center_y = y0 + row * 16 + 8
            samples = [pixel(pixels, center_x + dx, center_y + dy)
                       for dy in (-2, 2) for dx in (-2, 2)]
            expected = pattern[row][column]
            for index, sample in enumerate(samples):
                assert_color(sample, expected, f"{name} quadrant {row},{column} sample {index}")
            results[f"{row},{column}"] = {"expected": expected, "samples": samples}
    return results


def inspect_frame(pixels, frame):
    """矩形、fallback、四枚の画像bindingを一枚のcaptureで確認する。"""
    even = frame % 2 == 0
    scene_colors = ((255, 0, 0), (0, 255, 0)) if even else ((0, 255, 0), (255, 0, 0))
    ui_colors = ((0, 0, 255), (255, 255, 0)) if even else ((255, 255, 0), (0, 0, 255))
    result = {"frame": frame, "scene_rectangles": {}, "ui_rectangles": {}}
    for index, x in enumerate((32, 128)):
        scene = pixel(pixels, x + 32, 32 + 24)
        ui = pixel(pixels, x + 32, 112 + 24)
        assert_color(scene, scene_colors[index], f"frame {frame} Scene rectangle {index}")
        assert_color(ui, ui_colors[index], f"frame {frame} UI rectangle {index}")
        result["scene_rectangles"][str(index)] = scene
        result["ui_rectangles"][str(index)] = ui

    scene_fallback = pixel(pixels, 256, 56)
    ui_fallback = pixel(pixels, 256, 224)
    assert_color(scene_fallback, (0, 0, 0), f"frame {frame} zero-constant Scene fallback")
    assert_color(ui_fallback, (0, 255, 255), f"frame {frame} white-texture UI fallback")
    result["scene_fallback"] = scene_fallback
    result["ui_fallback"] = ui_fallback

    result["scene_pattern_a"] = inspect_quadrants(pixels, (32, 208), PATTERN_A, f"frame {frame} Scene A")
    result["scene_pattern_b"] = inspect_quadrants(pixels, (128, 208), PATTERN_B, f"frame {frame} Scene B")
    result["ui_pattern_a"] = inspect_quadrants(pixels, (320, 208), PATTERN_A, f"frame {frame} UI A")
    result["ui_pattern_b"] = inspect_quadrants(pixels, (416, 208), PATTERN_B, f"frame {frame} UI B")
    return result


def main():
    """生成入力画像を作り、6フレームのshader binding結果を記録する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--shader", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    shader = args.shader.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output / "shader-binding-fixtures"
    fixture.mkdir(parents=True, exist_ok=True)
    pattern_a = fixture / "pattern_a.png"
    pattern_b = fixture / "pattern_b.png"
    write_pattern_png(pattern_a, PATTERN_A)
    write_pattern_png(pattern_b, PATTERN_B)
    verify_pattern_png(pattern_a, PATTERN_A)
    verify_pattern_png(pattern_b, PATTERN_B)

    capture = output / "shader_binding.ppm"
    captures = [capture] + [Path(str(capture) + f".frame{frame}.ppm") for frame in range(1, 6)]
    extra_capture = Path(str(capture) + ".frame6.ppm")
    for path in captures + [extra_capture]:
        if path.exists():
            path.unlink()

    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(capture)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = "6"
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = "0"
    completed = subprocess.run([str(executable), str(shader), str(fixture)], env=environment,
                               capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90)
    assert completed.returncode == 0, ("shader binding capture failed", completed.returncode,
                                       completed.stdout, completed.stderr)
    assert "expected shader deletion rejection:" in completed.stdout, completed.stdout
    assert "expected stale shader rejection:" in completed.stdout, completed.stdout
    assert not extra_capture.exists(), "capture exceeded the requested six frames"

    frame_images = []
    frame_results = []
    for frame, path in enumerate(captures):
        assert path.is_file(), (frame, "missing frame capture", str(path))
        pixels = read_capture(path)
        frame_images.append(pixels)
        frame_results.append(inspect_frame(pixels, frame))
    for frame in range(2, 6, 2):
        assert frame_images[frame] == frame_images[0], (frame, "even frame image changed unexpectedly")
    for frame in range(3, 6, 2):
        assert frame_images[frame] == frame_images[1], (frame, "odd frame image changed unexpectedly")

    # 色定数を交互に変えても、画像と固定色の領域全体は変わらない。
    stable_regions = ((32, 208, 96, 240), (128, 208, 192, 240), (320, 208, 384, 240), (416, 208, 480, 240), (224, 208, 288, 240), (224, 32, 288, 80))
    for frame, image in enumerate(frame_images[1:], 1):
        for bounds in stable_regions:
            assert region(image, bounds) == region(frame_images[0], bounds), (frame, "fixed binding region changed", bounds)

    results = {
        "frames": 6,
        "texture_draws_verified_per_frame": 4,
        "capture_files": [path.name for path in captures],
        "expected_shader_lifetime_markers": [
            "expected shader deletion rejection:",
            "expected stale shader rejection:",
        ],
        "frames_detail": frame_results,
    }
    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
