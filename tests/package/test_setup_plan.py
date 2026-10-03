"""Setup must run the full CPU/package suite as well as opt-in GPU tests."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import setup


class SetupPlanTests(unittest.TestCase):
    def test_prepare_keeps_full_suite_when_gpu_smoke_is_enabled(self):
        commands = []
        with patch.object(setup, "require_windows_toolchain", return_value=Path("C:/VS/MSBuild.exe")), \
             patch.object(setup, "ensure_dependencies"), \
             patch.object(setup, "run", side_effect=lambda command, **kwargs: commands.append(command)), \
             patch("builtins.print"):
            setup.prepare(gpu_check=True)

        configure = next(command for command in commands if command[:2] == ["cmake", "-S"])
        self.assertIn("-DGKCORE_RUN_BACKEND_SMOKE=ON", configure)
        command = next(command for command in commands if command[0] == "ctest")
        self.assertIn("--output-on-failure", command)
        self.assertNotIn("-L", command)
        self.assertNotIn("windows|gpu", command)


if __name__ == "__main__":
    unittest.main()
