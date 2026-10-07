"""GLB metallic-roughness画像をreferenceモデルとGPU画素で比較する。"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


WIDTH = 640
HEIGHT = 480
MODEL_BOUNDS = (195, 150, 445, 330)
UV_SAMPLE_POINTS = ((250, 195), (390, 195), (250, 285), (390, 285))
UV_OUTSIDE_POINTS = ((180, 140), (460, 140), (180, 340), (460, 340))
SCENE_MODELS = (
    "uniform-mr",
    "factor-reference",
    "ignored-ra",
    "scaled-mr",
    "scaled-reference",
    "shared-image",
    "shared-reference",
    "mr-only",
    "mr-only-reference",
    "uv1-pattern",
    "uv1-reference",
    "mixed-pairs",
    "mixed-reference",
    "masked-mr",
    "no-mr-default",
)
SCENE_PAIRS = (
    ("uniform-mr", "factor-reference", "full"),
    ("ignored-ra", "factor-reference", "full"),
    ("scaled-mr", "scaled-reference", "full"),
    ("shared-image", "shared-reference", "full"),
    ("mr-only", "mr-only-reference", "full"),
    ("uv1-pattern", "uv1-reference", "uv-samples"),
    ("mixed-pairs", "mixed-reference", "model-roi"),
    ("masked-mr", "factor-reference", "full"),
)
UI_MODELS = ("uniform-mr", "factor-reference", "shared-image", "shared-reference", "uv1-pattern", "uv1-reference", "mixed-pairs", "mixed-reference")
SAMPLE_POINTS = ((80, 400), (500, 400), (540, 50))
EXPECTED_BACKGROUND = (40, 80, 120)
EXPECTED_UI = (0, 255, 0)


def read_ppm(path):
    """P6画像の形式、寸法、画素データ長を検査する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise ValueError(f"{path}: unexpected PPM format")
    width, height = map(int, dimensions.split())
    if (width, height) != (WIDTH, HEIGHT):
        raise ValueError(f"{path}: expected {WIDTH}x{HEIGHT}, got {width}x{height}")
    if len(pixels) != WIDTH * HEIGHT * 3:
        raise ValueError(f"{path}: pixel data length is {len(pixels)}")
    return pixels


