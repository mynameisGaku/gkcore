#!/usr/bin/env python3
"""A development-only install must never look like a usable Runtime SDK."""
import argparse
from pathlib import Path
import subprocess
import tempfile


parser = argparse.ArgumentParser()
parser.add_argument("--cmake", required=True)
parser.add_argument("--build-dir", required=True)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix="gkcore-runtime-refusal-") as temporary:
    prefix = Path(temporary) / "prefix"
    result = subprocess.run(
        [args.cmake, "--install", args.build_dir, "--prefix", str(prefix), "--component", "Runtime"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode == 0:
        raise SystemExit("Runtime installation unexpectedly succeeded without a real renderer")
    if "Runtime is unavailable" not in result.stdout:
        raise SystemExit("Runtime installation failed for an unexpected reason:\n" + result.stdout)
    if prefix.exists() and any(path.is_file() for path in prefix.rglob("*")):
        raise SystemExit("failed Runtime installation left distributable files behind")
print("PASS: Runtime component fails closed when no real renderer was built")
