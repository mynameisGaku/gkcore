#!/usr/bin/env python3
"""missing-NORMAL自動生成を独立GLB参照とGPU画像で検査する。"""

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
    ("generated-normal-flat", "generated-normal-flat-reference"),
    ("generated-normal-tangent-ignored",
     "generated-normal-tangent-ignored-reference"),
    ("generated-normal-uv1", "generated-normal-uv1-reference"),
    ("generated-normal-sharp-fold", "generated-normal-sharp-fold-reference"),
    ("generated-normal-coplanar-shared",
     "generated-normal-coplanar-shared-reference"),
    ("generated-normal-node-transform",
     "generated-normal-node-transform-reference"),
    ("generated-normal-tiny-node", "generated-normal-tiny-node-reference"),
    ("generated-normal-non-indexed",
     "generated-normal-non-indexed-reference"),
    ("generated-normal-unused-source-vertex",
     "generated-normal-unused-source-vertex-reference"),
    ("generated-normal-no-map", "generated-normal-no-map-reference"),
    ("generated-normal-no-map-tangent-ignored",
     "generated-normal-no-map-tangent-ignored-reference"),
    ("generated-normal-degenerate-uv-no-map",
     "generated-normal-degenerate-uv-no-map-reference"),
)
SCENE_MODELS = tuple(name for pair in SCENE_PAIRS for name in pair) + (
    "generated-normal-sharp-fold-averaged",)
UI_PAIRS = (SCENE_PAIRS[0], SCENE_PAIRS[2], SCENE_PAIRS[3])


def read_ppm(path):
    """P6画像の寸法とRGB byte数を検査する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise ValueError(f"{path}: unexpected PPM format")
    width, height = map(int, dimensions.split())
    if (width, height) != (WIDTH, HEIGHT) or len(pixels) != WIDTH * HEIGHT * 3:
        raise ValueError(f"{path}: unexpected PPM size")
    return pixels


def pixel(pixels, x, y):
    """画像の指定pixelからRGBを取り出す。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def capture(executable, fixture_dir, output_dir, name, mode="scene",
            count=1, start_frame=0):
    """GLBを描き、要求したframeのcaptureを読み込む。"""
    target = output_dir / f"{mode}-{name}.ppm"
    for suffix in [""] + [f".frame{index}.ppm" for index in range(1, count + 1)]:
        (Path(str(target) + suffix) if suffix else target).unlink(missing_ok=True)
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(target)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = str(count)
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = str(start_frame)
    completed = subprocess.run(
        [str(executable), str(fixture_dir / f"{name}.glb"), mode],
        cwd=output_dir, env=environment, check=False, capture_output=True,
        text=True, encoding="utf-8", errors="replace", timeout=180)
    if completed.returncode != 0:
        raise RuntimeError(f"{name}/{mode}: exit {completed.returncode}\n"
                           f"{completed.stdout}\n{completed.stderr}")
    paths = [target] + [Path(str(target) + f".frame{index}.ppm")
                        for index in range(1, count)]
    if any(not path.is_file() for path in paths):
        raise RuntimeError(f"{name}/{mode}: missing capture")
    return [read_ppm(path) for path in paths]


def compare(actual, expected, bounds=None):
    """全画面または指定範囲のRGB差を集計する。"""
    x0, y0, x1, y1 = bounds or (0, 0, WIDTH, HEIGHT)
    mismatches = 0
    over_ten = 0
    maximum = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            first = pixel(actual, x, y)
            second = pixel(expected, x, y)
            delta = max(abs(first[channel] - second[channel])
                        for channel in range(3))
            mismatches += delta > 2
            over_ten += delta > 10
            maximum = max(maximum, delta)
    return {"compared_pixels": (x1 - x0) * (y1 - y0),
            "mismatches_over_2": mismatches, "pixels_over_10": over_ten,
            "max_channel_difference": maximum}


def require_match(actual, expected, label):
    """参照と差2以下で全画面一致することを要求する。"""
    result = compare(actual, expected)
    if result["mismatches_over_2"]:
        raise AssertionError(f"{label}: {result}")
    return result


def check_markers(pixels, label):
    """Scene背景とUI markerが保持されることを確認する。"""
    backgrounds = [pixel(pixels, x, y) for x, y in BACKGROUND_SAMPLES]
    for index, color in enumerate(backgrounds):
        if any(abs(color[channel] - BACKGROUND[channel]) > 2
               for channel in range(3)):
            raise AssertionError(f"{label}: background{index} was {color}")
    marker = pixel(pixels, *MARKER_SAMPLE)
    if any(abs(marker[channel] - UI_GREEN[channel]) > 2
           for channel in range(3)):
        raise AssertionError(f"{label}: UI marker was {marker}")
    return {"background": backgrounds, "ui_marker": marker}


def main():
    """自動法線のScene/UI画像とcontrast controlを検査する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--fixture-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    fixtures = args.fixture_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    scene_images = {}
    scene_markers = {}
    for name in SCENE_MODELS:
        scene_images[name] = capture(executable, fixtures, output, name)[0]
        scene_markers[name] = check_markers(scene_images[name], f"scene/{name}")

    scene_results = {}
    for actual_name, reference_name in SCENE_PAIRS:
        scene_results[actual_name] = require_match(
            scene_images[actual_name], scene_images[reference_name],
            f"scene/{actual_name}/{reference_name}")

    sharp_control = compare(scene_images["generated-normal-sharp-fold"],
                            scene_images["generated-normal-sharp-fold-averaged"],
                            MODEL_BOUNDS)
    if sharp_control["mismatches_over_2"] <= 100:
        raise AssertionError(f"flat-normal contrast was too small: {sharp_control}")

    normal_effect = compare(scene_images["generated-normal-flat"],
                            scene_images["generated-normal-no-map"],
                            MODEL_BOUNDS)
    if normal_effect["pixels_over_10"] < 100:
        raise AssertionError(f"normal map had insufficient effect: {normal_effect}")

    ui_results = {}
    for actual_name, reference_name in UI_PAIRS:
        actual = capture(executable, fixtures, output, actual_name, "ui")[0]
        reference = capture(executable, fixtures, output, reference_name, "ui")[0]
        check_markers(actual, f"ui/{actual_name}")
        ui_results[actual_name] = require_match(
            actual, reference, f"ui/{actual_name}/{reference_name}")

    mirror_images = {
        name: capture(executable, fixtures, output, name, "mirror")[0]
        for name in ("generated-normal-flat",
                     "generated-normal-flat-reference")}
    mirror_result = require_match(
        mirror_images["generated-normal-flat"],
        mirror_images["generated-normal-flat-reference"],
        "runtime-mirror/generated-normal-flat")

    stress = capture(executable, fixtures, output, "generated-normal-flat",
                     "stress", count=2, start_frame=130)
    stress_reference = [require_match(
        frame, scene_images["generated-normal-flat-reference"],
        f"stress/frame{index + 130}") for index, frame in enumerate(stress)]
    stress_stability = require_match(stress[0], stress[1],
                                     "stress/frame130-vs-131")
    results = {"dimensions": [WIDTH, HEIGHT], "scene_markers": scene_markers,
               "scene": scene_results,
               "sharp_fold_averaged_control": sharp_control,
               "normal_map_effect": normal_effect, "ui": ui_results,
               "runtime_mirror": mirror_result,
               "stress": {"frames": [130, 131],
                          "reference": stress_reference,
                          "stability": stress_stability}}
    (output / "results.json").write_text(json.dumps(results, indent=2),
                                         encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Generated normal capture validation failed: {error}",
              file=sys.stderr)
        sys.exit(1)
