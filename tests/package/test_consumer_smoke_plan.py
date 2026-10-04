#!/usr/bin/env python3
import unittest
from pathlib import Path

from verify_runtime_package import consumer_configure_command


class ConsumerSmokePlanTests(unittest.TestCase):
    def test_gpu_smoke_is_opt_in_for_the_installed_consumer(self):
        command = consumer_configure_command(
            "cmake", Path("source"), Path("build"), "Visual Studio", Path("prefix"), "Release", True
        )
        self.assertIn("-DGKCORE_PACKAGE_GPU_SMOKE=ON", command)

    def test_regular_package_consumer_does_not_enable_gpu_smoke(self):
        command = consumer_configure_command(
            "cmake", Path("source"), Path("build"), "Visual Studio", Path("prefix"), "Release", False
        )
        self.assertNotIn("-DGKCORE_PACKAGE_GPU_SMOKE=ON", command)


if __name__ == "__main__":
    unittest.main()