def pixel(pixels, x, y):
    """画像から座標1点のRGBを読む。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def remove_old_captures(path, count):
    """以前のcaptureと余分な連番画像を削除する。"""
    for suffix in [""] + [f".frame{index}.ppm" for index in range(1, count + 1)]:
        candidate = Path(str(path) + suffix) if suffix else path
        candidate.unlink(missing_ok=True)


def capture(executable, fixture_dir, output_dir, model_name, mode="scene", count=1, start_frame=0):
    """GLBを描画し、指定されたframe数だけPPMを取得する。"""
    output_path = output_dir / f"{mode}-{model_name}.ppm"
    remove_old_captures(output_path, count)
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(output_path)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = str(count)
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = str(start_frame)
    model_path = fixture_dir / f"{model_name}.glb"
    completed = subprocess.run(
        [str(executable), str(model_path), mode],
        cwd=output_dir,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"{model_name}/{mode}: exit {completed.returncode}\n{completed.stdout}\n{completed.stderr}")
    expected_paths = [output_path] + [Path(str(output_path) + f".frame{index}.ppm") for index in range(1, count)]
    for expected_path in expected_paths:
        if not expected_path.is_file():
            raise RuntimeError(f"{model_name}/{mode}: missing capture {expected_path}")
    overshoot_path = Path(str(output_path) + f".frame{count}.ppm")
    if overshoot_path.exists():
        raise RuntimeError(f"{model_name}/{mode}: unexpected extra capture {overshoot_path}")
    return [read_ppm(path) for path in expected_paths]


def compare_images(left, right, bounds=None):
    """全画面または指定矩形でRGB差2以内の画素数と最大差を返す。"""
    x0, y0, x1, y1 = bounds or (0, 0, WIDTH, HEIGHT)
    mismatches = 0
    max_difference = 0
    compared = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            first = pixel(left, x, y)
            second = pixel(right, x, y)
            delta = max(abs(first[channel] - second[channel]) for channel in range(3))
            max_difference = max(max_difference, delta)
            mismatches += delta > 2
            compared += 1
    return {"compared_pixels": compared, "mismatches_over_2": mismatches, "max_channel_difference": max_difference}


def require_match(left, right, label, bounds=None):
    """独立referenceと比較し、RGB差2を超える画素がないことを確認する。"""
    result = compare_images(left, right, bounds)
    if result["mismatches_over_2"]:
        raise AssertionError(f"{label}: {result}")
    return result


def check_scene_markers(pixels, label):
    """一定背景とUI緑markerの代表画素を確認する。"""
    background = [pixel(pixels, x, y) for x, y in SAMPLE_POINTS[:2]]
    for index, color in enumerate(background):
        if any(abs(color[channel] - EXPECTED_BACKGROUND[channel]) > 2 for channel in range(3)):
            raise AssertionError(f"{label}: background sample {index} expected {EXPECTED_BACKGROUND}, got {color}")
    marker = pixel(pixels, *SAMPLE_POINTS[2])
    if any(abs(marker[channel] - EXPECTED_UI[channel]) > 2 for channel in range(3)):
        raise AssertionError(f"{label}: UI marker expected {EXPECTED_UI}, got {marker}")
    non_green = 0
    for y in range(32, 80):
        for x in range(520, 584):
            non_green += pixel(pixels, x, y) != EXPECTED_UI
    if non_green:
        raise AssertionError(f"{label}: UI marker contains {non_green} non-green pixels")
    return {"background_samples": background, "ui_marker": marker, "ui_marker_non_green_pixels": non_green}


def check_uv_samples(left, right, label):
    """UV patternの内側4点とモデル周辺4点をreferenceと比較する。"""
    samples = {}
    for x, y in UV_SAMPLE_POINTS + UV_OUTSIDE_POINTS:
        first = pixel(left, x, y)
        second = pixel(right, x, y)
        if any(abs(first[channel] - second[channel]) > 2 for channel in range(3)):
            raise AssertionError(f"{label}: sample ({x},{y}) differs: {first} vs {second}")
        samples[f"{x},{y}"] = {"actual": first, "reference": second}
    return samples


def contrast_proof(textured, default, label):
    """uniform MR画像がfactor 1の同色baseColor材質と異なる画素数を数える。"""
    result = compare_images(textured, default, MODEL_BOUNDS)
    if result["mismatches_over_2"] < 100:
        raise AssertionError(f"{label}: MR texture did not change enough model pixels: {result}")
    return result


def main():
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
    scene_results = {}
    for model_name in SCENE_MODELS:
        scene_images[model_name] = capture(executable, fixture_dir, output_dir, model_name, "scene")[0]
        scene_results[model_name] = check_scene_markers(scene_images[model_name], f"scene/{model_name}")

    comparisons = {}
    for left_name, right_name, comparison_kind in SCENE_PAIRS:
        left = scene_images[left_name]
        right = scene_images[right_name]
        label = f"scene/{left_name} vs {right_name}"
        if comparison_kind == "uv-samples":
            comparisons[label] = check_uv_samples(left, right, label)
        else:
            bounds = MODEL_BOUNDS if comparison_kind == "model-roi" else None
            comparisons[label] = require_match(left, right, label, bounds)
    comparisons["scene/uniform-mr vs no-mr-default"] = contrast_proof(scene_images["uniform-mr"], scene_images["no-mr-default"], "scene/uniform-mr vs no-mr-default")

    ui_images = {}
    ui_results = {}
    for model_name in UI_MODELS:
        ui_images[model_name] = capture(executable, fixture_dir, output_dir, model_name, "ui")[0]
        ui_results[model_name] = check_scene_markers(ui_images[model_name], f"ui/{model_name}")
    ui_comparisons = {}
    ui_pairs = (("uniform-mr", "factor-reference"), ("shared-image", "shared-reference"), ("uv1-pattern", "uv1-reference"), ("mixed-pairs", "mixed-reference"))
    for left_name, right_name in ui_pairs:
        label = f"ui/{left_name} vs {right_name}"
        if left_name == "uv1-pattern":
            ui_comparisons[label] = check_uv_samples(ui_images[left_name], ui_images[right_name], label)
        else:
            ui_comparisons[label] = require_match(ui_images[left_name], ui_images[right_name], label, MODEL_BOUNDS)

    stress_frames = capture(executable, fixture_dir, output_dir, "shared-image", "stress", count=2, start_frame=130)
    stress_results = [check_scene_markers(frame, f"stress/shared-image/{index}") for index, frame in enumerate(stress_frames)]
    stress_comparisons = [require_match(frame, scene_images["shared-reference"], f"stress frame {index} vs reference") for index, frame in enumerate(stress_frames)]
    stress_pair = require_match(stress_frames[0], stress_frames[1], "stress frame 130 vs 131")

    results = {
        "dimensions": [WIDTH, HEIGHT],
        "scene_markers": scene_results,
        "scene_comparisons": comparisons,
        "ui_markers": ui_results,
        "ui_comparisons": ui_comparisons,
        "stress": {"frames": [130, 131], "markers": stress_results, "reference_comparisons": stress_comparisons, "frame_comparison": stress_pair},
    }
    (output_dir / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Model material capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
