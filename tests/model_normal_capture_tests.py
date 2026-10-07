"""GLB normal textureのTBN、scale、UV、画像寿命をGPU画素で検査する。"""

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
UV_OFFSETS = ((-2, 0), (2, 0), (0, -2), (0, 2))
SCENE_MODELS = (
    "no-normal",
    "uniform-normal",
    "normal-reference",
    "alpha-ignored",
    "scale-zero",
    "scale-two",
    "scale-two-reference",
    "scale-negative",
    "scale-negative-reference",
    "mirrored-tangent",
    "mirrored-reference",
    "uv1-pattern",
    "uv1-reference",
    "shared-image",
    "shared-reference",
    "shared-image-aliases",
    "mixed-role-image-alias",
    "distinct-image-records",
    "node-mirror-normal",
    "node-mirror-reference",
)
SCENE_PAIRS = (
    ("uniform-normal", "normal-reference", "full"),
    ("alpha-ignored", "normal-reference", "full"),
    ("scale-zero", "no-normal", "full"),
    ("scale-two", "scale-two-reference", "full"),
    ("scale-negative", "scale-negative-reference", "full"),
    ("mirrored-tangent", "mirrored-reference", "full"),
    ("uv1-pattern", "uv1-reference", "uv-samples"),
    ("shared-image", "shared-reference", "full"),
    ("shared-image-aliases", "shared-reference", "full"),
    ("mixed-role-image-alias", "shared-reference", "full"),
    ("distinct-image-records", "shared-reference", "full"),
    ("node-mirror-normal", "node-mirror-reference", "full"),
)
UI_MODELS = ("uniform-normal", "normal-reference", "mirrored-tangent", "mirrored-reference", "uv1-pattern", "uv1-reference", "shared-image", "shared-reference", "shared-image-aliases", "mixed-role-image-alias", "distinct-image-records")
UI_PAIRS = (
    ("uniform-normal", "normal-reference", "full"),
    ("mirrored-tangent", "mirrored-reference", "full"),
    ("uv1-pattern", "uv1-reference", "uv-samples"),
    ("shared-image", "shared-reference", "full"),
    ("shared-image-aliases", "shared-reference", "full"),
    ("mixed-role-image-alias", "shared-reference", "full"),
    ("distinct-image-records", "shared-reference", "full"),
)
BACKGROUND = (40, 80, 120)
UI_GREEN = (0, 255, 0)
MARKER_SAMPLE = (540, 50)
BACKGROUND_SAMPLES = ((80, 400), (500, 400))


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
        raise ValueError(f"{path}: pixel data length is {len(pixels)}")
    return pixels


