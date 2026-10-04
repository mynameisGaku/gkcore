#!/usr/bin/env python3
"""Explicitly fetch or unpack the locked The Forge development dependency."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
import tempfile
from urllib.request import Request, urlopen

from forge_checkout import ForgeCheckoutError, LOCK_PATH, validate_checkout


def _extended_windows_path(path: Path) -> Path:
    """Windowsの長いcheckoutパスを扱うため、絶対パスに長さ対応prefixを付ける。"""
    absolute = os.path.abspath(os.fspath(path))
    if os.name != "nt" or absolute.startswith("\\\\?\\"):
        return Path(absolute)
    if absolute.startswith("\\\\"):
        return Path("\\\\?\\UNC\\" + absolute[2:])
    return Path("\\\\?\\" + absolute)


def download(url: str, target: Path) -> None:
    request = Request(url, headers={"User-Agent": "gkcore-developer-bootstrap/1"})
    with urlopen(request, timeout=60) as response, target.open("wb") as output:
        shutil.copyfileobj(response, output, length=1024 * 1024)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract_safe(archive: Path, temporary: Path, commit: str) -> Path:
    expected_root = f"The-Forge-{commit}"
    with tarfile.open(archive, "r:gz") as tar:
        members = tar.getmembers()
        if not members:
            raise ForgeCheckoutError("The Forge archive is empty")
        for member in members:
            path = PurePosixPath(member.name)
            if "\\" in member.name or path.is_absolute() or ".." in path.parts or not path.parts or path.parts[0] != expected_root:
                raise ForgeCheckoutError(f"unsafe or unexpected path in archive: {member.name}")
            if member.issym() or member.islnk() or member.isdev():
                raise ForgeCheckoutError(f"unsupported link/device in archive: {member.name}")
        extraction_root = _extended_windows_path(temporary)
        tar.extractall(extraction_root, members=members)
    return extraction_root / expected_root


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, help="new directory for pinned The Forge sources")
    parser.add_argument("--archive", help="use a previously downloaded pinned tar.gz")
    args = parser.parse_args(argv)
    try:
        lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        destination = Path(args.destination).resolve()
        if destination.exists():
            raise ForgeCheckoutError(f"destination already exists; refusing to replace it: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".gkcore-forge-", dir=destination.parent) as temp_name:
            temp = Path(temp_name)
            archive = Path(args.archive).resolve() if args.archive else temp / "the-forge.tar.gz"
            if not args.archive:
                print(f"Downloading The Forge commit {lock['commit']} from the pinned archive URL...")
                download(lock["archive_url"], archive)
            actual = sha256(archive)
            if actual != lock["archive_sha256"]:
                raise ForgeCheckoutError(f"archive SHA-256 mismatch: expected {lock['archive_sha256']}, got {actual}")
            extracted = extract_safe(archive, temp, lock["commit"])
            validate_checkout(extracted)
            os.replace(extracted, destination)
        print(f"The Forge {lock['commit']} verified at {destination}")
        return 0
    except (OSError, ValueError, tarfile.TarError, ForgeCheckoutError) as exc:
        print(f"The Forge setup failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
