"""実GPU画像で透明UI、画像回転、handle寿命、texture cache入れ替えを検査する。"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import zlib


WIDTH = 640
HEIGHT = 480


def png_chunk(kind, data):
    """PNGの長さ、chunk種別、data、CRCを組み立てる。"""
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def write_png(path, width, height, pixels):
    """RGBA pixel列を標準ライブラリだけでPNGへ保存する。"""
    rows = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(rows)) + png_chunk(b"IEND", b""))


def make_fixtures(directory):
    """透明stripe、半透明1x1、色の違う四隅のPNGを生成する。"""
    directory.mkdir(parents=True, exist_ok=True)
    alpha = bytearray()
    alphas = (0, 64, 128, 255)
    for _y in range(64):
        for x in range(64):
            alpha.extend((255, 0, 0, alphas[x // 16]))
    write_png(directory / "alpha.png", 64, 64, bytes(alpha))
    write_png(directory / "half.png", 1, 1, bytes((255, 0, 0, 128)))
    corners = bytearray()
    for y in range(16):
        for x in range(32):
            if y < 8:
                color = (255, 0, 0, 255) if x < 16 else (0, 255, 0, 255)
            else:
                color = (0, 0, 255, 255) if x < 16 else (255, 255, 255, 255)
            corners.extend(color)
    write_png(directory / "corners.png", 32, 16, bytes(corners))


def read_image(path):
    """P6画像の形式、寸法、画素数を検査する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    assert magic == b"P6" and maximum == b"255", f"unexpected capture format: {path}"
    width, height = map(int, dimensions.split())
    assert (width, height) == (WIDTH, HEIGHT), (path, width, height)
    assert len(pixels) == width * height * 3, f"truncated capture: {path}"
    return pixels


