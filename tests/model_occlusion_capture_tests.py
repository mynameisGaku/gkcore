#!/usr/bin/env python3
"""遮蔽テクスチャGPU画像をbase色焼込み参照と比較する。"""

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
SCENE_CASES = (
    "occlusion-default", "occlusion-no-texture", "occlusion-default-repeat",
    "occlusion-r-values", "occlusion-strength-0",
    "occlusion-strength-half", "occlusion-strength-1",
    "occlusion-uv1-pattern", "occlusion-shared-roles",
    "occlusion-sampler-mixed", "occlusion-strength-mixed",
    "occlusion-mip-9987", "occlusion-mask", "occlusion-stress",
)
UI_CASES = ("occlusion-strength-half", "occlusion-shared-roles",
            "occlusion-sampler-mixed")


def read_ppm(path):
    """P6画像の寸法とbyte数を確認する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise ValueError(f"{path}: unexpected PPM format")
    width, height = map(int, dimensions.split())
    if (width, height) != (WIDTH, HEIGHT) or len(pixels) != WIDTH * HEIGHT * 3:
        raise ValueError(f"{path}: unexpected PPM dimensions or data size")
    return pixels


def pixel(pixels, x, y):
    """画像からRGB値を得る。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def capture(executable, fixture_dir, output_dir, name, mode="scene",
            count=1, start_frame=0):
    """GLBを描いて指定frameの画像を保存する。"""
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
    """全画面または指定範囲の最大差を集計する。"""
    x0, y0, x1, y1 = bounds or (0, 0, WIDTH, HEIGHT)
    differing = 0
    over_ten = 0
    maximum = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            a = pixel(actual, x, y)
            b = pixel(expected, x, y)
            delta = max(abs(a[channel] - b[channel]) for channel in range(3))
            differing += delta > 2
            over_ten += delta > 10
            maximum = max(maximum, delta)
    return {"compared_pixels": (x1 - x0) * (y1 - y0),
            "mismatches_over_2": differing, "pixels_over_10": over_ten,
            "max_channel_difference": maximum}


def require_match(actual, expected, label, bounds=None):
    """参照との差が2以下であることを要求する。"""
    result = compare(actual, expected, bounds)
    if result["mismatches_over_2"]:
        raise AssertionError(f"{label}: {result}")
    return result


def main():
    """fixtureをcaptureして照明、sampler、mip、寿命を検証する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--fixture-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    fixtures = args.fixture_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    actuals = {}
    scene_results = {}
    for name in SCENE_CASES:
        actuals[name] = capture(executable, fixtures, output, name)[0]
        reference = capture(executable, fixtures, output,
                            f"{name}-reference")[0]
        scene_results[name] = require_match(actuals[name], reference,
                                             f"scene/{name}")

    sampler_left = pixel(actuals["occlusion-sampler-mixed"], 250, 240)
    sampler_right = pixel(actuals["occlusion-sampler-mixed"], 390, 240)
    sampler_delta = max(abs(sampler_left[channel] - sampler_right[channel])
                        for channel in range(3))
    if sampler_delta < 10:
        raise AssertionError(f"same-image sampler difference was hidden: {sampler_left} / {sampler_right}")
    strength_left = pixel(actuals["occlusion-strength-mixed"], 250, 240)
    strength_right = pixel(actuals["occlusion-strength-mixed"], 390, 240)
    strength_delta = max(abs(strength_left[channel] - strength_right[channel])
                         for channel in range(3))
    if strength_delta < 10:
        raise AssertionError(f"same-image strengths were not distinct: {strength_left} / {strength_right}")

    mask_left = pixel(actuals["occlusion-mask"], 250, 240)
    mask_right = pixel(actuals["occlusion-mask"], 390, 240)
    if max(abs(mask_left[index] - BACKGROUND[index]) for index in range(3)) > 2:
        raise AssertionError(f"MASK discard changed background: {mask_left}")
    if max(mask_right) < 20:
        raise AssertionError(f"MASK retained half is not visible: {mask_right}")

    ui_results = {}
    for name in UI_CASES:
        actual = capture(executable, fixtures, output, name, "ui")[0]
        reference = capture(executable, fixtures, output,
                            f"{name}-reference", "ui")[0]
        ui_results[name] = require_match(actual, reference, f"ui/{name}")

    direct = capture(executable, fixtures, output,
                     "occlusion-direct-only", "direct")[0]
    direct_ref = capture(executable, fixtures, output,
                         "occlusion-direct-only-reference", "direct")[0]
    direct_result = require_match(direct, direct_ref, "direct-light-unoccluded")
    emission = capture(executable, fixtures, output,
                       "occlusion-emission-only", "emission")[0]
    emission_ref = capture(executable, fixtures, output,
                           "occlusion-emission-only-reference", "emission")[0]
    emission_result = require_match(emission, emission_ref,
                                    "emission-unoccluded")
    mixed = capture(executable, fixtures, output,
                    "occlusion-mixed-lighting", "mixed")[0]
    mixed_ref = capture(executable, fixtures, output,
                        "occlusion-mixed-lighting-reference",
                        "mixed-reference")[0]
    mixed_result = require_match(mixed, mixed_ref,
                                 "ambient-attenuation-only-with-direct-and-emission")

    mip_no = capture(executable, fixtures, output,
                     "occlusion-mip-9987-no-mip", "scene")[0]
    mip_effect = compare(actuals["occlusion-mip-9987"], mip_no, MODEL_BOUNDS)
    if mip_effect["pixels_over_10"] < 100:
        raise AssertionError(f"mip sampler effect was too small: {mip_effect}")

    stress = capture(executable, fixtures, output, "occlusion-stress",
                     "stress", count=2, start_frame=130)
    stress_ref = capture(executable, fixtures, output,
                         "occlusion-stress-reference", "scene")[0]
    stress_results = [require_match(frame, stress_ref,
                                    f"stress/frame{index + 130}")
                      for index, frame in enumerate(stress)]
    stress_stability = require_match(stress[0], stress[1], "stress-frame-stability")

    results = {"dimensions": [WIDTH, HEIGHT], "scene": scene_results,
               "ui": ui_results,
               "same_image_sampler": {"left": sampler_left,
                                       "right": sampler_right,
                                       "delta": sampler_delta},
               "same_image_strength": {"left": strength_left,
                                       "right": strength_right,
                                       "delta": strength_delta},
               "mask": {"discarded": mask_left, "retained": mask_right},
               "direct_only": direct_result, "emission_only": emission_result,
               "mixed_lighting": mixed_result, "mip_contrast": mip_effect,
               "stress": {"frames": [130, 131],
                          "reference": stress_results,
                          "stability": stress_stability}}
    (output / "results.json").write_text(json.dumps(results, indent=2),
                                         encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Model occlusion capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
