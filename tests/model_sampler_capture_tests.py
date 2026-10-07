"""glTF samplerのaddressingとfilteringをGPU画像で検証する。"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


WIDTH = 640
HEIGHT = 480
MODEL_BOUNDS = (195, 150, 445, 330)
MODEL_CENTER = (320, 240)
WRAP_PAIRS = (
    "sampler-wrap-repeat",
    "sampler-wrap-mirror",
    "sampler-wrap-clamp",
    "sampler-wrap-u-repeat-v-clamp",
    "sampler-wrap-u-clamp-v-repeat",
    "sampler-wrap-default-repeat",
    "sampler-wrap-negative-integer",
    "sampler-wrap-positive-integer",
)
FILTER_PAIRS = (
    "sampler-mag-nearest",
    "sampler-mag-linear",
    "sampler-min-nearest",
    "sampler-min-linear",
)
ROLE_PAIR = "sampler-role-mixed"
BATCH_PAIR = "sampler-batch-mixed"
BACKGROUND = (40, 80, 120)
UI_GREEN = (0, 255, 0)
MARKER_SAMPLE = (540, 50)
BACKGROUND_SAMPLES = ((80, 400), (500, 400))


def read_ppm(path):
    """P6画像の寸法とRGB byte数を検査する。"""
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
    """画像からRGB pixelを返す。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def remove_old_captures(path, count):
    """前回の画像を今回のcaptureと混同しないように削除する。"""
    for suffix in [""] + [f".frame{index}.ppm" for index in range(1, count + 1)]:
        candidate = Path(str(path) + suffix) if suffix else path
        candidate.unlink(missing_ok=True)


def capture(executable, fixture_dir, output_dir, model_name, mode="scene", count=1, start_frame=0):
    """fixtureを描画し、今回の実行で書かれたPPMだけを返す。"""
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
        timeout=180,
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
    """全画面または範囲内の最大channel差と不一致数を集計する。"""
    x0, y0, x1, y1 = bounds or (0, 0, WIDTH, HEIGHT)
    mismatches = 0
    max_difference = 0
    pixels_over_ten = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            first = pixel(left, x, y)
            second = pixel(right, x, y)
            delta = max(abs(first[channel] - second[channel]) for channel in range(3))
            max_difference = max(max_difference, delta)
            mismatches += delta > 2
            pixels_over_ten += delta > 10
    return {"mismatches_over_2": mismatches, "pixels_over_10": pixels_over_ten, "max_channel_difference": max_difference}


def require_match(left, right, label, bounds=None):
    """実描画をbaked referenceと比べ、2 RGB値を超える差を拒否する。"""
    result = compare_images(left, right, bounds)
    if result["mismatches_over_2"]:
        raise AssertionError(f"{label}: {result}")
    return result


def require_difference(left, right, label, bounds):
    """異なるsampler期待値が十分に違うことを確認する。"""
    result = compare_images(left, right, bounds)
    if result["pixels_over_10"] == 0:
        raise AssertionError(f"{label}: sampler references did not separate: {result}")
    return result


def check_markers(pixels, label):
    """背景とUI markerの代表画素を確認する。"""
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


def check_center(left, right, label):
    """minification filterの中心画素をbaked referenceと照合する。"""
    actual = pixel(left, *MODEL_CENTER)
    expected = pixel(right, *MODEL_CENTER)
    if any(abs(actual[channel] - expected[channel]) > 2 for channel in range(3)):
        raise AssertionError(f"{label}: center sample {actual} differs from baked reference {expected}")
    return {"actual": actual, "reference": expected}


