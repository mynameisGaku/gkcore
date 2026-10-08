"""glTF mip filterと生成mipのGPU画素を検証する。"""

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
LEFT_SAMPLE = (250, 240)
RIGHT_SAMPLE = (390, 240)
MIP_CASES = ("mip-min-9984", "mip-min-9985", "mip-min-9986", "mip-min-9987")
MIP_REFERENCE_SRGB = {"mip-min-9984": 0, "mip-min-9985": 137,
                      "mip-min-9986": 99, "mip-min-9987": 151}
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
    """画像からRGB画素を返す。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def remove_old_captures(path, count):
    """前回の実行で作ったPPMだけを削除する。"""
    for suffix in [""] + [f".frame{index}.ppm" for index in range(1, count)]:
        candidate = Path(str(path) + suffix) if suffix else path
        candidate.unlink(missing_ok=True)


def capture(executable, fixture_dir, output_dir, model_name, mode="scene",
            count=1, start_frame=0):
    """GLBを描画し、今回保存されたPPMだけを読む。"""
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
    expected_paths = [output_path] + [Path(str(output_path) + f".frame{index}.ppm")
                                     for index in range(1, count)]
    for expected_path in expected_paths:
        if not expected_path.is_file():
            raise RuntimeError(f"{model_name}/{mode}: missing capture {expected_path}")
    overshoot_path = Path(str(output_path) + f".frame{count}.ppm")
    if overshoot_path.exists():
        raise RuntimeError(f"{model_name}/{mode}: unexpected extra capture {overshoot_path}")
    return [read_ppm(path) for path in expected_paths]


def compare_pixel(actual, reference, point, label, tolerance=3):
    """指定画素がbaked referenceから許容差内か確認する。"""
    actual_color = pixel(actual, *point)
    reference_color = pixel(reference, *point)
    delta = max(abs(actual_color[channel] - reference_color[channel]) for channel in range(3))
    if delta > tolerance:
        raise AssertionError(f"{label}: actual {actual_color}, reference {reference_color}, delta {delta}")
    return {"actual": actual_color, "reference": reference_color, "max_channel_difference": delta}


def require_difference(actual, other, point, label, minimum=5):
    """異なるmip level参照が十分離れた色になるか確認する。"""
    actual_color = pixel(actual, *point)
    other_color = pixel(other, *point)
    delta = max(abs(actual_color[channel] - other_color[channel]) for channel in range(3))
    if delta < minimum:
        raise AssertionError(f"{label}: samples too similar: {actual_color} vs {other_color}")
    return {"first": actual_color, "second": other_color, "max_channel_difference": delta}


def check_markers(pixels, label):
    """背景色とUI markerがcapture中に維持されたか確認する。"""
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
    return {"background": background, "ui_marker": marker}


def capture_pair(executable, fixture_dir, output_dir, name, mode):
    """actual/reference GLBを同じ画面条件で描く。"""
    actual = capture(executable, fixture_dir, output_dir, name, mode)[0]
    reference = capture(executable, fixture_dir, output_dir, f"{name}-reference", mode)[0]
    check_markers(actual, f"{mode}/{name}")
    check_markers(reference, f"{mode}/{name}-reference")
    return actual, reference


def srgb_byte(linear_value):
    """線形reference factorがcaptureで示すSRGB byteを求める。"""
    encoded = (12.92 * linear_value if linear_value <= 0.0031308 else
               1.055 * linear_value ** (1.0 / 2.4) - 0.055)
    return round(encoded * 255.0)


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

    min_results = {}
    min_images = {}
    for name in MIP_CASES + ("mip-no-mipmap",):
        actual, reference = capture_pair(executable, fixture_dir, output_dir, name, "scene")
        min_images[name] = actual
        min_results[name] = compare_pixel(actual, reference, MODEL_CENTER, name)
        if name in MIP_REFERENCE_SRGB:
            expected = MIP_REFERENCE_SRGB[name]
            actual_color = pixel(reference, *MODEL_CENTER)
            if any(abs(channel - expected) > 3 for channel in actual_color):
                raise AssertionError(f"{name}: baked linear reference expected {expected}, got {actual_color}")

    mip_separation = {
        "no-mipmap-vs-linear-mip-linear": require_difference(
            min_images["mip-no-mipmap"], min_images["mip-min-9987"], MODEL_CENTER,
            "base level vs mip level"),
        "nearest-vs-linear-mip-nearest": require_difference(
            min_images["mip-min-9984"], min_images["mip-min-9985"], MODEL_CENTER,
            "nearest vs linear texel filtering"),
        "nearest-vs-linear-mip-linear": require_difference(
            min_images["mip-min-9986"], min_images["mip-min-9987"], MODEL_CENTER,
            "nearest vs linear texel filtering with trilinear mip selection"),
        "mip-nearest-vs-linear": require_difference(
            min_images["mip-min-9984"], min_images["mip-min-9986"], MODEL_CENTER,
            "nearest mip level vs linear mip level"),
    }

    role_results = {}
    pbr_contrasts = {}
    for name, mode in (("mip-shared-roles", "pbr-scene"),
                       ("mip-shared-roles", "pbr-ui"),
                       ("mip-linear-mr", "pbr-scene"),
                       ("mip-linear-normal", "pbr-scene")):
        actual, reference = capture_pair(executable, fixture_dir, output_dir, name, mode)
        role_results[f"{mode}/{name}"] = compare_pixel(actual, reference, MODEL_CENTER,
                                                       f"{mode}/{name}", tolerance=4)
        if mode == "pbr-scene":
            no_mipmap_name = f"{name}-no-mipmap"
            no_mipmap = capture(executable, fixture_dir, output_dir,
                                no_mipmap_name, mode)[0]
            check_markers(no_mipmap, f"{mode}/{no_mipmap_name}")
            pbr_contrasts[name] = require_difference(
                actual, no_mipmap, MODEL_CENTER, f"{name} mip vs no-mipmap")

    npot_results = {}
    for name, expected_linear in (("mip-npot-5x3", 1.0 / 15.0),
                                  ("mip-npot-1x7", 1.0 / 7.0)):
        actual, reference = capture_pair(executable, fixture_dir, output_dir, name, "scene")
        npot_results[name] = compare_pixel(actual, reference, MODEL_CENTER, name, tolerance=4)
        expected_byte = srgb_byte(expected_linear)
        actual_reference = pixel(reference, *MODEL_CENTER)
        if any(abs(channel - expected_byte) > 3 for channel in actual_reference):
            raise AssertionError(f"{name}: full mip average expected {expected_byte}, got {actual_reference}")

    stress_actual = capture(executable, fixture_dir, output_dir, "mip-sampler-stress",
                            "stress", count=2, start_frame=130)
    stress_reference = capture(executable, fixture_dir, output_dir,
                               "mip-sampler-stress-reference", "scene")[0]
    stress_results = []
    for frame_index, frame in enumerate(stress_actual):
        check_markers(frame, f"stress/frame-{130 + frame_index}")
        stress_results.append({
            "no_mipmap": compare_pixel(frame, stress_reference, LEFT_SAMPLE,
                                       f"stress/frame-{130 + frame_index}/base-level"),
            "mipmap": compare_pixel(frame, stress_reference, RIGHT_SAMPLE,
                                    f"stress/frame-{130 + frame_index}/mip-level"),
        })
    stress_stability = {
        "no_mipmap": compare_pixel(stress_actual[0], stress_actual[1], LEFT_SAMPLE,
                                   "stress frame 130 vs 131 base-level"),
        "mipmap": compare_pixel(stress_actual[0], stress_actual[1], RIGHT_SAMPLE,
                                "stress frame 130 vs 131 mip-level"),
    }

    results = {
        "dimensions": [WIDTH, HEIGHT],
        "mip_filter_references": min_results,
        "mip_filter_separation": mip_separation,
        "same_image_color_space_and_roles": role_results,
        "pbr_mipmap_contrasts": pbr_contrasts,
        "npot_mip_chain_endpoint_inclusion": npot_results,
        "stress": {"frames": [130, 131], "references": stress_results,
                   "frame_comparison": stress_stability},
    }
    (output_dir / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Model mip capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