def pixel(pixels, x, y):
    """640幅の画像からRGB画素を読む。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def region(pixels, bounds):
    """左上を含み右下を含まない矩形領域をbytesで返す。"""
    x0, y0, x1, y1 = bounds
    return b"".join(bytes(pixel(pixels, x, y)) for y in range(y0, y1) for x in range(x0, x1))


def near_color(actual, expected, tolerance=2):
    """2色の各RGB成分が指定誤差内か判定する。"""
    return all(abs(actual[index] - expected[index]) <= tolerance for index in range(3))


def capture(executable, fixtures, output, mode, count=1, first_frame=0):
    """古い画像を消し、指定modeを実行して今回のPPMだけを読む。"""
    path = output / f"{mode}.ppm"
    paths = [path] + [Path(str(path) + f".frame{frame}.ppm") for frame in range(1, count + 1)]
    for old_path in paths:
        if old_path.exists():
            old_path.unlink()
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(path)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = str(count)
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = str(first_frame)
    completed = subprocess.run([str(executable), mode, str(fixtures)], env=environment, capture_output=True, text=True, encoding="utf-8", check=True, timeout=120)
    if mode in ("baseline", "fx"):
        assert "expected stale-image rejection:" in completed.stdout, (mode, completed.stdout, completed.stderr)
    image_paths = [path] + [Path(str(path) + f".frame{frame}.ppm") for frame in range(1, count)]
    images = [read_image(image_path) for image_path in image_paths]
    assert not paths[-1].exists(), (mode, "capture exceeded requested count", paths[-1])
    return images


def dominant(color, channel):
    """色相が指定した単色に十分寄っているか判定する。"""
    other = [color[index] for index in range(3) if index != channel]
    return color[channel] > 220 and max(other) < 20


def inspect_ui(pixels, name):
    """透明stripe、alpha無視、1x1、四隅回転の画素を独立して確認する。"""
    transparent = pixel(pixels, 40, 64)
    quarter = pixel(pixels, 56, 64)
    stripe_half = pixel(pixels, 72, 64)
    opaque = pixel(pixels, 88, 64)
    half_image = pixel(pixels, 160, 64)
    ignored_alpha = pixel(pixels, 40, 144)
    ignored_alpha_opaque = pixel(pixels, 88, 144)
    reference_corners = {
        "top_left": pixel(pixels, 40, 244),
        "top_right": pixel(pixels, 56, 244),
        "bottom_left": pixel(pixels, 40, 252),
        "bottom_right": pixel(pixels, 56, 252),
    }
    rotated_corners = {
        "top_left": pixel(pixels, 152, 232),
        "top_right": pixel(pixels, 168, 232),
        "bottom_left": pixel(pixels, 152, 264),
        "bottom_right": pixel(pixels, 168, 264),
    }
    assert dominant(transparent, 2), (name, "alpha-zero stripe did not reveal blue backing", transparent)
    assert quarter[0] < stripe_half[0] and quarter[2] > stripe_half[2], (name, "alpha stripe ordering is invalid", quarter, stripe_half)
    assert dominant(opaque, 0), (name, "alpha-255 stripe did not remain red", opaque)
    assert 175 <= half_image[0] <= 195 and 175 <= half_image[2] <= 195 and half_image[1] < 5, (name, "scaled 1x1 half-alpha blend is invalid", half_image)
    assert dominant(ignored_alpha, 0) and dominant(ignored_alpha_opaque, 0), (name, "alphaBlend=false did not draw both transparent and opaque texels red", ignored_alpha, ignored_alpha_opaque)
    expected_reference = {"top_left": 0, "top_right": 1, "bottom_left": 2, "bottom_right": -1}
    expected_rotated = {"top_left": 2, "top_right": 0, "bottom_left": -1, "bottom_right": 1}
    for corner, color in reference_corners.items():
        channel = expected_reference[corner]
        if channel == -1:
            assert min(color) > 220, (name, "unrotated white corner is invalid", color)
        else:
            assert dominant(color, channel), (name, "unrotated corner order is invalid", corner, color)
    for corner, color in rotated_corners.items():
        channel = expected_rotated[corner]
        if channel == -1:
            assert min(color) > 220, (name, "rotated white corner is invalid", color)
        else:
            assert dominant(color, channel), (name, "rotated corner order is invalid", corner, color)
    assert dominant(pixel(pixels, 200, 180), 2), (name, "opaque blue UI backing is missing", pixel(pixels, 200, 180))
    return {
        "transparent": transparent,
        "alpha_64": quarter,
        "alpha_128": stripe_half,
        "alpha_255": opaque,
        "scaled_1x1_alpha_128": half_image,
        "alpha_blend_disabled_samples": [ignored_alpha, ignored_alpha_opaque],
        "unrotated_corners": reference_corners,
        "rotated_corners": rotated_corners,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixtures = output / "fixtures"
    make_fixtures(fixtures)

    baseline = capture(executable, fixtures, output, "baseline")[0]
    effect = capture(executable, fixtures, output, "fx")[0]
    results = {
        "baseline": inspect_ui(baseline, "baseline"),
        "fx": inspect_ui(effect, "fx"),
    }
    scene_baseline = pixel(baseline, 600, 400)
    scene_effect = pixel(effect, 600, 400)
    scene_delta = max(abs(scene_baseline[channel] - scene_effect[channel]) for channel in range(3))
    assert scene_delta > 10, ("Scene effects did not change the background", scene_baseline, scene_effect)
    ui_regions = ((32, 32, 224, 192), (32, 240, 64, 256), (144, 216, 176, 280))
    for bounds in ui_regions:
        assert region(baseline, bounds) == region(effect, bounds), ("Scene effects changed opaque-backed UI pixels", bounds)

    late_images = capture(executable, fixtures, output, "eviction", count=2, first_frame=130)
    results["eviction_frame_130"] = inspect_ui(late_images[0], "eviction frame 130")
    results["eviction_frame_131"] = inspect_ui(late_images[1], "eviction frame 131")
    assert pixel(late_images[0], 600, 400) == scene_baseline, ("baseline Scene changed after cache eviction", pixel(late_images[0], 600, 400), scene_baseline)
    assert pixel(late_images[1], 600, 400) == scene_effect, ("effect Scene changed after cache eviction", pixel(late_images[1], 600, 400), scene_effect)
    for frame, image in enumerate(late_images):
        for bounds in ui_regions:
            assert region(image, bounds) == region(baseline, bounds), (frame, "cache eviction changed UI payload pixels", bounds)
    results["scene_baseline"] = scene_baseline
    results["scene_effect"] = scene_effect
    results["scene_max_channel_delta"] = scene_delta
    results["cache_frames"] = 132
    results["fresh_images_per_frame"] = 3
    results["fresh_images_total"] = 132 * 3
    results["cache_entry_capacity"] = 128
    assert results["fresh_images_total"] > results["cache_entry_capacity"], results
    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
