#!/usr/bin/env python3
"""Install into an isolated prefix, check its exact manifest, and build a consumer."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from package_allowlist import validate


def consumer_configure_command(install_script, source, consumer_build, generator, prefix, config, gpu_smoke):
    command = [install_script, "-S", str(source / "tests/package/consumer"), "-B", str(consumer_build),
               "-G", generator, f"-DCMAKE_PREFIX_PATH={prefix}"]
    if config:
        command += [f"-DCMAKE_BUILD_TYPE={config}"]
    if gpu_smoke:
        command.append("-DGKCORE_PACKAGE_GPU_SMOKE=ON")
    return command


def run(command, cwd=None):
    subprocess.run(command, cwd=cwd, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="")
    parser.add_argument("--generator", required=True)
    parser.add_argument("--install-script", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--gpu-smoke", action="store_true")
    args = parser.parse_args()
    source = Path(args.source_dir).resolve()
    build = Path(args.build_dir).resolve()
    with tempfile.TemporaryDirectory(prefix="gkcore-runtime-package-") as temporary:
        root = Path(temporary)
        prefix = root / "prefix"
        command = [args.install_script, "--install", str(build), "--prefix", str(prefix), "--component", "Runtime"]
        if args.config:
            command += ["--config", args.config]
        run(command)
        files = {p.relative_to(prefix).as_posix() for p in prefix.rglob("*") if p.is_file()}
        validate(files, configuration=args.config or "Release")

        consumer_build = root / "consumer-build"
        configure = consumer_configure_command(args.install_script, source, consumer_build, args.generator,
                                               prefix, args.config, args.gpu_smoke)
        run(configure)
        run([args.install_script, "--build", str(consumer_build), "--config", args.config or "Release"])
        executable_name = "package_consumer.exe" if os.name == "nt" else "package_consumer"
        executables = list(consumer_build.rglob(executable_name))
        if len(executables) != 1:
            raise RuntimeError(f"expected exactly one built consumer executable, found {executables}")
        environment = os.environ.copy()
        environment["PATH"] = str(prefix / "bin") + os.pathsep + environment.get("PATH", "")
        subprocess.run([str(executables[0])], check=True, env=environment)


if __name__ == "__main__":
    main()
