"""実GPUの描画画像を色・位置・効果の独立した期待値で検査する。"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import zlib


def write_image(path):
    """画像描画の色を判定できる不透明なマゼンタ画像を作る。"""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = (b"\0" + bytes((255, 0, 255, 255)) * 64) * 64
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 64, 64, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def read_image(path):
    """GPUから取得したP6画像の寸法と画素数を検査する。"""
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    assert magic == b"P6" and maximum == b"255", "unexpected capture format"
    width, height = map(int, dimensions.split())
    assert (width, height) == (640, 480), (width, height)
    assert len(pixels) == width * height * 3, "truncated capture"
    return pixels


def pixel(pixels, x, y):
    """640幅の画像から指定座標のRGBを取り出す。"""
    offset = (y * 640 + x) * 3
    return tuple(pixels[offset:offset + 3])


def inspect(pixels, name):
    """描画コードの式を再利用せず、画面に必要な色と文字を判定する。"""
    scene = pixel(pixels, 80, 80)
    ui = pixel(pixels, 520, 64)
    triangle = pixel(pixels, 320, 240)
    image = pixel(pixels, 512, 182)
    background = pixel(pixels, 8, 8)
    assert scene[0] > 100 and scene[0] > scene[1] + 70 and scene[0] > scene[2] + 70, (name, "missing red Scene rectangle", scene)
    assert ui[1] > 230 and ui[0] < 20 and ui[2] < 20, (name, "invalid green UI rectangle", ui)
    assert triangle[2] > 170 and triangle[2] > triangle[0] + 100 and triangle[2] > triangle[1] + 100, (name, "missing blue 3D triangle", triangle)
    assert image[0] > 100 and image[2] > 170 and image[1] < 40, (name, "invalid magenta image", image)
    assert max(background) < 120, (name, "invalid background", background)
    white = sum(min(pixel(pixels, x, y)) > 220 for y in range(408, 450) for x in range(28, 170))
    assert white > 50, (name, "missing Japanese text", white)
    return {"scene": scene, "ui": ui, "triangle": triangle, "image": image, "background": background, "text_pixels": white}


def region_pixels(pixels, x0, y0, x1, y1):
    """球の内部とその照明変化を調べる領域の画素を列挙する。"""
    return [pixel(pixels, x, y) for y in range(y0, y1) for x in range(x0, x1)]


def inspect_model(pixels, name):
    """モデル照明画像から左右の材質色が球面に現れた画素数を数える。"""
    dielectric = region_pixels(pixels, 150, 165, 300, 315)
    metallic = region_pixels(pixels, 345, 165, 495, 315)
    warm_body = sum(r > 35 and r > g * 1.2 and r > b * 1.5 for r, g, b in dielectric)
    cool_body = sum(b > 35 and b > r * 1.3 and b > g * 1.1 for r, g, b in metallic)
    assert warm_body > 800, (name, "missing warm dielectric sphere body", warm_body)
    assert cool_body > 800, (name, "missing cool metallic sphere body", cool_body)
    return {"warm_body_pixels": warm_body, "cool_body_pixels": cool_body}


def changed_pixels(first, second, bounds):
    """同じ画面領域で照明方向により変化した画素数を数える。"""
    x0, y0, x1, y1 = bounds
    return sum(
        max(abs(a - b) for a, b in zip(pixel(first, x, y), pixel(second, x, y))) > 8
        for y in range(y0, y1)
        for x in range(x0, x1)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output / "magenta.png"
    write_image(fixture)
    shader = args.source_dir.resolve() / "tests/assets/shaders/post_effect_tint.frag"
    results = {}
    for name in ("default", "direct", "tint"):
        capture = output / (name + ".ppm")
        extra_capture = Path(str(capture) + ".frame1.ppm")
        for path in (capture, extra_capture):
            if path.exists():
                path.unlink()
        environment = os.environ.copy()
        environment["GKCORE_TEST_CAPTURE_PATH"] = str(capture)
        if name == "default":
            environment.pop("GKCORE_TEST_CAPTURE_FRAMES", None)
        else:
            environment["GKCORE_TEST_CAPTURE_FRAMES"] = "1"
        subprocess.run([str(args.executable.resolve()), name, str(fixture), str(shader)], env=environment, check=True, timeout=45)
        results[name] = inspect(read_image(capture), name)
        assert not extra_capture.exists(), (name, "single capture continued beyond its limit")
    assert results["default"]["scene"][0] - results["tint"]["scene"][0] > 35, "custom post effect did not change Scene color"
    assert results["default"]["ui"] == results["tint"]["ui"], "custom post effect changed UI color"
    # 各Presentで取得し、同じアプリ内の有効・無効切り替えを独立して判定する。
    capture = output / "sequence.ppm"
    sequence_paths = [capture] + [Path(str(capture) + f".frame{frame}.ppm") for frame in range(1, 6)]
    for path in sequence_paths:
        if path.exists():
            path.unlink()
    environment = os.environ.copy()
    environment["GKCORE_TEST_CAPTURE_PATH"] = str(capture)
    environment["GKCORE_TEST_CAPTURE_FRAMES"] = "6"
    subprocess.run([str(args.executable.resolve()), "sequence", str(fixture), str(shader)], env=environment, check=True, timeout=45)
    results["sequence"] = []
    for frame, path in enumerate(sequence_paths):
        observed = inspect(read_image(path), f"sequence frame {frame}")
        expected = results["tint" if frame % 2 else "default"]
        assert abs(observed["scene"][0] - expected["scene"][0]) <= 2, (frame, "post effect did not follow frame setting", observed["scene"])
        assert observed["ui"] == results["default"]["ui"], (frame, "UI changed during effect switching", observed["ui"])
        results["sequence"].append(observed)
    # 誤った取得回数は描画テストを成功扱いせず、診断付きで失敗させる。
    for value in ("0", "17", "invalid"):
        invalid_capture = output / "invalid-count.ppm"
        if invalid_capture.exists():
            invalid_capture.unlink()
        environment = os.environ.copy()
        environment["GKCORE_TEST_CAPTURE_PATH"] = str(invalid_capture)
        environment["GKCORE_TEST_CAPTURE_FRAMES"] = value
        rejected = subprocess.run([str(args.executable.resolve()), "default", str(fixture), str(shader)], env=environment, capture_output=True, timeout=45)
        assert rejected.returncode == 1, (value, "invalid capture count was accepted", rejected.returncode)
        assert b"GKCORE_TEST_CAPTURE_FRAMES" in rejected.stderr, (value, "missing capture count diagnostic")
        assert not invalid_capture.exists(), (value, "invalid count created an image")
    model = args.source_dir.resolve() / "examples/assets/model_lighting.glb"
    model_images = {}
    for name in ("model", "model_rotated"):
        capture = output / (name + ".ppm")
        if capture.exists():
            capture.unlink()
        environment = os.environ.copy()
        environment["GKCORE_TEST_CAPTURE_PATH"] = str(capture)
        environment["GKCORE_TEST_CAPTURE_FRAMES"] = "1"
        subprocess.run([str(args.executable.resolve()), name, str(model), str(shader)], env=environment, check=True, timeout=45)
        model_images[name] = read_image(capture)
        results[name] = inspect_model(model_images[name], name)
    for bounds, side in (((150, 165, 300, 315), "dielectric"), ((345, 165, 495, 315), "metallic")):
        changed = changed_pixels(model_images["model"], model_images["model_rotated"], bounds)
        assert changed > 300, (side, "directional light rotation did not change sphere shading", changed)
        results["model"][side + "_light_changed_pixels"] = changed
    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
