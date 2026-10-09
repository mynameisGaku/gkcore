"""GLB alpha blend、mask、opaqueの実GPU画像を独立した期待色で検査する。"""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess

from support.model_blend_fixtures import generate


WIDTH = 640
HEIGHT = 480


def read_capture(path):
    """P6画像の寸法とpixel payloadを検証する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    assert magic == b"P6" and maximum == b"255", (path.name, "invalid PPM header")
    width, height = map(int, dimensions.split())
    assert (width, height) == (WIDTH, HEIGHT), (path.name, width, height)
    assert len(pixels) == WIDTH * HEIGHT * 3, (path.name, "truncated PPM")
    return pixels


def pixel(pixels, x, y):
    """640x480画像から指定位置のRGBを読む。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def encode_srgb(linear):
    """linear channelをsRGB byteへ変換する。"""
    value = 12.92 * linear if linear <= 0.0031308 else 1.055 * math.pow(linear, 1.0 / 2.4) - 0.055
    return int(round(max(0.0, min(1.0, value)) * 255.0))


def blend_over(background, foreground, alpha):
    """linear RGBのsource-over期待値をsRGBへ変換する。"""
    return tuple(encode_srgb(foreground[channel] * alpha + background[channel] * (1.0 - alpha)) for channel in range(3))


def assert_color(actual, expected, label, tolerance=3):
    """GPU出力と独立計算したRGBを許容差付きで比較する。"""
    assert max(abs(actual[index] - expected[index]) for index in range(3)) <= tolerance, (label, actual, expected)


def image_difference(actual, expected, tolerance=12):
    """2枚のRGB画像の画素差を全frameで集計する。"""
    changed_pixels = 0
    max_difference = 0
    difference_sum = 0
    for offset in range(0, len(actual), 3):
        pixel_difference = max(abs(actual[offset + channel] - expected[offset + channel]) for channel in range(3))
        max_difference = max(max_difference, pixel_difference)
        difference_sum += sum(abs(actual[offset + channel] - expected[offset + channel]) for channel in range(3))
        changed_pixels += pixel_difference > tolerance
    mean_difference = difference_sum / max(1, len(actual))
    return {"changed_pixels": changed_pixels, "max_channel_difference": max_difference,
            "mean_channel_difference": mean_difference}


def compare_images(actual, expected, label, tolerance=2, allowed_changed_pixels=32):
    """独立GLB参照との全frame差を測り、大きな投影ずれを拒否する。"""
    differences = image_difference(actual, expected, tolerance)
    changed_pixels = differences["changed_pixels"]
    max_difference = differences["max_channel_difference"]
    mean_difference = differences["mean_channel_difference"]
    assert changed_pixels <= allowed_changed_pixels, (label, "too many mismatched pixels", changed_pixels, max_difference, mean_difference)
    assert mean_difference <= 0.05, (label, "mean image difference is too large", changed_pixels, max_difference, mean_difference)
    return differences


def run_capture(executable, fixture_dir, output_dir, mode):
    """modeごとに新しい実行を起動し、1枚だけ画像を保存する。"""
    capture = output_dir / (mode + ".ppm")
    extra = Path(str(capture) + ".frame1.ppm")
    for path in (capture, extra):
        if path.exists():
            path.unlink()
    environment = os.environ.copy()
    environment.pop("GKCORE_TEST_CAPTURE_START_FRAME", None)
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(capture)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = "1"
    subprocess.run([str(executable), str(fixture_dir), mode], env=environment, check=True, timeout=60)
    assert not extra.exists(), (mode, "single-frame capture wrote an extra image")
    return read_capture(capture)


