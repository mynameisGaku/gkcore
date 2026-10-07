"""GLB alpha MASKの閾値、描画層、depthへの影響を実画像で検査する。"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


WIDTH = 640
HEIGHT = 480
BACKGROUND = (40, 80, 120)
RED = (255, 0, 0)
BLUE = (0, 0, 255)
GREEN = (0, 255, 0)
STRIPE_SAMPLES = ((220, 240), (285, 240), (350, 240), (415, 240))
SCENE_CASES = {
    "opaque-default": (RED, RED, RED, RED),
    "opaque": (RED, RED, RED, RED),
    "opaque-cutoff-two": (RED, RED, RED, RED),
    "mask-default": (BACKGROUND, BACKGROUND, RED, RED),
    "mask-zero": (RED, RED, RED, RED),
    "mask-one": (BACKGROUND, BACKGROUND, BACKGROUND, RED),
    "mask-two": (BACKGROUND, BACKGROUND, BACKGROUND, BACKGROUND),
    "mask-factor-half": (BACKGROUND, BACKGROUND, BACKGROUND, RED),
    "mask-factor-quarter": (BACKGROUND, BACKGROUND, BACKGROUND, BACKGROUND),
    "mask-equality": (BACKGROUND, BACKGROUND, RED, RED),
    "mask-no-texture": (BACKGROUND, BACKGROUND, BACKGROUND, BACKGROUND),
}
UI_CASES = ("mask-default", "mask-zero", "mask-one", "mask-no-texture")


def read_ppm(path):
    """P6画像の形式、寸法、全画素の長さを検査する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise ValueError(f"{path}: unexpected capture format")
    width, height = map(int, dimensions.split())
    if (width, height) != (WIDTH, HEIGHT):
        raise ValueError(f"{path}: expected {WIDTH}x{HEIGHT}, got {width}x{height}")
    if len(pixels) != WIDTH * HEIGHT * 3:
        raise ValueError(f"{path}: truncated pixel data")
    return pixels


def pixel(pixels, x, y):
    """画像から1画素のRGB値を読む。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def remove_old_captures(path):
    """今回の結果と誤認しないよう、以前の画像を削除する。"""
    path.unlink(missing_ok=True)
    Path(str(path) + ".frame1.ppm").unlink(missing_ok=True)


def capture(executable, fixture_dir, output_dir, model_name, mode):
    """モデルを1回実行し、今回作られたPPMだけを読み込む。"""
    path = output_dir / f"{mode}-{model_name}.ppm"
    remove_old_captures(path)
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(path)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = "1"
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = "0"
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
    if not path.is_file():
        raise RuntimeError(f"{model_name}/{mode}: capture file was not written")
    overshoot = Path(str(path) + ".frame1.ppm")
    if overshoot.exists():
        raise RuntimeError(f"{model_name}/{mode}: unexpected extra capture {overshoot}")
    return read_ppm(path)


def validate_custom_rejection(executable, fixture_dir, output_dir, shader):
    """mask付きモデルへのcustom shader適用がPresentで拒否されることを確認する。"""
    path = output_dir / "custom-reject.ppm"
    remove_old_captures(path)
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(path)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = "1"
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = "0"
    model_path = fixture_dir / "mask-default.glb"
    completed = subprocess.run(
        [str(executable), str(model_path), "custom-reject", str(shader)],
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
        raise RuntimeError(f"custom rejection process exited {completed.returncode}\n{completed.stdout}\n{completed.stderr}")
    marker = "expected masked custom shader rejection:"
    diagnostic = "custom pixel shaders do not support masked model materials"
    if marker not in completed.stdout or diagnostic not in completed.stdout:
        raise AssertionError(f"expected rejection diagnostic missing\nstdout={completed.stdout}\nstderr={completed.stderr}")
    if path.exists() or Path(str(path) + ".frame1.ppm").exists():
        raise AssertionError("custom shader rejection unexpectedly produced a capture")
    return {"marker_seen": True, "diagnostic_seen": True, "capture_written": False}


def inspect_stripes(pixels, expected, label):
    """4種類のalpha stripeが期待色か検査して、実測値を返す。"""
    actual = []
    for index, (x, y) in enumerate(STRIPE_SAMPLES):
        color = pixel(pixels, x, y)
        wanted = expected[index]
        if any(abs(color[channel] - wanted[channel]) > 2 for channel in range(3)):
            raise AssertionError(f"{label} stripe {index} at ({x},{y}): expected {wanted}, got {color}")
        actual.append(color)
    return actual


def inspect_marker(pixels, label):
    """UIの緑マーカーがScene画像と重ならず表示されることを確認する。"""
    sample = pixel(pixels, 540, 50)
    if any(abs(sample[channel] - GREEN[channel]) > 2 for channel in range(3)):
        raise AssertionError(f"{label}: UI marker expected {GREEN}, got {sample}")
    changed = 0
    for y in range(32, 80):
        for x in range(520, 584):
            if pixel(pixels, x, y) != GREEN:
                changed += 1
    if changed:
        raise AssertionError(f"{label}: UI marker has {changed} non-green pixels")
    return {"sample": sample, "non_green_pixels": changed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--fixture-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--shader", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    fixture_dir = args.fixture_dir.resolve()
    output_dir = args.output_dir.resolve()
    shader = args.shader.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    if not shader.is_file():
        raise FileNotFoundError(shader)

    results = {"dimensions": [WIDTH, HEIGHT], "background_rgb": BACKGROUND, "scene": {}, "ui": {}, "depth": {}}
    for model_name, expected in SCENE_CASES.items():
        pixels = capture(executable, fixture_dir, output_dir, model_name, "scene")
        actual = inspect_stripes(pixels, expected, f"scene/{model_name}")
        inspect_marker(pixels, f"scene/{model_name}")
        background = pixel(pixels, 80, 400)
        if any(abs(background[channel] - BACKGROUND[channel]) > 2 for channel in range(3)):
            raise AssertionError(f"scene/{model_name}: background expected {BACKGROUND}, got {background}")
        results["scene"][model_name] = {"expected_stripes": expected, "actual_stripes": actual, "background": background}

    for model_name in UI_CASES:
        pixels = capture(executable, fixture_dir, output_dir, model_name, "ui")
        actual = inspect_stripes(pixels, SCENE_CASES[model_name], f"ui/{model_name}")
        marker = inspect_marker(pixels, f"ui/{model_name}")
        background = pixel(pixels, 80, 400)
        if any(abs(background[channel] - BACKGROUND[channel]) > 2 for channel in range(3)):
            raise AssertionError(f"ui/{model_name}: background expected {BACKGROUND}, got {background}")
        results["ui"][model_name] = {"expected_stripes": SCENE_CASES[model_name], "actual_stripes": actual, "marker": marker, "background": background}

    depth_pixels = capture(executable, fixture_dir, output_dir, "mask-default", "depth")
    depth_expected = (BLUE, BLUE, RED, RED)
    depth_actual = inspect_stripes(depth_pixels, depth_expected, "depth/mask-default")
    depth_marker = inspect_marker(depth_pixels, "depth/mask-default")
    results["depth"]["mask-default"] = {"expected_stripes": depth_expected, "actual_stripes": depth_actual, "marker": depth_marker}
    results["custom_shader_rejection"] = validate_custom_rejection(executable, fixture_dir, output_dir, shader)

    result_path = output_dir / "results.json"
    result_path.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Model alpha capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
