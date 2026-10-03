#!/usr/bin/env python3
"""Download, checksum, and safely extract the locked Windows DXC package."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tempfile
from urllib.request import Request, urlopen
import zipfile

from verify_dxc import LOCK_PATH, verify


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract_safe(archive: Path, destination: Path) -> None:
    with zipfile.ZipFile(archive) as package:
        for item in package.infolist():
            name = PurePosixPath(item.filename.replace("\\", "/"))
            if name.is_absolute() or ".." in name.parts or not name.parts:
                raise ValueError(f"unsafe path in DXC archive: {item.filename}")
            if any(":" in part for part in name.parts):
                raise ValueError(f"unsafe path in DXC archive: {item.filename}")
            # Reject Unix symlinks carried in external attributes.
            if (item.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError(f"unsupported symlink in DXC archive: {item.filename}")
        package.extractall(destination)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, help="new development-tools directory")
    parser.add_argument("--archive", help="use a previously downloaded release ZIP")
    args = parser.parse_args(argv)
    try:
        lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        destination = Path(args.destination).resolve()
        if destination.exists():
            raise ValueError(f"destination already exists; refusing to replace it: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".gkcore-dxc-", dir=destination.parent) as temp_name:
            temp = Path(temp_name)
            archive = Path(args.archive).resolve() if args.archive else temp / "dxc.zip"
            if not args.archive:
                request = Request(lock["windows_url"], headers={"User-Agent": "gkcore-developer-bootstrap/1"})
                print(f"Downloading pinned DXC {lock['release']} package...")
                with urlopen(request, timeout=60) as response, archive.open("wb") as output:
                    shutil.copyfileobj(response, output, length=1024 * 1024)
            digest = sha256(archive)
            if digest != lock["windows_sha256"]:
                raise ValueError(f"DXC archive SHA-256 mismatch: expected {lock['windows_sha256']}, got {digest}")
            extracted = temp / "package"
            extracted.mkdir()
            extract_safe(archive, extracted)
            verify(extracted)
            os.replace(extracted, destination)
        print(f"DXC {lock['release']} verified at {destination}")
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        print(f"DXC setup failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
