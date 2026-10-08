#!/usr/bin/env python3
"""自己発光材質のGLBをGPU画像と独立した参照GLBで比較する。"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


WIDTH = 640
HEIGHT = 480
MODEL_BOUNDS = (195, 150, 445, 330)
BACKGROUND = (40, 80, 120)
UI_GREEN = (0, 255, 0)
MARKER_SAMPLE = (540, 50)
BACKGROUND_SAMPLES = ((80, 400), (500, 400))
SCENE_PAIRS = (
    ("emissive-default", "emissive-default-reference", None),
    ("emissive-factor", "emissive-factor-reference", None),
    ("emissive-zero-strength", "emissive-zero-strength-reference", None),
    ("emissive-uniform-texture-alpha-ignored", "emissive-uniform-texture-alpha-ignored-reference", None),
    ("emissive-factor-product", "emissive-factor-product-reference", None),
    ("emissive-srgb-mid", "emissive-srgb-mid-reference", None),
    ("emissive-uv1-pattern", "emissive-uv1-pattern-reference", None),
    ("emissive-wrap-mirror", "emissive-wrap-mirror-reference", None),
    ("emissive-shared-base", "emissive-shared-base-reference", None),
    ("emissive-batch-mixed", "emissive-batch-mixed-reference", None),
    ("emissive-sampler-mixed", "emissive-sampler-mixed-reference", None),
    ("emissive-factor-mixed", "emissive-factor-mixed-reference", None),
    ("emissive-mip-9987", "emissive-mip-9987-reference", None),
    ("emissive-mask", "emissive-mask-reference", (330, 205, 435, 275)),
)
UI_CASES = ("emissive-factor", "emissive-srgb-mid", "emissive-shared-base",
            "emissive-sampler-mixed")


def read_ppm(path):
    """P6画像の寸法と全画素byte数を検査する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise ValueError(f"{path}: unexpected PPM format")
    width, height = map(int, dimensions.split())
    if (width, height) != (WIDTH, HEIGHT):
        raise ValueError(f"{path}: expected {WIDTH}x{HEIGHT}, got {width}x{height}")
    if len(pixels) != WIDTH * HEIGHT * 3:
        raise ValueError(f"{path}: invalid pixel byte count {len(pixels)}")
    return pixels