def capture_pair(executable, fixture_dir, output_dir, name, mode):
    """actual/reference GLBを同じmodeで描画する。"""
    actual = capture(executable, fixture_dir, output_dir, name, mode)[0]
    reference = capture(executable, fixture_dir, output_dir, f"{name}-reference", mode)[0]
    check_markers(actual, f"{mode}/{name}")
    check_markers(reference, f"{mode}/{name}-reference")
    return actual, reference


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

    wrap_results = {}
    wrap_images = {}
    for name in WRAP_PAIRS:
        actual, reference = capture_pair(executable, fixture_dir, output_dir, name, "scene")
        wrap_images[name] = actual
        wrap_results[name] = require_match(actual, reference, f"scene/{name}", MODEL_BOUNDS)

    wrap_separation = {
        "default-repeat-equals-explicit-repeat": require_match(wrap_images["sampler-wrap-default-repeat"], wrap_images["sampler-wrap-repeat"], "default repeat vs explicit repeat", MODEL_BOUNDS),
        "negative-equals-positive-integer-repeat": require_match(wrap_images["sampler-wrap-negative-integer"], wrap_images["sampler-wrap-positive-integer"], "negative vs positive integer repeat", MODEL_BOUNDS),
        "repeat-vs-mirror": require_difference(wrap_images["sampler-wrap-repeat"], wrap_images["sampler-wrap-mirror"], "repeat vs mirror", MODEL_BOUNDS),
        "repeat-vs-clamp": require_difference(wrap_images["sampler-wrap-repeat"], wrap_images["sampler-wrap-clamp"], "repeat vs clamp", MODEL_BOUNDS),
    }

    batch_results = {}
    batch_images = {}
    for mode in ("scene", "ui"):
        actual, reference = capture_pair(executable, fixture_dir, output_dir, BATCH_PAIR, mode)
        batch_images[mode] = (actual, reference)
        batch_results[mode] = require_match(actual, reference, f"{mode}/{BATCH_PAIR}", MODEL_BOUNDS)

    role_results = {}
    role_images = {}
    for mode in ("scene-pbr", "ui-pbr"):
        actual, reference = capture_pair(executable, fixture_dir, output_dir, ROLE_PAIR, mode)
        role_images[mode] = (actual, reference)
        role_results[mode] = require_match(actual, reference, f"{mode}/{ROLE_PAIR}", MODEL_BOUNDS)

    filter_results = {}
    filter_images = {}
    for name in FILTER_PAIRS:
        actual, reference = capture_pair(executable, fixture_dir, output_dir, name, "scene")
        filter_images[name] = (actual, reference)
        if name.startswith("sampler-min-"):
            filter_results[name] = check_center(actual, reference, f"scene/{name}")
        else:
            filter_results[name] = require_match(actual, reference, f"scene/{name}", MODEL_BOUNDS)
    filter_separation = {
        "mag-nearest-vs-linear": require_difference(filter_images["sampler-mag-nearest"][0], filter_images["sampler-mag-linear"][0], "mag nearest vs linear", MODEL_BOUNDS),
        "min-nearest-vs-linear": require_difference(filter_images["sampler-min-nearest"][0], filter_images["sampler-min-linear"][0], "min nearest vs linear", (MODEL_CENTER[0], MODEL_CENTER[1], MODEL_CENTER[0] + 1, MODEL_CENTER[1] + 1)),
    }

    stress_frames = capture(executable, fixture_dir, output_dir, BATCH_PAIR, "stress", count=2, start_frame=130)
    stress_markers = [check_markers(frame, f"stress/{BATCH_PAIR}/{index}") for index, frame in enumerate(stress_frames)]
    stress_results = [require_match(frame, batch_images["scene"][1], f"stress frame {130 + index} vs reference", MODEL_BOUNDS) for index, frame in enumerate(stress_frames)]
    stress_stability = require_match(stress_frames[0], stress_frames[1], "stress frame 130 vs 131", MODEL_BOUNDS)

    results = {
        "dimensions": [WIDTH, HEIGHT],
        "wrap_references": wrap_results,
        "wrap_mode_separation": wrap_separation,
        "same_image_material_batch": batch_results,
        "role_specific_pbr_samplers": role_results,
        "mag_min_filter_references": filter_results,
        "mag_min_filter_separation": filter_separation,
        "stress": {"frames": [130, 131], "markers": stress_markers, "reference_comparisons": stress_results, "frame_comparison": stress_stability},
    }
    (output_dir / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Model sampler capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
