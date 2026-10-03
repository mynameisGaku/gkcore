#!/usr/bin/env python3
"""Validate the exact DXC tools and redistributable files used by gkcore."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

LOCK_PATH = Path(__file__).resolve().parents[1] / "cmake" / "dxc-lock.json"


def verify(root: Path) -> list[str]:
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    missing = [name for name in lock["files_sha256"] if not (root / name).is_file()]
    if missing:
        raise ValueError("DXC package is missing required files: " + ", ".join(missing))
    invalid = []
    for name, expected in lock["files_sha256"].items():
        checksum = hashlib.sha256()
        with (root / name).open("rb") as file:
            for block in iter(lambda: file.read(1024 * 1024), b""):
                checksum.update(block)
        digest = checksum.hexdigest()
        if digest != expected:
            invalid.append(name)
    if invalid:
        raise ValueError("DXC package file SHA-256 mismatch: " + ", ".join(invalid))
    return list(lock["files_sha256"])


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="extracted DXC release root")
    args = parser.parse_args(argv)
    try:
        files = verify(Path(args.root).resolve())
        print(f"DXC {json.loads(LOCK_PATH.read_text(encoding='utf-8'))['release']} package verified ({len(files)} required files)")
        return 0
    except (OSError, ValueError, KeyError) as exc:
        print(f"DXC verification failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