def pixel(pixels, x, y):
    """画像から一点のRGB byteを読む。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def remove_old_captures(path, count):
    """前回の画像が今回の取得と混ざらないよう削除する。"""
    for suffix in [""] + [f".frame{index}.ppm" for index in range(1, count + 1)]:
        candidate = Path(str(path) + suffix) if suffix else path
        candidate.unlink(missing_ok=True)


def capture(executable, fixture_dir, output_dir, name, mode="scene", count=1,
            start_frame=0):
    """fixtureを描き、指定frameのPPMを取得する。"""
    output_path = output_dir / f"{mode}-{name}.ppm"
    remove_old_captures(output_path, count)
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(output_path)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = str(count)
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = str(start_frame)
    completed = subprocess.run(
        [str(executable), str(fixture_dir / f"{name}.glb"), mode],
        cwd=output_dir, env=environment, check=False, capture_output=True,
        text=True, encoding="utf-8", errors="replace", timeout=180)
    if completed.returncode != 0:
        raise RuntimeError(f"{name}/{mode}: exit {completed.returncode}\n"
                           f"{completed.stdout}\n{completed.stderr}")
    paths = [output_path] + [Path(str(output_path) + f".frame{index}.ppm")
                             for index in range(1, count)]
    for path in paths:
        if not path.is_file():
            raise RuntimeError(f"{name}/{mode}: missing capture {path}")
    extra = Path(str(output_path) + f".frame{count}.ppm")
    if extra.exists():
        raise RuntimeError(f"{name}/{mode}: unexpected capture {extra}")
    return [read_ppm(path) for path in paths]


def compare_images(left, right, bounds=None):
    """全画面または矩形のRGB差を集計する。"""
    x0, y0, x1, y1 = bounds or (0, 0, WIDTH, HEIGHT)
    mismatches = 0
    maximum = 0
    over_ten = 0
    compared = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            first = pixel(left, x, y)
            second = pixel(right, x, y)
            delta = max(abs(first[channel] - second[channel]) for channel in range(3))
            mismatches += delta > 2
            over_ten += delta > 10
            maximum = max(maximum, delta)
            compared += 1
    return {"compared_pixels": compared, "mismatches_over_2": mismatches,
            "pixels_over_10": over_ten, "max_channel_difference": maximum}


def require_match(left, right, label, bounds=None):
    """referenceとの差が2以下であることを要求する。"""
    result = compare_images(left, right, bounds)
    if result["mismatches_over_2"]:
        raise AssertionError(f"{label}: {result}")
    return result


def check_frame(pixels, label):
    """背景とUI markerを検査し画面全体の取り違えを防ぐ。"""
    background = [pixel(pixels, x, y) for x, y in BACKGROUND_SAMPLES]
    if any(max(abs(color[channel] - BACKGROUND[channel]) for channel in range(3)) > 2
           for color in background):
        raise AssertionError(f"{label}: unexpected background {background}")
    marker = pixel(pixels, *MARKER_SAMPLE)
    if max(abs(marker[channel] - UI_GREEN[channel]) for channel in range(3)) > 2:
        raise AssertionError(f"{label}: unexpected UI marker {marker}")
    return {"background": background, "ui_marker": marker}


def check_mask(actual, label):
    """MASKの捨てた左側と残した右側をmodel内部で確認する。"""
    left = pixel(actual, 250, 240)
    right = pixel(actual, 390, 240)
    if max(abs(left[channel] - BACKGROUND[channel]) for channel in range(3)) > 2:
        raise AssertionError(f"{label}: discarded emission changed background: {left}")
    if max(right) < 20:
        raise AssertionError(f"{label}: retained emission is missing: {right}")
    return {"discarded": left, "retained": right}


def main():
    """capture suiteを実行してJSON結果を保存する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--fixture-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    fixture_dir = args.fixture_dir.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    scene_images = {}
    scene_checks = {}
    for actual_name, reference_name, _ in SCENE_PAIRS:
        scene_images[actual_name] = capture(executable, fixture_dir, output_dir,
                                            actual_name, "scene")[0]
        scene_checks[actual_name] = check_frame(scene_images[actual_name],
                                                f"scene/{actual_name}")
    scene_comparisons = {}
    for actual_name, reference_name, bounds in SCENE_PAIRS:
        reference = capture(executable, fixture_dir, output_dir,
                            reference_name, "scene")[0]
        scene_comparisons[actual_name] = require_match(
            scene_images[actual_name], reference, f"scene/{actual_name}", bounds)
    sampler_left = pixel(scene_images["emissive-sampler-mixed"], 250, 240)
    sampler_right = pixel(scene_images["emissive-sampler-mixed"], 390, 240)
    sampler_delta = max(abs(sampler_left[channel] - sampler_right[channel])
                        for channel in range(3))
    if sampler_delta < 10:
        raise AssertionError(f"sampler changes did not produce distinct emissions: {sampler_left} vs {sampler_right}")
    sampler_halves = {"left": sampler_left, "right": sampler_right,
                      "max_channel_difference": sampler_delta}
    mask = check_mask(scene_images["emissive-mask"], "scene/emissive-mask")

    ui_comparisons = {}
    for name in UI_CASES:
        actual = capture(executable, fixture_dir, output_dir, name, "ui")[0]
        reference = capture(executable, fixture_dir, output_dir,
                            f"{name}-reference", "ui")[0]
        check_frame(actual, f"ui/{name}")
        ui_comparisons[name] = require_match(actual, reference, f"ui/{name}")

    dark_emissive = capture(executable, fixture_dir, output_dir,
                            "emissive-factor", "dark")[0]
    dark_reference = capture(executable, fixture_dir, output_dir,
                             "emissive-factor-reference", "scene")[0]
    dark_comparison = require_match(dark_emissive, dark_reference,
                                    "dark/emission-independent-of-lighting")

    mip_without = capture(executable, fixture_dir, output_dir,
                          "emissive-mip-9987-no-mip", "scene")[0]
    mip_effect = compare_images(scene_images["emissive-mip-9987"], mip_without,
                                MODEL_BOUNDS)
    if mip_effect["pixels_over_10"] < 100:
        raise AssertionError(f"mip sampler had insufficient effect: {mip_effect}")

    hdr_on = capture(executable, fixture_dir, output_dir,
                     "emissive-hdr-bloom", "bloom")[0]
    hdr_reference = capture(executable, fixture_dir, output_dir,
                            "emissive-hdr-bloom-reference", "bloom")[0]
    hdr_comparison = require_match(hdr_on, hdr_reference, "bloom/hdr-reference")
    hdr_off_reference = capture(executable, fixture_dir, output_dir,
                                "emissive-hdr-bloom-reference", "bloom-off")[0]
    hdr_off = capture(executable, fixture_dir, output_dir,
                      "emissive-hdr-bloom", "bloom-off")[0]
    hdr_off_comparison = require_match(hdr_off, hdr_off_reference,
                                      "bloom-off/hdr-reference")
    bloom_effect = compare_images(hdr_on, hdr_off, (150, 120, 490, 360))
    if bloom_effect["pixels_over_10"] < 20:
        raise AssertionError(f"Bloom had insufficient visible effect: {bloom_effect}")

    stress = capture(executable, fixture_dir, output_dir, "emissive-stress",
                     "stress", count=2, start_frame=130)
    stress_reference = capture(executable, fixture_dir, output_dir,
                               "emissive-stress-reference", "scene")[0]
    stress_comparisons = [require_match(frame, stress_reference,
                                        f"stress/frame{index + 130}")
                          for index, frame in enumerate(stress)]
    stress_stability = require_match(stress[0], stress[1], "stress/frames-130-131")
    stress_markers = [check_frame(frame, f"stress/frame{index + 130}")
                      for index, frame in enumerate(stress)]

    results = {
        "dimensions": [WIDTH, HEIGHT],
        "scene_markers": scene_checks,
        "scene_comparisons": scene_comparisons,
        "same_image_sampler_halves": sampler_halves,
        "mask": mask,
        "ui_comparisons": ui_comparisons,
        "dark_lighting_independence": dark_comparison,
        "mip_sampler_contrast": mip_effect,
        "bloom": {"reference": hdr_comparison,
                  "bloom_off_reference": hdr_off_comparison,
                  "on_vs_off": bloom_effect},
        "stress": {"frames": [130, 131], "markers": stress_markers,
                   "reference_comparisons": stress_comparisons,
                   "frame_comparison": stress_stability},
    }
    (output_dir / "results.json").write_text(json.dumps(results, indent=2),
                                             encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Model emissive capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
