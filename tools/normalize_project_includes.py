#!/usr/bin/env python3
"""プロジェクト内のC++ includeをmodule-root表記へ揃える候補を表示する。"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


AUTHORED_SUFFIXES = {".h", ".hpp", ".cpp"}
AUTHORED_ROOTS = ("src", "include", "examples", "tests")
INCLUDE_LINE = re.compile(r'(?m)^(\s*#\s*include\s*)"([^"\r\n]+)"')
INCLUDE_DIRECTIVE = re.compile(r"(?m)^\s*#\s*include\b[^\r\n]*")
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|[^\s]')


def is_within(path: Path, parent: Path) -> bool:
    """解決済みpathが指定directoryの内側にあるか調べる。"""
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def read_source(path: Path) -> tuple[str, bool]:
    """UTF-8本文と元のBOM有無を読む。"""
    data = path.read_bytes()
    has_bom = data.startswith(b"\xef\xbb\xbf")
    return data.decode("utf-8-sig"), has_bom


def non_include_token_hash(source: str) -> str:
    """include指示を除いたC++ token列のhashを返す。"""
    body = INCLUDE_DIRECTIVE.sub("", source)
    tokens = [value for value in TOKEN.findall(body) if not value.startswith(("//", "/*"))]
    return hashlib.sha256("".join(tokens).encode("utf-8")).hexdigest()


def locate_target(workspace: Path, source_path: Path, include_name: str) -> Path | None:
    """相対位置と想定include rootから既存headerを探す。"""
    requested = Path(include_name)
    if requested.is_absolute():
        return None
    roots = (source_path.parent, workspace, workspace / "src", workspace / "include", workspace / "third_party", workspace / "tests")
    for root in roots:
        candidate = root / requested
        try:
            resolved = candidate.resolve(strict=True)
        except (OSError, RuntimeError):
            continue
        if resolved.is_file():
            return resolved
    return None


def qualified_include(workspace: Path, target: Path) -> str | None:
    """対象rootに合わせてquoteまたはangleのinclude名を作る。"""
    public_root = (workspace / "include").resolve()
    source_root = (workspace / "src").resolve()
    third_party_root = (workspace / "third_party").resolve()
    if is_within(target, public_root):
        relative = target.relative_to(public_root).as_posix()
        if relative == "gkcore.h" or relative.startswith("gkcore/"):
            return f"<{relative}>"
        return f"<gkcore/{relative}>"
    if is_within(target, source_root):
        return f'"{target.relative_to(source_root).as_posix()}"'
    if is_within(target, third_party_root):
        relative = target.relative_to(third_party_root).as_posix()
        return f"<{relative}>"
    tests_root = (workspace / "tests").resolve()
    if is_within(target, tests_root):
        return f'"tests/{target.relative_to(workspace / "tests").as_posix()}"'
    examples_root = (workspace / "examples").resolve()
    if is_within(target, examples_root):
        return f'"examples/{target.relative_to(workspace / "examples").as_posix()}"'
    shaders_root = (workspace / "shaders").resolve()
    if is_within(target, shaders_root):
        return f'"shaders/{target.relative_to(shaders_root).as_posix()}"'
    return None


def collect_sources(workspace: Path) -> list[Path]:
    """対象の自作C++領域からheaderとtranslation unitを集める。"""
    files: list[Path] = []
    for root_name in AUTHORED_ROOTS:
        root = workspace / root_name
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.suffix.lower() in AUTHORED_SUFFIXES and path.is_file():
                resolved = path.resolve()
                if is_within(resolved, root.resolve()):
                    files.append(path)
    return sorted(files)


def plan_file(workspace: Path, path: Path, source: str) -> tuple[str, list[tuple[str, str]], int]:
    """一つのsourceに対するinclude差分を計画する。"""
    changes: list[tuple[str, str]] = []
    unresolved = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal unresolved
        original = match.group(2)
        target = locate_target(workspace, path, original)
        if target is None:
            unresolved += 1
            return match.group(0)
        replacement = qualified_include(workspace, target)
        if replacement is None or replacement == f'"{original}"':
            return match.group(0)
        changes.append((original, replacement))
        return match.group(1) + replacement

    planned = INCLUDE_LINE.sub(replace, source)
    return planned, changes, unresolved


def main() -> int:
    """dry-runを既定にして変更候補とtoken保持を報告する。"""
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="Project C++ include表記の変換候補を表示します。")
    parser.add_argument("--workspace", type=Path, default=Path(__file__).resolve().parents[1], help="project root")
    parser.add_argument("--apply", action="store_true", help="候補をsourceへ書き込みます。既定はdry-runです。")
    parser.add_argument("--all", action="store_true", help="dry-runで全include変更を表示します。")
    parser.add_argument("--show", type=int, default=40, help="dry-runで表示する変更例数")
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    files = collect_sources(workspace)
    changed_files = 0
    change_count = 0
    unresolved_count = 0
    examples: list[str] = []
    token_mismatches: list[str] = []
    planned_files: list[tuple[Path, str, bool]] = []

    for path in files:
        source, has_bom = read_source(path)
        planned, changes, unresolved = plan_file(workspace, path, source)
        unresolved_count += unresolved
        if not changes:
            continue
        before_hash = non_include_token_hash(source)
        after_hash = non_include_token_hash(planned)
        if before_hash != after_hash:
            token_mismatches.append(path.relative_to(workspace).as_posix())
            continue
        changed_files += 1
        change_count += len(changes)
        planned_files.append((path, planned, has_bom))
        for original, replacement in changes:
            if len(examples) < args.show or args.all:
                examples.append(f"{path.relative_to(workspace).as_posix()}: {original} -> {replacement}")

    print(f"対象ファイル: {len(files)}")
    print(f"変更候補: {change_count} include / {changed_files} files")
    print(f"解決できないquote include: {unresolved_count}")
    print(f"非include token hash不一致: {len(token_mismatches)}")
    for item in examples:
        print(item)
    for path in token_mismatches:
        print(f"ERROR token hash mismatch: {path}", file=sys.stderr)
    if token_mismatches:
        return 2

    if args.apply:
        for path, planned, has_bom in planned_files:
            normalized = planned.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")
            path.write_bytes(b"\xef\xbb\xbf" + normalized.encode("utf-8"))
        print(f"書き込み済み: {len(planned_files)} files")
    else:
        print("dry-runのみです。適用するには --apply を指定してください。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
