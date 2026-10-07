"""GLBのbaseColorTextureが選ぶUV setを実画像で確認する。"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


WIDTH = 640
HEIGHT = 480
MODELS = ("default", "uv0", "uv1", "uv2", "normalized-uv1", "normalized-byte-uv1")
NORMAL_PIXELS = {
    "top_left": (255, 0, 0),
    "top_right": (0, 255, 0),
    "bottom_left": (0, 0, 255),
    "bottom_right": (255, 255, 255),
}
FLIPPED_PIXELS = {
    "top_left": (0, 255, 0),
    "top_right": (255, 0, 0),
    "bottom_left": (255, 255, 255),
    "bottom_right": (0, 0, 255),
}
SAMPLES = {
    "top_left": (250, 195),
    "top_right": (390, 195),
    "bottom_left": (250, 285),
    "bottom_right": (390, 285),
}


def read_ppm(path):
    """固定サイズのP6画像を読み、RGB byte列を返す。"""
    data = path.read_bytes()
    header, separator, pixels = data.partition(b"\n255\n")
    if not separator:
        raise ValueError(f"{path}: PPM header is incomplete")
    fields = header.split()
    if fields != [b"P6", str(WIDTH).encode(), str(HEIGHT).encode()]:
        raise ValueError(f"{path}: unexpected PPM dimensions or format: {fields!r}")
    expected_bytes = WIDTH * HEIGHT * 3
    if len(pixels) != expected_bytes:
        raise ValueError(f"{path}: expected {expected_bytes} pixel bytes, got {len(pixels)}")
    return pixels


def pixel(image, x, y):
    """座標のRGB値を取得する。"""
    offset = (y * WIDTH + x) * 3
    return tuple(image[offset:offset + 3])


def mismatch_count(left, right):
    """各画素でRGBのいずれかが異なる数を数える。"""
    mismatches = 0
    for offset in range(0, len(left), 3):
        if left[offset:offset + 3] != right[offset:offset + 3]:
            mismatches += 1
    return mismatches


def ensure_close(actual, expected, label):
    """指定画素が期待色から各channelで2以内か確認する。"""
    if any(abs(actual[channel] - expected[channel]) > 2 for channel in range(3)):
        raise AssertionError(f"{label}: expected {expected}, got {actual}")


def capture(executable, model, output_path, output_dir):
    """指定GLBを1 frame描き、capture helperの画像を読む。"""
    stale_paths = (output_path, Path(str(output_path) + ".frame1.ppm"))
    for stale_path in stale_paths:
        stale_path.unlink(missing_ok=True)
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(output_path)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = "1"
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = "0"
    completed = subprocess.run([str(executable), str(model)], cwd=output_dir, env=environment, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    if completed.returncode != 0:
        raise RuntimeError(f"{model.name}: exit {completed.returncode}\n{completed.stdout}\n{completed.stderr}")
    if not output_path.is_file():
        raise RuntimeError(f"{model.name}: capture was not written\n{completed.stdout}\n{completed.stderr}")
    if stale_paths[1].exists():
        raise RuntimeError(f"{model.name}: unexpected extra frame capture: {stale_paths[1]}")
    return read_ppm(output_path), completed.stdout


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

    images = {}
    process_output = {}
    for model_name in MODELS:
        model_path = fixture_dir / f"{model_name}.glb"
        if not model_path.is_file():
            raise FileNotFoundError(model_path)
        images[model_name], process_output[model_name] = capture(executable, model_path, output_dir / f"{model_name}.ppm", output_dir)

    reference = images["uv0"]
    comparisons = {}
    for model_name in ("default",):
        count = mismatch_count(images[model_name], reference)
        comparisons[f"{model_name}_vs_uv0_mismatches"] = count
        if count != 0:
            raise AssertionError(f"{model_name} should match uv0 exactly; mismatches={count}")
    flipped_reference = images["uv1"]
    comparisons["uv1_vs_uv0_mismatches"] = mismatch_count(flipped_reference, reference)
    if comparisons["uv1_vs_uv0_mismatches"] == 0:
        raise AssertionError("uv1 and uv0 images unexpectedly match")
    for model_name in ("uv2", "normalized-uv1", "normalized-byte-uv1"):
        count = mismatch_count(images[model_name], flipped_reference)
        comparisons[f"{model_name}_vs_uv1_mismatches"] = count
        if count != 0:
            raise AssertionError(f"{model_name} should match uv1 exactly; mismatches={count}")

    pixel_results = {}
    for model_name in ("default", "uv0"):
        pixel_results[model_name] = {}
        for region, (x, y) in SAMPLES.items():
            actual = pixel(images[model_name], x, y)
            ensure_close(actual, NORMAL_PIXELS[region], f"{model_name}.{region} at ({x},{y})")
            pixel_results[model_name][region] = actual
    for model_name in ("uv1", "uv2", "normalized-uv1", "normalized-byte-uv1"):
        pixel_results[model_name] = {}
        for region, (x, y) in SAMPLES.items():
            actual = pixel(images[model_name], x, y)
            ensure_close(actual, FLIPPED_PIXELS[region], f"{model_name}.{region} at ({x},{y})")
            pixel_results[model_name][region] = actual

    background_samples = ((80, 400), (500, 400))
    background_results = {}
    for model_name in MODELS:
        for index, (x, y) in enumerate(background_samples):
            actual = pixel(images[model_name], x, y)
            ensure_close(actual, (40, 80, 120), f"{model_name}.background[{index}]")
            background_results[f"{model_name}_{index}"] = actual
    ui_reference = images["default"]
    ui_mismatches = {}
    for model_name in MODELS:
        count = 0
        for y in range(32, 80):
            for x in range(520, 584):
                offset = (y * WIDTH + x) * 3
                if images[model_name][offset:offset + 3] != ui_reference[offset:offset + 3]:
                    count += 1
        ui_mismatches[model_name] = count
        if count != 0:
            raise AssertionError(f"{model_name}: UI marker differs from default in {count} pixels")
        ensure_close(pixel(images[model_name], 540, 50), (0, 255, 0), f"{model_name}.ui")

    result = {
        "models": list(MODELS),
        "dimensions": [WIDTH, HEIGHT],
        "samples": pixel_results,
        "background_samples": background_results,
        "ui_marker_mismatches": ui_mismatches,
        "image_mismatch_counts": comparisons,
        "capture_stdout": process_output,
    }
    (output_dir / "results.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"GLB texture capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
