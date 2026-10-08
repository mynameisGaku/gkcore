#!/usr/bin/env python3
"""固定した接線生成コードと原文noticeの改変を検出する。"""
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / "third_party" / "mikktspace"
EXPECTED = {
    "mikktspace.c": "de87e74107df766ce68108801262bd8d53899414236b59810509a8fc2a51e288",
    "mikktspace.h": "17fc433894f24c73753d548086cc4d8c5c0379f4a6edfb98b5da243e4f0bc3d0",
}


class VendorLockTests(unittest.TestCase):
    """固定commitのsource bytesと配布するnoticeの一致を確認する。"""

    def test_exact_pinned_source_bytes(self):
        for filename, expected in EXPECTED.items():
            with self.subTest(filename=filename):
                self.assertEqual(hashlib.sha256((ROOT / filename).read_bytes()).hexdigest(), expected)

    def test_license_matches_original_header(self):
        header = (ROOT / "mikktspace.h").read_bytes()
        start = header.index(b"/**\n *  Copyright")
        end = header.index(b" */", start) + 3
        self.assertEqual((ROOT / "LICENSE").read_bytes(), header[start:end] + b"\n")


if __name__ == "__main__":
    unittest.main()
