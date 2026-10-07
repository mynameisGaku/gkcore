"""多数の描画を続けた後のGPU画像を独立した画素期待値で検査する。"""
import argparse
import os
from pathlib import Path
import subprocess


def read_image(path):
    """取得画像が640x480の完全なP6データであることを確認する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    assert magic == b"P6" and maximum == b"255", "unexpected capture format"
    width, height = map(int, dimensions.split())
    assert (width, height) == (640, 480), (width, height)
    assert len(pixels) == width * height * 3, "truncated capture"
    return pixels


def pixel(pixels, x, y):
    """640幅の画像から指定座標のRGBを返す。"""
    offset = (y * 640 + x) * 3
    return tuple(pixels[offset:offset + 3])


def inspect(pixels, name, expected_scene):
    """Scene、UI、3D、文字の画素を個別の閾値で検査する。"""
    scene = pixel(pixels, 80, 80)
    ui = pixel(pixels, 520, 64)
    triangle = pixel(pixels, 320, 240)
    assert scene[expected_scene] > 220, (name, "incorrect Scene color", scene)
    assert all(scene[channel] < 30 for channel in range(3) if channel != expected_scene), (name, "Scene color contamination", scene)
    assert ui[1] > 230 and ui[0] < 20 and ui[2] < 20, (name, "invalid green UI rectangle", ui)
    assert triangle[1] > 190 and triangle[2] > 190 and triangle[0] < 40, (name, "missing cyan 3D triangle", triangle)
    white = sum(min(pixel(pixels, x, y)) > 220 for y in range(408, 450) for x in range(28, 220))
    assert white > 50, (name, "missing Japanese text", white)
    return {"scene": scene, "ui": ui, "triangle": triangle, "text_pixels": white}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    capture = output / "stress.ppm"
    extra_capture = Path(str(capture) + ".frame1.ppm")
    for path in (capture, extra_capture):
        if path.exists():
            path.unlink()
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(capture)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = "2"
    environment["GKCORE_TEST_CAPTURE_START_FRAME"] = "119"
    subprocess.run([str(args.executable.resolve())], env=environment, check=True, timeout=120)
    results = {
        "frame119": inspect(read_image(capture), "frame 119", 2),
        "frame120": inspect(read_image(extra_capture), "frame 120", 0),
    }
    print(results)


if __name__ == "__main__":
    main()