def inspect_mode(executable, fixture_dir, output_dir, mode):
    """指定modeの画面色、順序、UIの分離を確認する。"""
    pixels = run_capture(executable, fixture_dir, output_dir, mode)
    ui = pixel(pixels, 540, 50)
    assert_color(ui, (0, 255, 0), (mode, "UI marker"))
    results = {"ui_marker": ui}

    if mode == "swatch":
        background = pixel(pixels, 8, 8)
        assert_color(background, (0, 0, 255), (mode, "Scene background"))
        alphas = (0.0, (64.0 / 255.0) * 0.5, (128.0 / 255.0) * 0.5, 0.5)
        for band, x in enumerate((225, 290, 350, 415)):
            actual = pixel(pixels, x, 240)
            expected = blend_over((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), alphas[band])
            assert_color(actual, expected, (mode, "alpha band", band))
            results[f"alpha_{band}"] = {"actual": actual, "expected": expected}
        return results

    if mode == "overlay-barrier":
        actual = pixel(pixels, 320, 240)
        assert_color(actual, (0, 0, 255), (mode, "opaque Scene command remains after earlier transparent draw"))
        results["scene_overlay"] = actual
        return results

    if mode in ("overlap-scene", "overlap-ui", "overlap-camera-reverse", "triangles-scene", "triangles-camera-reverse"):
        actual = pixel(pixels, 320, 240)
        reverse = mode in ("overlap-ui", "overlap-camera-reverse", "triangles-camera-reverse")
        far_color = (1.0, 0.0, 0.0) if reverse else (0.0, 0.0, 1.0)
        near_color = (0.0, 0.0, 1.0) if reverse else (1.0, 0.0, 0.0)
        linear_after_far = tuple(channel * 0.5 for channel in far_color)
        linear_final = tuple(near_color[index] * 0.5 + linear_after_far[index] * 0.5 for index in range(3))
        expected = tuple(encode_srgb(value) for value in linear_final)
        assert_color(actual, expected, (mode, "overlapping translucent models"))
        results["overlap"] = {"actual": actual, "expected": expected}
        return results

    if mode == "mask-depth":
        transparent_texel = pixel(pixels, 250, 240)
        opaque_texel = pixel(pixels, 390, 240)
        assert_color(transparent_texel, (0, 255, 0), (mode, "MASK hole reveals opaque back"))
        assert_color(opaque_texel, (255, 0, 0), (mode, "MASK opaque texel writes front"))
        results["transparent_texel"] = transparent_texel
        results["opaque_texel"] = opaque_texel
        return results

    if mode == "opaque-front":
        actual = pixel(pixels, 320, 240)
        assert_color(actual, (255, 0, 0), (mode, "OPAQUE ignores factor alpha"))
        results["opaque_front"] = actual
        return results

    raise AssertionError((mode, "unknown capture mode"))


def inspect_projection(executable, fixture_dir, output_dir):
    """複合変換のGPU投影と、事前変換GLBの画像を比較する。"""
    opaque_actual = run_capture(executable, fixture_dir, output_dir, "projection-opaque-actual")
    opaque_reference = run_capture(executable, fixture_dir, output_dir, "projection-opaque-reference")
    opaque = compare_images(opaque_actual, opaque_reference, "opaque GPU transform vs baked reference")
    opaque_identity = run_capture(executable, fixture_dir, output_dir, "projection-opaque-identity")
    transform_effect = image_difference(opaque_actual, opaque_identity, tolerance=12)
    assert transform_effect["changed_pixels"] > 500, ("opaque transform sensitivity", transform_effect)
    blend_actual = run_capture(executable, fixture_dir, output_dir, "projection-blend-actual")
    blend_reference = run_capture(executable, fixture_dir, output_dir, "projection-blend-reference")
    blend = compare_images(blend_actual, blend_reference, "BLEND GPU transform vs baked reference")
    blend_reverse = run_capture(executable, fixture_dir, output_dir, "projection-blend-reversed")
    sorting = compare_images(blend_actual, blend_reverse, "BLEND submission order independence", tolerance=0, allowed_changed_pixels=0)
    assert_color(pixel(blend_actual, 320, 240), pixel(blend_reverse, 320, 240), "BLEND sorted overlap center", tolerance=0)
    return {"opaque_reference": opaque, "opaque_transform_sensitivity": transform_effect,
            "blend_reference": blend, "blend_sorting": sorting}


def main():
    """生成fixtureを読み込み、alpha描画と複合変換の実GPU画像を検証する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--fixture-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()
    executable = arguments.executable.resolve()
    fixture_dir = arguments.fixture_dir.resolve()
    output_dir = arguments.output_dir.resolve()
    fixture_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)
    count = generate(fixture_dir)
    assert count == 13, ("unexpected fixture count", count)
    modes = ("swatch", "overlap-scene", "overlap-ui", "overlap-camera-reverse", "overlay-barrier",
             "triangles-scene", "triangles-camera-reverse", "mask-depth", "opaque-front")
    results = {mode: inspect_mode(executable, fixture_dir, output_dir, mode) for mode in modes}
    results["projection-transform"] = inspect_projection(executable, fixture_dir, output_dir)
    (output_dir / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
