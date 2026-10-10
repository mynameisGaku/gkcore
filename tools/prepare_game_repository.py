#!/usr/bin/env python3
"""開発checkoutからゲーム構築用のソース一式を生成する。"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import sys


ROOT_FILES = ("CMakeLists.txt", "CMakePresets.json", "PRE_SETUP.bat", "README.md", ".gitignore", ".gitattributes")
CODE_DIRECTORIES = ("include", "src", "shaders")
THIRD_PARTY_FILES = (
    "third_party/cgltf/cgltf.h",
    "third_party/cgltf/LICENSE",
    "third_party/stb/stb_image.h",
    "third_party/stb/LICENSE",
    "third_party/mikktspace/mikktspace.c",
    "third_party/mikktspace/mikktspace.h",
    "third_party/mikktspace/LICENSE",
    "third_party/ufbx/ufbx.c",
    "third_party/ufbx/ufbx.h",
    "third_party/ufbx/LICENSE",
)
BUILD_FILES = (
    "tools/setup.py",
    "tools/build_forge.py",
    "tools/build_gkcore_shaders.py",
    "tools/forge_checkout.py",
    "tools/verify_dxc.py",
    "tools/fetch_forge.py",
    "tools/fetch_dxc.py",
    "tools/compile_pixel_shader.py",
)
EXAMPLE_FILES = (
    "examples/mixed_scene.cpp",
    "examples/rectangle_outline.cpp",
    "examples/custom_post_effect.cpp",
    "examples/model_lighting.cpp",
    "examples/model_viewer.cpp",
    "examples/support/FHumanoidMapOptions.h",
    "examples/support/FHumanoidMapOptions.cpp",
    "examples/support/ModelMappingReport.h",
    "examples/support/ModelMappingReport.cpp",
    "examples/support/FModelArmIkPreview.h",
    "examples/support/FModelArmIkPreview.cpp",
    "examples/support/FModelMotionView.h",
    "examples/support/FModelMotionView.cpp",
    "examples/assets/model_lighting.glb",
    "examples/shaders/tint.hlsl",
    "examples/shaders/post_effect_tint.hlsl",
)
PRODUCT_DOCS = (
    "docs/quickstart.md",
    "docs/input.md",
    "docs/images.md",
    "docs/models.md",
    "docs/lighting.md",
    "docs/rectangles.md",
    "docs/effects.md",
    "docs/custom-shader.md",
    "docs/post-effect-shader.md",
    "docs/model-animation.md",
    "docs/humanoid-bone-map.md",
    "docs/package-layout.md",
    "docs/THIRD_PARTY_NOTICES.md",
)
GAME_GITIGNORE = """/build/
/.devtools/
/.vs/
*.user
*.suo
*.vcxproj.user
*.o
__pycache__/
*.py[cod]
"""
GAME_GITATTRIBUTES = """# Runtimeとゲーム例の自作ソースをCRLFで保つ。
third_party/** -text -whitespace
*.bat text eol=crlf
/include/** text eol=crlf
/src/** text eol=crlf
/shaders/** text eol=crlf
/examples/*.cpp text eol=crlf
/examples/shaders/*.hlsl text eol=crlf
/tools/*.py text eol=crlf
/CMakeLists.txt text eol=crlf
/cmake/*.cmake.in text eol=crlf
/examples/assets/*.glb binary
/cmake/gpu.cfg -text -whitespace
"""
DEVELOPMENT_DOCS = {
    "roadmap.md", "tdd_log.md", "render-validation.md", "development.md",
    "model-performance.md", "model-viewer.md", "project-layout.md",
}
DEVELOPMENT_EVIDENCE = re.compile(
    r"(?i)(?:\bctest\b|\bCPU[- ]only\b|\bCPU tests?\b|\bGPU smoke\b|"
    r"\bCOM reflection\b|\breflection tests?\b|"
    r"\b(?:image|GPU image|shader|integration|package|render) tests?\b|"
    r"\btests?\s+(?:results?|passed|failed|were|run|cover|check)\b|"
    r"全CTest|CPUテスト|GPU画像テスト|shader画像テスト|画像検査|検査結果|検証結果|"
    r"テスト(?:では|で確認|結果|用|を実行)|確認テスト|GPU smoke|実GPU|実 GPU|"
    r"確認済み|確認しました|描画検証|TDD|ROADMAP|"
    r"再検査|byte単位|byte単位一致|全画素一致|旧実装|修正前|修正後|対照実行|回帰結果|"
    r"この検証|この検査|取得した\d+画像|実行時間|参照画像|"
    r"render-validation\.md|roadmap\.md|tdd_log\.md|development\.md|"
    r"model-performance\.md|project-layout\.md)")
PUBLIC_CONTRACT = re.compile(
    r"(?i)(?:gk::|NaN|infinity|non-finite|範囲外|有限|上限|"
    r"失敗します|拒否します|拒否され|受け付けません|使用できません|未対応|"
    r"エラー|戻り値|返します|返す|無効handle|無効値|解放後|削除後|適用されません|適用しません|"
    r"読み込みに失敗|読み込めません|できません|呼び出してください|必須です)")
DEVELOPMENT_CODE = re.compile(
    r"(?i)(?:\bctest\b|(?:^|[/\\])tests[/\\]|--gpu-check|"
    r"build/native-validation|gkcore_.*(?:_tests|capture_tests)|"
    r"test[_-](?:dir|filter|suite))")
DEVELOPMENT_HEADINGS = re.compile(
    r"(?i)^#{1,6}\s*(?:開発環境|開発用 checkout|配布検証|"
    r"実GPU.*確認|実GPU.*検査|検査結果|検証結果|画像検査結果|"
    r"再検査|実行結果|構成別build|Debug.*検証|Release.*検証)\s*$")
DEVELOPMENT_HISTORY = re.compile(r"(?i)(?:旧実装|修正前|修正後|対照実行|回帰結果|この検証|この検査)")
MARKDOWN_LINK = re.compile(r"(!?)\[([^\]]*)\]\((<[^>]+>|[^)\s]+)(?:\s+[^)]*)?\)")
GENERATED_README = """# gkcore

gkcore は Windows 向けの C++ 描画フレームワークです。ゲームからは `<gkcore.h>` と `gk::` API を使います。

ゲームの最小構成とフレームの流れは[クイックスタート](docs/quickstart.md)、画像・モデル・入力・描画効果の使い方は各ガイドを参照してください。

この checkout から Runtime とサンプルを構築するには、Windows 10/11 x64、Visual Studio 2022 または 2026 の C++ 開発環境、v142 14.29 toolset、Windows SDK 10.0.22621.0、対応する CMake、Python 3.9 以降が必要です。`PRE_SETUP.bat` が固定版 The Forge と DXC を取得し、Runtime とサンプルをビルドします。

The Forge、DXC、gkcore を含む third-party の配布条件は[通知一覧](docs/THIRD_PARTY_NOTICES.md)を確認してください。

- [クイックスタート](docs/quickstart.md)
- [キー入力](docs/input.md)
- [画像](docs/images.md)
- [モデル](docs/models.md)
- [モデル照明](docs/lighting.md)
- [ポストエフェクト](docs/effects.md)
- [カスタムシェーダー](docs/custom-shader.md)
- [Runtime SDK の構成](docs/package-layout.md)
"""


class ExportError(RuntimeError):
    """入力checkoutまたは生成先が安全なexport条件を満たさない。"""


def allowed_repository_files(source: Path | None = None) -> tuple[str, ...]:
    """ゲーム構築に使う固定 allowlist の相対パスを返す。"""
    source_root = source or Path(__file__).resolve().parents[1]
    selected = set(ROOT_FILES)
    selected.update(f"cmake/{name}" for name in (
        "gkcoreConfig.cmake.in", "forge-files-lock.json", "dxc-lock.json", "gpu.cfg"))
    selected.update(BUILD_FILES)
    selected.update(EXAMPLE_FILES)
    selected.update(PRODUCT_DOCS)
    for directory in CODE_DIRECTORIES:
        selected.update(path.relative_to(source_root).as_posix()
                        for path in (source_root / directory).rglob("*") if path.is_file())

    # ドキュメントから実際に参照されている図だけを含める。
    documents = [(relative, source_root / relative) for relative in PRODUCT_DOCS if relative.lower().endswith(".md")]
    for name in ("quickstart.md", "package-layout.md"):
        override = source_root / "cmake" / "game-repository-docs" / name
        if override.is_file():
            documents.append((f"docs/{name}", override))
    for output_relative, source_document in documents:
        document = source_root / output_relative
        reference_text = source_document
        if not reference_text.is_file():
            continue
        for target in re.findall(r"!?\[[^\]]*\]\(([^)]+)\)", reference_text.read_text(encoding="utf-8-sig")):
            target = target.split("#", 1)[0].split("?", 1)[0].strip().strip("<>")
            if not target or re.match(r"^[a-z][a-z0-9+.-]*:", target, re.IGNORECASE):
                continue
            candidate = (document.parent / target).resolve()
            try:
                candidate_relative = candidate.relative_to(source_root.resolve()).as_posix()
            except ValueError:
                continue
            if candidate.suffix.lower() in (".png", ".jpg", ".jpeg", ".svg", ".webp"):
                selected.add(candidate_relative)

    selected.update(THIRD_PARTY_FILES)
    return tuple(sorted(selected))


def _markdown_blocks(source: str) -> list[str]:
    """空行とfenceを基準にMarkdownのまとまりへ分ける。"""
    blocks: list[str] = []
    current: list[str] = []
    fence: str | None = None
    for line in source.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        marker = re.match(r"^\s*(```+|~~~+)", line)
        if marker:
            token = marker.group(1)[0]
            if fence is None:
                fence = token
            elif token == fence:
                fence = None
        if not line.strip() and fence is None:
            if current:
                blocks.append("\n".join(current))
                current = []
        else:
            current.append(line)
    if current:
        blocks.append("\n".join(current))
    return blocks


def _link_path(root: Path, document: Path, target: str) -> Path | None:
    """外部URLを除き、Markdownリンク先のローカルファイルを解決する。"""
    from urllib.parse import unquote

    value = target.strip().strip("<>").split("#", 1)[0].split("?", 1)[0]
    if not value or re.match(r"^[a-z][a-z0-9+.-]*:", value, re.IGNORECASE) or value.startswith("//"):
        return None
    return (root / document.parent / Path(unquote(value))).resolve()


def _clean_broken_links(block: str, root: Path, document: Path) -> str:
    """存在しない文書リンクは文字にし、存在しない画像だけを外す。"""
    def replace_link(match: re.Match) -> str:
        image, label, target = match.groups()
        path = _link_path(root, document, target)
        if path is None or path.is_file():
            return match.group(0)
        return "" if image else label

    return MARKDOWN_LINK.sub(replace_link, block)


def _is_development_code(block: str) -> bool:
    return block.lstrip().startswith(("```", "~~~")) and bool(DEVELOPMENT_CODE.search(block))


def _sanitize_document(source: str, root: Path, document: Path) -> str:
    """開発記録を落とし、ゲーム向けの説明・図・ローカルリンクを保つ。"""
    kept_blocks: list[str] = []
    for block in _markdown_blocks(source):
        if _is_development_code(block):
            continue
        if DEVELOPMENT_HEADINGS.match(block.strip()):
            continue
        # 図は説明文が検証記録でも残し、周辺の使い方だけを整理する。
        if re.fullmatch(r"\s*!\[[^\]]*\]\([^)]+\)\s*", block):
            kept_blocks.append(_clean_broken_links(block, root, document))
            continue

        retained_lines: list[str] = []
        for line in block.splitlines():
            if re.match(r"^\s*#{1,6}\s+", line) and DEVELOPMENT_HEADINGS.match(line.strip()):
                continue
            sentences = re.split(r"(?<=。)", line)
            for sentence in sentences:
                if DEVELOPMENT_HISTORY.search(sentence) or (
                        DEVELOPMENT_EVIDENCE.search(sentence) and not PUBLIC_CONTRACT.search(sentence)):
                    continue
                cleaned = _clean_broken_links(sentence, root, document).rstrip()
                if cleaned.strip():
                    retained_lines.append(cleaned)
        cleaned_block = "\n".join(retained_lines).strip()
        if cleaned_block:
            kept_blocks.append(cleaned_block)

    # 削除した検証記録の見出しが空で残らないようにする。
    pruned: list[str] = []
    for index, block in enumerate(kept_blocks):
        if re.match(r"^#{1,6}\s+", block.strip()):
            next_is_heading = index + 1 == len(kept_blocks) or bool(
                re.match(r"^#{1,6}\s+", kept_blocks[index + 1].strip()))
            if next_is_heading:
                continue
        pruned.append(block)
    result = "\n\n".join(pruned).strip()
    return result + "\n" if result else ""


def _write_text_file(path: Path, text: str, *, bom: bool = True) -> None:
    """生成したテキストをUTF-8 BOM付き・CRLFで保存する。"""
    normalized = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")
    path.write_bytes((b"\xef\xbb\xbf" if bom else b"") + normalized.encode("utf-8"))


def _normalize_authored_files(root: Path, selected: tuple[str, ...]) -> None:
    """ゲーム側で編集するテキストをUTF-8 BOMとCRLFへ揃える。"""
    extensions = {".cpp", ".h", ".hpp", ".hlsl", ".fsl", ".py", ".ps1", ".bat", ".md", ".in"}
    for relative in selected:
        path = Path(relative)
        if path.parts[0] == "third_party" or path.suffix.lower() not in extensions:
            continue
        target = root / path
        if not target.is_file():
            continue
        data = target.read_bytes()
        has_bom = data.startswith(b"\xef\xbb\xbf")
        text = data.decode("utf-8-sig")
        _write_text_file(target, text, bom=has_bom)


def find_broken_local_links(root: Path) -> list[str]:
    """生成文書から存在しないローカルMarkdownリンクを返す。"""
    errors: list[str] = []
    for document in root.rglob("*.md"):
        text = document.read_text(encoding="utf-8-sig")
        for line_number, line in enumerate(text.splitlines(), 1):
            for match in MARKDOWN_LINK.finditer(line):
                target = match.group(3)
                path = _link_path(root, document.relative_to(root), target)
                if path is not None and not path.is_file():
                    errors.append(f"{document.relative_to(root).as_posix()}:{line_number}: {target}")
    return errors


def _is_removed_if(line: str) -> bool:
    match = re.match(r"^\s*if\s*\((.*?)\)\s*$", line, re.IGNORECASE)
    if not match:
        return False
    condition = match.group(1)
    upper = condition.upper()
    if any(name in upper for name in (
            "GKCORE_BUILD_TESTS", "GKCORE_RUN_BACKEND_SMOKE", "GKCORE_RUN_INTERACTIVE_TESTS",
            "GKCORE_RENDER_PERFORMANCE_METRICS")):
        return True
    target = re.search(r"\bTARGET\s+([A-Za-z0-9_]+)", condition, re.IGNORECASE)
    if target:
        target_name = target.group(1).lower()
        return any(word in target_name for word in ("test", "capture", "benchmark", "smoke", "reflection"))
    return False


def _remove_if_blocks(lines: list[str]) -> list[str]:
    kept: list[str] = []
    depth = 0
    remove_depth = 0
    for line in lines:
        if re.match(r"^\s*if\s*\(", line, re.IGNORECASE):
            depth += 1
            if remove_depth == 0 and _is_removed_if(line):
                remove_depth = depth
            if remove_depth == 0:
                kept.append(line)
            continue
        if re.match(r"^\s*endif\b", line, re.IGNORECASE):
            if depth == 0:
                raise ExportError("CMakeLists.txt has an unmatched endif")
            was_removed = remove_depth != 0
            closes_removed_block = remove_depth == depth
            depth -= 1
            if not was_removed:
                kept.append(line)
            if closes_removed_block:
                remove_depth = 0
            continue
        if remove_depth == 0:
            kept.append(line)
    if depth != 0:
        raise ExportError("CMakeLists.txt has an unmatched if block")
    return kept


def _remove_benchmark_commands(lines: list[str]) -> list[str]:
    """benchmark targetを定義するCMake commandだけを丸ごと除く。"""
    kept: list[str] = []
    statement: list[str] = []
    balance = 0
    skipping = False
    for line in lines:
        if not statement and not re.match(r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\(", line):
            kept.append(line)
            continue
        statement.append(line)
        balance += line.count("(") - line.count(")")
        if len(statement) == 1:
            skipping = "gkcore_model_benchmark" in line.lower()
        if balance <= 0:
            if not skipping:
                kept.extend(statement)
            statement = []
            balance = 0
            skipping = False
    if statement:
        if skipping:
            return kept
        kept.extend(statement)
    return kept


def make_game_cmake(source: str) -> str:
    """テスト専用ブロックを除き、ゲーム向けRuntime buildを保つ。"""
    lines = _remove_benchmark_commands(_remove_if_blocks(source.splitlines()))
    lines = [line for line in lines if not re.match(
        r"^\s*option\(\s*(GKCORE_BUILD_TESTS|GKCORE_RUN_BACKEND_SMOKE|"
        r"GKCORE_RUN_INTERACTIVE_TESTS|GKCORE_RENDER_PERFORMANCE_METRICS)\b", line, re.IGNORECASE)]
    result = "\n".join(lines) + "\n"
    result = result.replace(
        "Linux and other platforms can build the development tests with GKCORE_BUILD_RUNTIME=OFF.",
        "Linux and other platforms cannot build the Windows Runtime.")
    # 固定したGPU選択設定は製品側の配置から読み込む。
    result = result.replace('"${GKCORE_FORGE_ROOT}/Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg"', '"${CMAKE_CURRENT_SOURCE_DIR}/cmake/gpu.cfg"')
    # 製品サンプルの shader は example HLSL から build 時に作る。
    old_shader = '"${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/shaders/post_effect_tint.frag"'
    if old_shader in result:
        result = result.replace(old_shader, '"${_gkcore_custom_post_shader}"')
        anchor = "    add_executable(gkcore_custom_post_effect examples/custom_post_effect.cpp)\n"
        insertion = (
            '    set(_gkcore_custom_post_shader "${CMAKE_CURRENT_BINARY_DIR}/post_effect_tint.frag")\n'
            '    add_custom_command(OUTPUT "${_gkcore_custom_post_shader}"\n'
            '        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_pixel_shader.py"\n'
            '            --dxc-root "${GKCORE_DXC_ROOT}"\n'
            '            --input "${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/post_effect_tint.hlsl"\n'
            '            --output "${_gkcore_custom_post_shader}"\n'
            '        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/post_effect_tint.hlsl"\n'
            '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_pixel_shader.py"\n'
            '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_dxc.py"\n'
            '            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dxc-lock.json"\n'
            '        VERBATIM)\n'
            '    add_custom_target(gkcore_custom_post_shader DEPENDS "${_gkcore_custom_post_shader}")\n'
            '    add_executable(gkcore_custom_post_effect examples/custom_post_effect.cpp)\n'
            '    add_dependencies(gkcore_custom_post_effect gkcore_custom_post_shader)\n'
        )
        if result.count(anchor) != 1:
            raise ExportError("Could not locate the custom post-effect sample in CMakeLists.txt")
        result = result.replace(anchor, insertion)

    forbidden = re.compile(
        r"(?i)(?:\btests?\b|(?:^|[/\\])tests[/\\]|capture_runtime|benchmark|"
        r"GKCORE_BUILD_TESTS|GKCORE_TEST|GKCORE_RUN_BACKEND_SMOKE|"
        r"GKCORE_RUN_INTERACTIVE_TESTS|GKCORE_RENDER_PERFORMANCE_METRICS|GKCORE_MODEL_BENCHMARK|"
        r"gkcore_model_benchmark|"
        r"shader_reflection|backend_smoke)")
    leftovers = [f"{number}: {line.strip()}" for number, line in enumerate(result.splitlines(), 1)
                 if forbidden.search(line)]
    if leftovers:
        raise ExportError("Generated CMake still refers to development-only content: " + "; ".join(leftovers))
    return result


def make_game_presets(source: dict) -> dict:
    """Visual Studio Runtime presetだけを残し、テスト設定を外す。"""
    configure = [item for item in source.get("configurePresets", [])
                 if item.get("name") == "runtime-windows"]
    build = [item for item in source.get("buildPresets", [])
             if item.get("name") == "runtime-windows"]
    if len(configure) != 1 or len(build) != 1:
        raise ExportError("CMakePresets.json must contain one runtime-windows configure and build preset")
    configure[0] = json.loads(json.dumps(configure[0]))
    variables = configure[0].setdefault("cacheVariables", {})
    for key in ("GKCORE_BUILD_TESTS", "GKCORE_RUN_BACKEND_SMOKE", "GKCORE_RUN_INTERACTIVE_TESTS",
                "GKCORE_RENDER_PERFORMANCE_METRICS"):
        variables.pop(key, None)
    return {"version": source["version"], "configurePresets": configure, "buildPresets": build}


def make_game_shader_builder(source: str) -> str:
    """FSL build helperから開発checkout専用のshader reflection検査を外す。"""
    source = source.replace("\r\n", "\n").replace("\r", "\n")
    function_pattern = re.compile(
        r"(?ms)^def reflection_command\(.*?(?=^def compile_shaders\()")
    result, function_count = function_pattern.subn("", source)
    if function_count != 1:
        raise ExportError("build_gkcore_shaders.py must contain one reflection_command helper")
    result, call_count = re.subn(
        r"(?m)^\s*subprocess\.run\(reflection_command\(python, dxc_root, output_root\), check=True, cwd=ROOT\)\s*\n",
        "", result)
    if call_count != 1:
        raise ExportError("build_gkcore_shaders.py must contain one shader reflection test invocation")
    if re.search(r"(?i)(?:^|[/\\])tests[/\\]", result):
        raise ExportError("Game shader builder still refers to the development tests directory")
    return result


def _copy_file(source_root: Path, destination_root: Path, relative: str) -> None:
    source = source_root / Path(relative)
    if not source.is_file():
        raise ExportError(f"Required game-building file is missing: {relative}")
    destination = destination_root / Path(relative)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def export_repository(source: Path, destination: Path) -> tuple[str, ...]:
    """allowlistから空のstaging directoryへゲーム構築checkoutを作る。"""
    source_root = source.resolve()
    destination_root = destination.resolve()
    if not (source_root / "CMakeLists.txt").is_file() or not (source_root / "tools" / "setup.py").is_file():
        raise ExportError(f"Not a gkcore development checkout: {source_root}")
    if source_root == destination_root or source_root in destination_root.parents:
        raise ExportError("Destination must be outside the source checkout")
    if destination_root.exists() and any(destination_root.iterdir()):
        raise ExportError(f"Destination must be empty: {destination_root}")
    destination_root.mkdir(parents=True, exist_ok=True)

    selected = allowed_repository_files(source_root)
    for relative in selected:
        if relative in ("CMakeLists.txt", "README.md", ".gitignore", ".gitattributes"):
            continue
        _copy_file(source_root, destination_root, relative)
    _normalize_authored_files(destination_root, selected)
    shader_builder_path = destination_root / "tools" / "build_gkcore_shaders.py"
    shader_builder_bytes = shader_builder_path.read_bytes()
    shader_builder_has_bom = shader_builder_bytes.startswith(b"\xef\xbb\xbf")
    shader_builder_text = make_game_shader_builder(shader_builder_bytes.decode("utf-8-sig"))
    shader_builder_encoded = shader_builder_text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n").encode("utf-8")
    shader_builder_path.write_bytes((b"\xef\xbb\xbf" if shader_builder_has_bom else b"") + shader_builder_encoded)
    _copy_file(source_root, destination_root, "CMakeLists.txt")

    cmake_path = destination_root / "CMakeLists.txt"
    source_bytes = cmake_path.read_bytes()
    has_bom = source_bytes.startswith(b"\xef\xbb\xbf")
    text = source_bytes.decode("utf-8-sig")
    generated = make_game_cmake(text)
    newline = "\r\n" if "\r\n" in text else "\n"
    encoded = generated.replace("\n", newline).encode("utf-8")
    cmake_path.write_bytes((b"\xef\xbb\xbf" if has_bom else b"") + encoded)

    presets_path = destination_root / "CMakePresets.json"
    presets = make_game_presets(json.loads(presets_path.read_text(encoding="utf-8-sig")))
    _write_text_file(presets_path, json.dumps(presets, ensure_ascii=False, indent=2) + "\n")
    _write_text_file(destination_root / "README.md", GENERATED_README)
    _write_text_file(destination_root / ".gitignore", GAME_GITIGNORE, bom=False)
    _write_text_file(destination_root / ".gitattributes", GAME_GITATTRIBUTES, bom=False)

    # ゲーム向けの差し替え文書があれば使い、残すガイドから開発記録を除く。
    for relative in PRODUCT_DOCS:
        if not relative.endswith(".md"):
            continue
        document = destination_root / relative
        override = source_root / "cmake" / "game-repository-docs" / document.name
        input_path = override if document.name in ("quickstart.md", "package-layout.md") and override.is_file() else source_root / relative
        original = input_path.read_bytes()
        has_bom = original.startswith(b"\xef\xbb\xbf")
        content = _sanitize_document(original.decode("utf-8-sig"), destination_root, Path(relative))
        _write_text_file(document, content, bom=has_bom)

    broken = find_broken_local_links(destination_root)
    if broken:
        raise ExportError("Generated product documentation has unresolved local links: " + "; ".join(broken))
    return selected


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_checkout", type=Path, help="development checkout containing the current product sources")
    parser.add_argument("destination", type=Path, help="empty staging directory for the game-building source tree")
    args = parser.parse_args(argv)
    try:
        selected = export_repository(args.source_checkout, args.destination)
        print(f"Game repository ready: {len(selected)} allowlisted files copied to {args.destination.resolve()}")
        return 0
    except (OSError, json.JSONDecodeError, ExportError) as exc:
        print(f"gkcore game repository export stopped: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
