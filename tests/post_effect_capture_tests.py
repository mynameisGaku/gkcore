"""実GPU画像からポスト効果の方向とUI分離、フレーム設定の保持を検査する。"""
import argparse
import json
import os
from pathlib import Path
import subprocess


WIDTH = 640
HEIGHT = 480
MODES = (
    "baseline",
    "exposure_low",
    "exposure_high",
    "tone_off",
    "saturation_zero",
    "saturation_high",
    "contrast_zero",
    "contrast_high",
    "bloom_zero",
    "bloom_high",
    "fxaa_on",
)


def read_image(path):
    """P6画像の形式、寸法、全画素の長さを検査する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    assert magic == b"P6" and maximum == b"255", f"unexpected capture format: {path}"
    width, height = map(int, dimensions.split())
    assert (width, height) == (WIDTH, HEIGHT), (path, width, height)
    assert len(pixels) == WIDTH * HEIGHT * 3, f"truncated capture: {path}"
    return pixels


def pixel(pixels, x, y):
    """画像から1画素のRGB値を読む。"""
    offset = (y * WIDTH + x) * 3
    return tuple(pixels[offset:offset + 3])


def region(pixels, bounds):
    """左上を含み右下を含まない領域の画素を列挙する。"""
    x0, y0, x1, y1 = bounds
    return [pixel(pixels, x, y) for y in range(y0, y1) for x in range(x0, x1)]


def mean_rgb(pixels, bounds):
    """領域の平均RGBを整数で返す。"""
    samples = region(pixels, bounds)
    return tuple(round(sum(sample[channel] for sample in samples) / len(samples)) for channel in range(3))


def inspect(pixels, name):
    """SceneとUIの代表画素を、明るさ条件を適用せず記録する。"""
    orange = pixel(pixels, 80, 80)
    gray = pixel(pixels, 220, 80)
    triangle = pixel(pixels, 320, 240)
    bloom_patch = pixel(pixels, 490, 170)
    ui = pixel(pixels, 520, 64)
    text_white = sum(min(value) > 220 for value in region(pixels, (28, 408, 250, 450)))
    assert ui[1] > 230 and ui[0] < 20 and ui[2] < 20, (name, "invalid green UI rectangle", ui)
    assert text_white > 50, (name, "missing Japanese UI text", text_white)
    return {
        "orange": orange,
        "gray": gray,
        "triangle": triangle,
        "bloom_patch": bloom_patch,
        "ui": ui,
        "ui_rect": mean_rgb(pixels, (480, 32, 576, 96)),
        "text_and_backing": mean_rgb(pixels, (16, 400, 272, 464)),
        "text_white_pixels": text_white,
    }


def remove_capture_files(capture, count):
    """過去の取得画像が今回の成功扱いに混ざらないよう消す。"""
    for path in [capture] + [Path(str(capture) + f".frame{frame}.ppm") for frame in range(1, count + 1)]:
        if path.exists():
            path.unlink()


def capture(executable, output, mode, count=1):
    """指定modeを実行し、今回生成された画像だけを読み込む。"""
    path = output / (mode + ".ppm")
    remove_capture_files(path, count)
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(path)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = str(count)
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = "0"
    subprocess.run([str(executable), mode], env=environment, check=True, timeout=120)
    paths = [path] + [Path(str(path) + f".frame{frame}.ppm") for frame in range(1, count)]
    images = [read_image(image_path) for image_path in paths]
    assert all(image_path.exists() for image_path in paths), (mode, "missing requested capture")
    assert not Path(str(path) + f".frame{count}.ppm").exists(), (mode, "capture exceeded requested count")
    return images


def changed_pixels(first, second, bounds, threshold=10):
    """指定領域でRGB差が閾値を超えた画素数を数える。"""
    x0, y0, x1, y1 = bounds
    return sum(max(abs(a - b) for a, b in zip(pixel(first, x, y), pixel(second, x, y))) > threshold for y in range(y0, y1) for x in range(x0, x1))


def mean_luma(rgb):
    """平均RGBから比較用の単純な明るさ値を求める。"""
    return sum(rgb) / 3.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    images = {name: capture(executable, output, name)[0] for name in MODES}
    results = {name: inspect(images[name], name) for name in MODES}
    baseline = images["baseline"]
    baseline_scene = results["baseline"]
    assert baseline_scene["orange"][0] > baseline_scene["orange"][1] + 12 and baseline_scene["orange"][1] > baseline_scene["orange"][2] + 8, ("baseline orange Scene patch", baseline_scene["orange"])
    assert max(baseline_scene["gray"]) - min(baseline_scene["gray"]) <= 3, ("baseline gray Scene patch is not neutral", baseline_scene["gray"])
    assert min(baseline_scene["triangle"]) > 180, ("baseline white 3D triangle is missing", baseline_scene["triangle"])
    assert min(baseline_scene["bloom_patch"]) > 180, ("baseline white Bloom patch is missing", baseline_scene["bloom_patch"])

    low = results["exposure_low"]["orange"]
    base = results["baseline"]["orange"]
    high = results["exposure_high"]["orange"]
    assert all(low[channel] + 10 < base[channel] < high[channel] - 10 for channel in range(3)), ("exposure ordering", low, base, high)

    tone_changes = changed_pixels(baseline, images["tone_off"], (32, 32, 276, 128), 5)
    assert tone_changes > 100, ("tone mapping toggle did not change the Scene patches", tone_changes)
    tone_white = results["tone_off"]["triangle"]
    assert min(tone_white) > 250 and min(tone_white) > min(results["baseline"]["triangle"]) + 15, ("disabled tone mapping did not preserve display white", tone_white)
    zero_sat = results["saturation_zero"]["orange"]
    assert max(zero_sat) - min(zero_sat) <= 3, ("saturation zero left color spread", zero_sat)
    base_spread = max(base) - min(base)
    high_sat = results["saturation_high"]["orange"]
    assert max(high_sat) - min(high_sat) > base_spread + 5, ("high saturation did not widen color spread", base, high_sat)

    flat_orange = results["contrast_zero"]["orange"]
    flat_gray = results["contrast_zero"]["gray"]
    assert max(flat_orange) - min(flat_orange) <= 3 and max(flat_gray) - min(flat_gray) <= 3, ("zero contrast did not neutralize Scene patches", flat_orange, flat_gray)
    assert abs(mean_luma(flat_orange) - mean_luma(flat_gray)) <= 5, ("zero contrast did not bring patches to a shared midpoint", flat_orange, flat_gray)
    assert all(155 <= value <= 210 for value in flat_orange + flat_gray), ("zero contrast midpoint outside broad display range", flat_orange, flat_gray)
    flat_triangle = results["contrast_zero"]["triangle"]
    assert max(flat_triangle) - min(flat_triangle) <= 3 and all(175 <= value <= 200 for value in flat_triangle), ("zero contrast did not move the white patch to the shared midpoint", flat_triangle)
    baseline_gap = mean_luma(results["baseline"]["triangle"]) - mean_luma(results["baseline"]["gray"])
    high_gap = mean_luma(results["contrast_high"]["triangle"]) - mean_luma(results["contrast_high"]["gray"])
    assert high_gap > baseline_gap + 20, ("high contrast did not expand bright-dark separation", baseline_gap, high_gap)

    assert images["bloom_zero"] == baseline, "zero-intensity Bloom changed the baseline image"
    halo_bounds = (470, 150, 522, 202)
    halo_pixels = 0
    halo_delta = 0
    for y in range(halo_bounds[1], halo_bounds[3]):
        for x in range(halo_bounds[0], halo_bounds[2]):
            if 480 <= x < 512 and 160 <= y < 192:
                continue
            before = pixel(baseline, x, y)
            after = pixel(images["bloom_high"], x, y)
            delta = max(after[channel] - before[channel] for channel in range(3))
            halo_delta = max(halo_delta, delta)
            halo_pixels += delta > 2
    assert halo_pixels > 4 and halo_delta > 3, ("high Bloom did not produce an outside halo", halo_pixels, halo_delta)

    fxaa_changes = changed_pixels(baseline, images["fxaa_on"], (250, 150, 390, 310), 5)
    assert fxaa_changes > 10, ("FXAA did not alter diagonal-edge region", fxaa_changes)
    assert results["fxaa_on"]["triangle"] == results["baseline"]["triangle"], ("FXAA changed flat triangle interior", results["baseline"]["triangle"], results["fxaa_on"]["triangle"])

    ui_bounds = ((480, 32, 576, 96), (16, 400, 272, 464))
    for name, image in images.items():
        for bounds in ui_bounds:
            assert region(image, bounds) == region(baseline, bounds), (name, "post effect changed UI pixels", bounds)

    snapshot_images = capture(executable, output, "snapshot", 3)
    snapshot_expected = (baseline, images["exposure_high"], images["exposure_low"])
    snapshot_results = []
    for frame, (observed, expected) in enumerate(zip(snapshot_images, snapshot_expected)):
        assert observed == expected, ("settings were not captured at BeginFrame", frame, pixel(observed, 80, 80), pixel(expected, 80, 80))
        snapshot_results.append(inspect(observed, f"snapshot frame {frame}"))
    results["tone_off"]["changed_scene_pixels"] = tone_changes
    results["fxaa_on"]["changed_diagonal_pixels"] = fxaa_changes
    results["bloom_high"]["halo_changed_pixels"] = halo_pixels
    results["bloom_high"]["halo_max_delta"] = halo_delta
    results["snapshot"] = snapshot_results

    result_path = output / "results.json"
    result_path.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
