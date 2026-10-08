#!/usr/bin/env python3
"""アニメーション姿勢を独立した静的GLB画像と比較する。"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from support.model_animation_capture_fixtures import main as generate_fixtures


WIDTH = 640
HEIGHT = 480
BACKGROUND = (40, 80, 120)
GREEN = (0, 255, 0)


def read_ppm(path):
    """P6画像の寸法と画素長を検証する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise ValueError(f"{path}: capture must be P6 RGB")
    width, height = map(int, dimensions.split())
    if (width, height) != (WIDTH, HEIGHT) or len(pixels) != WIDTH * HEIGHT * 3:
        raise ValueError(f"{path}: unexpected capture dimensions or payload")
    return pixels


def pixel(pixels, x, y):
    """画像の指定位置からRGBを読む。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def capture(executable, fixture_dir, output_dir, model_name, mode, animation=None, frames=1, start_frame=0):
    """指定fixtureを1回描画して今回のcaptureを返す。"""
    model_path = Path(model_name)
    if not model_path.is_absolute():
        model_path = fixture_dir / model_path
    if not model_path.suffix:
        model_path = model_path.with_suffix(".glb")
    path = output_dir / f"{mode}-{model_path.stem}.ppm"
    path.unlink(missing_ok=True)
    for stale in output_dir.glob(path.name + ".frame*.ppm"):
        stale.unlink()
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(path)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = str(frames)
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = str(start_frame)
    command = [str(executable), str(model_path), mode]
    if animation:
        animation_path = Path(animation)
        if not animation_path.is_absolute():
            animation_path = fixture_dir / animation_path
        if not animation_path.suffix:
            animation_path = animation_path.with_suffix(".glb")
        command.append(str(animation_path))
    completed = subprocess.run(command, cwd=output_dir, env=environment, check=False,
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    if completed.returncode != 0:
        raise RuntimeError(f"{mode}/{model_name} failed ({completed.returncode})\n{completed.stdout}\n{completed.stderr}")
    extra_frames = tuple(output_dir.glob(path.name + ".frame*.ppm"))
    expected_paths = tuple(Path(str(path) + f".frame{index}.ppm") for index in range(1, frames))
    if not path.is_file() or set(extra_frames) != set(expected_paths):
        raise RuntimeError(f"{mode}/{model_name} did not produce exactly {frames} captures")
    images = (read_ppm(path),) + tuple(read_ppm(frame_path) for frame_path in expected_paths)
    return images[0] if frames == 1 else images


def compare_images(actual, reference, label, require_red=True):
    """全画面の差を調べ、モデルとUIが実際に描かれたことも確認する。"""
    maximum = 0
    changed = 0
    model_pixels = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 3
            difference = max(abs(actual[offset + channel] - reference[offset + channel]) for channel in range(3))
            maximum = max(maximum, difference)
            changed += difference > 2
            color = tuple(actual[offset:offset + 3])
            in_marker = 520 <= x < 584 and 32 <= y < 80
            is_model_color = color[0] > 140 and color[0] > color[1] * 2 if require_red else any(abs(color[channel] - BACKGROUND[channel]) > 2 for channel in range(3))
            if not in_marker and is_model_color:
                model_pixels += 1
    if maximum > 2 or changed:
        raise AssertionError(f"{label}: expected static-pose match within 2 RGB levels; max={maximum}, changed={changed}")
    if model_pixels < 120:
        raise AssertionError(f"{label}: capture contains only {model_pixels} model pixels")
    background = pixel(actual, 80, 400)
    marker = pixel(actual, 540, 50)
    if any(abs(background[channel] - BACKGROUND[channel]) > 2 for channel in range(3)):
        raise AssertionError(f"{label}: scene background changed to {background}")
    if any(abs(marker[channel] - GREEN[channel]) > 2 for channel in range(3)):
        raise AssertionError(f"{label}: UI marker changed to {marker}")
    return {"max_channel_delta": maximum, "changed_pixels": changed, "model_pixels": model_pixels}


def require_pose_change(actual, rest, label):
    """UI markerを除く画像で、動作前後に十分な画素差があるか調べる。"""
    changed = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if 520 <= x < 584 and 32 <= y < 80:
                continue
            offset = (y * WIDTH + x) * 3
            if max(abs(actual[offset + channel] - rest[offset + channel]) for channel in range(3)) > 8:
                changed += 1
    if changed <= 50:
        raise AssertionError(f"{label}: animated pose changed only {changed} scene pixels")
    return changed


def main():
    """animated fixtureと静的参照のcaptureを比較する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--fixture-dir", required=True, type=Path)
    parser.add_argument("--fbx-fixture-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    fixture_dir = args.fixture_dir.resolve()
    fbx_fixture_dir = args.fbx_fixture_dir.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    generate_fixtures(["--output-dir", str(fixture_dir)])
    result_path = output_dir / "results.json"
    result_path.unlink(missing_ok=True)
    results = {}

    def save_results():
        """成功したcaseまでの結果を途中失敗時も残す。"""
        result_path.write_text(json.dumps(results, indent=2), encoding="utf-8")

    pairs = [
        ("translation-actual", "translation", "translation-reference", "static-x025"),
        ("blend-actual", "blend", "blend-reference", "static-blend"),
        ("skin-morph-actual", "skin-morph", "skin-morph-reference", "static"),
        ("rest-morph-actual", "rest-morph", "rest-morph-reference", "static"),
        ("ik-actual", "ik", "ik-reference", "static"),
        ("translation-actual", "external", "translation-reference", "static-x025", "translation-actual"),
        ("translation-actual", "snapshot", "translation-reference", "snapshot-reference"),
    ]
    for pair in pairs:
        actual_name, actual_mode, reference_name, reference_mode = pair[:4]
        animation_name = pair[4] if len(pair) == 5 else None
        actual = capture(executable, fixture_dir, output_dir, actual_name, actual_mode, animation_name)
        reference = capture(executable, fixture_dir, output_dir, reference_name, reference_mode)
        result = compare_images(actual, reference, actual_mode)
        if actual_mode == "snapshot":
            rest = capture(executable, fixture_dir, output_dir, actual_name, "snapshot-rest")
        elif actual_mode == "rest-morph":
            rest = capture(executable, fixture_dir, output_dir, "rest-morph-zero", "rest")
        else:
            rest = capture(executable, fixture_dir, output_dir, actual_name, "rest")
        result["changed_from_rest_pixels"] = require_pose_change(actual, rest, actual_mode)
        results[actual_mode] = result
        save_results()
    stress_frames = capture(executable, fixture_dir, output_dir, "translation-actual", "sequence-stress", frames=2, start_frame=130)
    stress_first_reference = capture(executable, fixture_dir, output_dir, "translation-reference", "static")
    stress_second_reference = capture(executable, fixture_dir, output_dir, "translation-reference", "static-x025")
    stress_first = compare_images(stress_frames[0], stress_first_reference, "sequence-stress/frame130")
    save_results()
    stress_second = compare_images(stress_frames[1], stress_second_reference, "sequence-stress/frame131")
    stress_change = require_pose_change(stress_frames[0], stress_frames[1], "sequence-stress")
    results["sequence-stress"] = {"frame130": stress_first, "frame131": stress_second, "changed_between_frames": stress_change}
    save_results()
    sequence_actual = capture(executable, fixture_dir, output_dir, "sequence-0.obj", "obj-sequence", "sequence-1.obj")
    sequence_reference = capture(executable, fixture_dir, output_dir, "sequence-0.obj", "obj-reference")
    sequence_result = compare_images(sequence_actual, sequence_reference, "obj-sequence", require_red=False)
    sequence_rest = capture(executable, fixture_dir, output_dir, "sequence-0.obj", "rest")
    sequence_result["changed_from_rest_pixels"] = require_pose_change(sequence_actual, sequence_rest, "obj-sequence")
    results["obj-sequence"] = sequence_result
    save_results()

    fbx_pairs = [
        ("fbx-animation-node-translation.fbx", "fbx-node", "fbx-animation-node-translation.fbx", "fbx-node-reference"),
        ("fbx-animation-skin-translation.fbx", "fbx-skin", "fbx-animation-skin-translation.fbx", "fbx-skin-reference"),
        ("fbx-animation-morph-weight.fbx", "fbx-morph", "fbx-animation-morph-weight.fbx", "fbx-morph-reference"),
        ("fbx-animation-morph-default-weight.fbx", "fbx-rest-morph", "fbx-animation-morph-weight.fbx", "fbx-rest-morph-reference"),
    ]
    for actual_name, actual_mode, reference_name, reference_mode in fbx_pairs:
        actual = capture(executable, fbx_fixture_dir, output_dir, actual_name, actual_mode)
        reference = capture(executable, fbx_fixture_dir, output_dir, reference_name, reference_mode)
        result = compare_images(actual, reference, actual_mode, require_red=False)
        if actual_mode == "fbx-rest-morph":
            rest_name = "fbx-animation-morph-weight.fbx"
            rest_mode = "fbx-morph-rest"
        else:
            rest_name = actual_name
            rest_mode = actual_mode + "-rest"
        rest = capture(executable, fbx_fixture_dir, output_dir, rest_name, rest_mode)
        result["changed_from_rest_pixels"] = require_pose_change(actual, rest, actual_mode)
        results[actual_mode] = result
        save_results()
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Model animation capture validation failed: {error}", file=sys.stderr)
        sys.exit(1)