def pixel(pixels, x, y):
    """画像から座標1点のRGBを取得する。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def remove_old_captures(path, count):
    """今回の取得結果と誤認する古い画像を削除する。"""
    for suffix in [""] + [f".frame{index}.ppm" for index in range(1, count + 1)]:
        candidate = Path(str(path) + suffix) if suffix else path
        candidate.unlink(missing_ok=True)


def capture(executable, fixture_dir, output_dir, model_name, mode="scene", count=1, start_frame=0):
    """GLBを描画し、指定されたframeのPPMだけを読み込む。"""
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
    """全画面または指定範囲でRGB差を集計する。"""
    x0, y0, x1, y1 = bounds or (0, 0, WIDTH, HEIGHT)
    mismatches = 0
    max_difference = 0
    compared = 0
    over_ten = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            first = pixel(left, x, y)
            second = pixel(right, x, y)
            delta = max(abs(first[channel] - second[channel]) for channel in range(3))
            max_difference = max(max_difference, delta)
            mismatches += delta > 2
            over_ten += delta > 10
            compared += 1
    return {"compared_pixels": compared, "mismatches_over_2": mismatches, "pixels_over_10": over_ten, "max_channel_difference": max_difference}


def require_match(left, right, label, bounds=None):
    """参照と比較し、RGB差2を超える画素がないことを確認する。"""
    result = compare_images(left, right, bounds)
    if result["mismatches_over_2"]:
        raise AssertionError(f"{label}: {result}")
    return result


def check_markers(pixels, label):
    """一定のScene背景とUI緑markerを代表点・領域で確認する。"""
    background = [pixel(pixels, x, y) for x, y in BACKGROUND_SAMPLES]
    for index, color in enumerate(background):
        if any(abs(color[channel] - BACKGROUND[channel]) > 2 for channel in range(3)):
            raise AssertionError(f"{label}: background {index} expected {BACKGROUND}, got {color}")
    marker = pixel(pixels, *MARKER_SAMPLE)
    if any(abs(marker[channel] - UI_GREEN[channel]) > 2 for channel in range(3)):
        raise AssertionError(f"{label}: UI marker expected {UI_GREEN}, got {marker}")
    changed = sum(pixel(pixels, x, y) != UI_GREEN for y in range(32, 80) for x in range(520, 584))
    if changed:
        raise AssertionError(f"{label}: UI marker has {changed} non-green pixels")
    return {"background": background, "ui_marker": marker, "ui_marker_non_green_pixels": changed}


def check_uv_samples(left, right, label):
    """UV1模様の四象限と各sample近傍を内部pixelだけで比較する。"""
    samples = {}
    points = tuple((x + dx, y + dy) for x, y in UV_SAMPLE_POINTS for dx, dy in ((0, 0),) + UV_OFFSETS)
    for x, y in points:
        first = pixel(left, x, y)
        second = pixel(right, x, y)
        if any(abs(first[channel] - second[channel]) > 2 for channel in range(3)):
            raise AssertionError(f"{label}: sample ({x},{y}) differs: {first} vs {second}")
        samples[f"{x},{y}"] = {"actual": first, "reference": second}
    return samples


def require_normal_map_effect(mapped, baseline, label):
    """normal mapが無いモデルから十分な画素差を作ることを確認する。"""
    result = compare_images(mapped, baseline, MODEL_BOUNDS)
    if result["pixels_over_10"] < 100:
        raise AssertionError(f"{label}: normal map had insufficient visible effect: {result}")
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
    scene_markers = {}
    for model_name in SCENE_MODELS:
        scene_images[model_name] = capture(executable, fixture_dir, output_dir, model_name, "scene")[0]
        scene_markers[model_name] = check_markers(scene_images[model_name], f"scene/{model_name}")
    scene_comparisons = {}
    for left_name, right_name, comparison_kind in SCENE_PAIRS:
        label = f"scene/{left_name} vs {right_name}"
        if comparison_kind == "uv-samples":
            scene_comparisons[label] = check_uv_samples(scene_images[left_name], scene_images[right_name], label)
        else:
            scene_comparisons[label] = require_match(scene_images[left_name], scene_images[right_name], label)
    scene_comparisons["scene/uniform-normal vs no-normal"] = require_normal_map_effect(scene_images["uniform-normal"], scene_images["no-normal"], "scene/uniform-normal vs no-normal")

    ui_images = {}
    ui_markers = {}
    for model_name in UI_MODELS:
        ui_images[model_name] = capture(executable, fixture_dir, output_dir, model_name, "ui")[0]
        ui_markers[model_name] = check_markers(ui_images[model_name], f"ui/{model_name}")
    ui_comparisons = {}
    for left_name, right_name, comparison_kind in UI_PAIRS:
        label = f"ui/{left_name} vs {right_name}"
        if comparison_kind == "uv-samples":
            ui_comparisons[label] = check_uv_samples(ui_images[left_name], ui_images[right_name], label)
        else:
            ui_comparisons[label] = require_match(ui_images[left_name], ui_images[right_name], label, MODEL_BOUNDS)

    mirror_images = {}
    mirror_comparison = {}
    for model_name in ("uniform-normal", "normal-reference"):
        mirror_images[model_name] = capture(executable, fixture_dir, output_dir, model_name, "mirror")[0]
        check_markers(mirror_images[model_name], f"mirror/{model_name}")
    mirror_comparison["mirror/uniform-normal vs normal-reference"] = require_match(mirror_images["uniform-normal"], mirror_images["normal-reference"], "mirror/uniform-normal vs normal-reference")

    stress_frames = capture(executable, fixture_dir, output_dir, "shared-image", "stress", count=2, start_frame=130)
    stress_markers = [check_markers(frame, f"stress/shared-image/{index}") for index, frame in enumerate(stress_frames)]
    stress_comparisons = [require_match(frame, scene_images["shared-reference"], f"stress frame {index} vs reference") for index, frame in enumerate(stress_frames)]
    stress_pair = require_match(stress_frames[0], stress_frames[1], "stress frame 130 vs 131")
    alias_stress_frames = capture(executable, fixture_dir, output_dir, "shared-image-aliases", "stress", count=2, start_frame=130)
    alias_stress_markers = [check_markers(frame, f"stress/shared-image-aliases/{index}") for index, frame in enumerate(alias_stress_frames)]
    alias_stress_comparisons = [require_match(frame, scene_images["shared-reference"], f"alias stress frame {index} vs reference") for index, frame in enumerate(alias_stress_frames)]
    alias_stress_pair = require_match(alias_stress_frames[0], alias_stress_frames[1], "alias stress frame 130 vs 131")
    alias_frame_image = capture(executable, fixture_dir, output_dir, "shared-image-aliases", "aliases-frame")[0]
    alias_frame_markers = check_markers(alias_frame_image, "aliases-frame/shared-image-aliases")
    alias_frame_comparison = require_match(alias_frame_image, scene_images["shared-reference"], "aliases-frame/shared-image-aliases vs reference")

    results = {
        "dimensions": [WIDTH, HEIGHT],
        "scene_markers": scene_markers,
        "scene_comparisons": scene_comparisons,
        "ui_markers": ui_markers,
        "ui_comparisons": ui_comparisons,
        "runtime_mirror": {"markers": [check_markers(mirror_images[name], f"mirror/{name}") for name in mirror_images], "comparisons": mirror_comparison},
        "stress": {"frames": [130, 131], "markers": stress_markers, "reference_comparisons": stress_comparisons, "frame_comparison": stress_pair},
        "alias_stress": {"frames": [130, 131], "markers": alias_stress_markers, "reference_comparisons": alias_stress_comparisons, "frame_comparison": alias_stress_pair},
        "aliases_frame": {"markers": alias_frame_markers, "reference_comparison": alias_frame_comparison},
    }
    (output_dir / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Model normal capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
