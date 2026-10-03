#!/usr/bin/env python3
"""Verify a The Forge source tree against the repository's immutable file lock."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "cmake" / "forge-files-lock.json"
REQUIRED_FILES = (
    "LICENSE",
    "Examples_3/Unit_Tests/PC_VS2019/Libraries/OS/OS.vcxproj",
    "Examples_3/Unit_Tests/PC_VS2019/Libraries/Renderer/Renderer.vcxproj",
    "Common_3/Tools/ForgeShadingLanguage/fsl.py",
    "Common_3/Tools/ForgeShadingLanguage/VS/fsl.targets",
    "Common_3/Graphics/Direct3D12/Direct3D12.c",
    "Common_3/Graphics/Direct3D12/Direct3D12_cxx.cpp",
    "Common_3/Resources/ResourceLoader/ResourceLoader.cpp",
    "Common_3/OS/Windows/WindowsBase.cpp",
)


class ForgeCheckoutError(RuntimeError):
    pass


def _git_blob_sha(path: Path) -> str:
    try:
        size = path.stat().st_size
        digest = hashlib.sha1(b"blob " + str(size).encode("ascii") + b"\0")
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        raise ForgeCheckoutError(f"cannot read Forge source file {path}: {exc}") from exc
    return digest.hexdigest()


def validate_checkout(root, expected_commit=None, manifest=None) -> str:
    root = Path(root).resolve()
    if not root.is_dir():
        raise ForgeCheckoutError(f"Forge source directory does not exist: {root}")
    lock = None
    if expected_commit is None or manifest is None:
        try:
            lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise ForgeCheckoutError(f"cannot read Forge pin lock {LOCK_PATH}: {exc}") from exc
        if expected_commit is None:
            expected_commit = lock["commit"]
        if manifest is None:
            manifest = lock["files"]
    if len(expected_commit) != 40 or any(c not in "0123456789abcdef" for c in expected_commit):
        raise ForgeCheckoutError("expected Forge commit must be a full lowercase Git SHA-1")
    if lock is not None and expected_commit != lock["commit"]:
        raise ForgeCheckoutError(f"expected commit {lock['commit']}, got {expected_commit}")
    if (root / ".git").exists():
        result = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                                text=True, capture_output=True, check=False)
        actual_commit = result.stdout.strip()
        if result.returncode != 0 or actual_commit != expected_commit:
            raise ForgeCheckoutError(f"expected commit {expected_commit}, got {actual_commit or 'unknown'}")

    missing_required = [name for name in REQUIRED_FILES if not (root / name).is_file()]
    if missing_required:
        raise ForgeCheckoutError("missing required Forge files: " + ", ".join(missing_required))

    missing = []
    mismatched = []
    for entry in manifest:
        relative = entry["path"]
        relative_path = Path(relative)
        if relative_path.is_absolute() or ".." in relative_path.parts:
            raise ForgeCheckoutError(f"unsafe path in Forge pin manifest: {relative}")
        target = root / relative_path
        try:
            if os.path.commonpath((str(root), str(target.resolve()))) != str(root):
                raise ForgeCheckoutError(f"unsafe path in Forge pin manifest: {relative}")
        except ValueError as exc:
            raise ForgeCheckoutError(f"unsafe path in Forge pin manifest: {relative}") from exc
        if not target.is_file():
            missing.append(relative)
            continue
        actual = _git_blob_sha(target)
        if actual != entry["git_blob"]:
            mismatched.append(relative)
    if missing or mismatched:
        details = []
        if missing:
            details.append("missing: " + ", ".join(missing[:5]))
        if mismatched:
            details.append("modified: " + ", ".join(mismatched[:5]))
        raise ForgeCheckoutError("Forge files do not match the pinned commit (" + "; ".join(details) + ")")
    return expected_commit


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="extracted The Forge checkout")
    args = parser.parse_args(argv)
    try:
        commit = validate_checkout(args.root)
    except ForgeCheckoutError as exc:
        print(f"Forge checkout validation failed: {exc}", file=sys.stderr)
        return 1
    print(f"Verified The Forge {commit} ({len(json.loads(LOCK_PATH.read_text(encoding='utf-8'))['files'])} locked files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
