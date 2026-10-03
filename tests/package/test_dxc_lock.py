"""Test the pinned DXC inventory without network access."""
import json
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from verify_dxc import LOCK_PATH, verify


class DxcLockTests(unittest.TestCase):
    def test_lock_pins_release_and_archive_digest(self):
        lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        self.assertEqual(lock["release"], "1.8.2405")
        self.assertEqual(lock["windows_sha256"], "8d2656e9523e7b3b7c41159237d74f89cc98034056f61f568b8841da0449c965")
        self.assertIn("/v1.8.2405/", lock["windows_url"])

    def test_requires_runtime_and_notice_files(self):
        import verify_dxc
        real_lock = verify_dxc.LOCK_PATH
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock_path = root / "lock.json"
            lock = {"release": "test", "windows_url": "https://example.invalid", "windows_sha256": "0" * 64,
                    "files_sha256": {}}
            contents = {}
            for name in ("bin/x64/dxc.exe", "bin/x64/dxcompiler.dll", "bin/x64/dxil.dll",
                         "lib/x64/dxcompiler.lib", "LICENSE-LLVM.txt", "LICENSE-MIT.txt", "LICENSE-MS.txt"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                contents[name] = ("payload:" + name).encode()
                path.write_bytes(contents[name])
                lock["files_sha256"][name] = hashlib.sha256(contents[name]).hexdigest()
            lock_path.write_text(json.dumps(lock), encoding="utf-8")
            verify_dxc.LOCK_PATH = lock_path
            try:
                self.assertEqual(len(verify(root)), len(lock["files_sha256"]))
                (root / "bin/x64/dxil.dll").unlink()
                with self.assertRaisesRegex(ValueError, "dxil.dll"):
                    verify(root)
                (root / "bin/x64/dxil.dll").write_text("tampered", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    verify(root)
            finally:
                verify_dxc.LOCK_PATH = real_lock


if __name__ == "__main__":
    unittest.main()
