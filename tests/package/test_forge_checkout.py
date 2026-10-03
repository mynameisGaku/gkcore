#!/usr/bin/env python3
"""RED-first contract for a pinned, complete Forge development checkout."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from forge_checkout import ForgeCheckoutError, validate_checkout


REQUIRED = (
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


class ForgeCheckoutTests(unittest.TestCase):
    def checkout(self, root, *, include_all=True):
        root.mkdir()
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        for name in REQUIRED if include_all else REQUIRED[:-1]:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(root), "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                        "add", "."], check=True)
        subprocess.run(["git", "-C", str(root), "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                        "commit", "-qm", "fixture"], check=True)
        return subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()

    def test_accepts_complete_checkout_at_requested_pin(self):
        with tempfile.TemporaryDirectory(prefix="forge-pin-good-") as tmp:
            root = Path(tmp) / "forge"
            commit = self.checkout(root)
            manifest = [{"path": name, "git_blob": self.git_blob(root / name)} for name in REQUIRED]
            self.assertEqual(validate_checkout(root, expected_commit=commit, manifest=manifest), commit)

    def test_rejects_wrong_commit(self):
        with tempfile.TemporaryDirectory(prefix="forge-pin-wrong-") as tmp:
            root = Path(tmp) / "forge"
            self.checkout(root)
            with self.assertRaisesRegex(ForgeCheckoutError, "expected commit"):
                validate_checkout(root, expected_commit="0" * 40,
                                  manifest=[{"path": name, "git_blob": self.git_blob(root / name)} for name in REQUIRED])

    def test_rejects_missing_official_windows_projects_and_sources(self):
        with tempfile.TemporaryDirectory(prefix="forge-pin-incomplete-") as tmp:
            root = Path(tmp) / "forge"
            commit = self.checkout(root, include_all=False)
            with self.assertRaisesRegex(ForgeCheckoutError, "missing required Forge files"):
                validate_checkout(root, expected_commit=commit,
                                  manifest=[{"path": name, "git_blob": self.git_blob(root / name)} for name in REQUIRED[:-1]])

    @staticmethod
    def git_blob(path):
        data = path.read_bytes()
        import hashlib
        return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


if __name__ == "__main__":
    unittest.main()
