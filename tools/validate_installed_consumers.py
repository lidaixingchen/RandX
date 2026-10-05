#!/usr/bin/env python3
"""Build and run C++17/C++23 consumers against one selected RandX package."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence


CONFIG_FILE_NAME: str = "RandXConfig.cmake"
CONSUMER_TARGET_NAME: str = "randx_consumer"
CONSUMER_TEST_NAME: str = "randx_consumer_run"
SUPPORTED_STANDARDS: tuple[str, ...] = ("17", "23")


class ValidationError(RuntimeError):
    """Raised when the selected package cannot satisfy the consumer contract."""


def _is_within(path: Path, root: Path) -> bool:
    path_value: str = os.path.normcase(str(path.resolve()))
    root_value: str = os.path.normcase(str(root.resolve()))
    try:
        return os.path.commonpath((path_value, root_value)) == root_value
    except ValueError:
        return False


def select_config_dir(prefix: Path, candidates: Sequence[Path]) -> Path:
    """Select the unique RandX package config inside a package prefix."""
    prefix_path: Path = prefix.resolve()
    matching_configs: list[Path] = sorted(
        {
            candidate.resolve()
            for candidate in candidates
            if candidate.name == CONFIG_FILE_NAME and _is_within(candidate, prefix_path)
        }
    )
    if len(matching_configs) > 1:
        formatted_configs: str = ", ".join(str(candidate) for candidate in matching_configs)
        raise ValidationError(f"The selected prefix contains multiple {CONFIG_FILE_NAME} files: {formatted_configs}")
    if matching_configs:
        return matching_configs[0].parent
    return prefix_path


def _run(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    result: subprocess.CompletedProcess[str] = subprocess.run(
        list(command),
        check=False,
        text=True,
    )
    if result.returncode != 0:
        raise ValidationError(f"Command failed with exit code {result.returncode}: {subprocess.list2cmdline(command)}")
    return result


def _cmake_prefix_path(decoy_prefixes: Sequence[Path], selected_prefix: Path) -> str:
    prefix_paths: list[Path] = [*(prefix.resolve() for prefix in decoy_prefixes), selected_prefix.resolve()]
    return ";".join(dict.fromkeys(str(prefix) for prefix in prefix_paths))


def _resolve_ctest(args: argparse.Namespace) -> str:
    if args.ctest:
        return args.ctest

    cmake_path: Path | None = None
    requested_cmake: Path = Path(args.cmake)
    if requested_cmake.is_file():
        cmake_path = requested_cmake.resolve()
    else:
        located_cmake: str | None = shutil.which(args.cmake)
        if located_cmake:
            cmake_path = Path(located_cmake).resolve()

    if cmake_path:
        ctest_name: str = "ctest.exe" if os.name == "nt" else "ctest"
        sibling_ctest: Path = cmake_path.with_name(ctest_name)
        if sibling_ctest.is_file():
            return str(sibling_ctest)

    return shutil.which("ctest") or "ctest"


def _configure_command(
    args: argparse.Namespace,
    *,
    build_dir: Path,
    config_dir: Path,
    include_root: Path,
    prefix_path: str,
    standard: str,
) -> list[str]:
    command: list[str] = [
        args.cmake,
        "-S",
        str(args.consumer_source_dir.resolve()),
        "-B",
        str(build_dir.resolve()),
        f"-DRANDX_CONSUMER_STANDARD={standard}",
        f"-DRANDX_EXPECTED_CONFIG_DIR={config_dir.resolve()}",
        f"-DRANDX_EXPECTED_INCLUDE_ROOT={include_root.resolve()}",
        f"-DCMAKE_PREFIX_PATH={prefix_path}",
        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
        "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
    ]
    if args.generator:
        command.extend(("-G", args.generator))
    if args.generator_platform:
        command.extend(("-A", args.generator_platform))
    if args.generator_toolset:
        command.extend(("-T", args.generator_toolset))
    if args.compiler:
        command.append(f"-DCMAKE_CXX_COMPILER={args.compiler}")
    if args.make_program:
        command.append(f"-DCMAKE_MAKE_PROGRAM={args.make_program}")
    if args.resource_compiler:
        command.append(f"-DCMAKE_RC_COMPILER={args.resource_compiler}")
    if args.manifest_tool:
        command.append(f"-DCMAKE_MT={args.manifest_tool}")
    if args.toolchain_file:
        command.append(f"-DCMAKE_TOOLCHAIN_FILE={Path(args.toolchain_file).resolve()}")
    if args.crosscompiling_emulator:
        command.append(f"-DCMAKE_CROSSCOMPILING_EMULATOR={args.crosscompiling_emulator}")
    if args.build_type:
        command.append(f"-DCMAKE_BUILD_TYPE={args.build_type}")
    return command


def _ctest_command(args: argparse.Namespace, *, build_dir: Path) -> list[str]:
    command: list[str] = [
        _resolve_ctest(args),
        "--test-dir",
        str(build_dir.resolve()),
        "--output-on-failure",
        "-R",
        f"^{CONSUMER_TEST_NAME}$",
    ]
    if args.configuration:
        command.extend(("-C", args.configuration))
    return command


def _check_missing_config_fails(
    args: argparse.Namespace,
    *,
    work_dir: Path,
    prefix: Path,
    decoy_prefixes: Sequence[Path],
) -> None:
    missing_prefix: Path = work_dir / "missing-randx-config"
    missing_prefix.mkdir(parents=True, exist_ok=True)
    probe_source_dir: Path = work_dir / "missing-config-source"
    probe_source_dir.mkdir(parents=True, exist_ok=True)
    (probe_source_dir / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(RandXConfigProbe NONE)\n"
        "find_package(RandX CONFIG REQUIRED PATHS \"${RANDX_MISSING_CONFIG_PREFIX}\" NO_DEFAULT_PATH)\n",
        encoding="utf-8",
    )
    probe_build_dir: Path = work_dir / "missing-config-probe"
    candidates: list[Path] = [*decoy_prefixes, prefix]
    prefix_path: str = _cmake_prefix_path(candidates, missing_prefix)
    command: list[str] = [
        args.cmake,
        "-S",
        str(probe_source_dir.resolve()),
        "-B",
        str(probe_build_dir.resolve()),
        f"-DRANDX_MISSING_CONFIG_PREFIX={missing_prefix.resolve()}",
        f"-DCMAKE_PREFIX_PATH={prefix_path}",
        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
        "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
    ]
    if args.generator:
        command.extend(("-G", args.generator))
    if args.generator_platform:
        command.extend(("-A", args.generator_platform))
    if args.generator_toolset:
        command.extend(("-T", args.generator_toolset))
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
    )
    output: str = f"{result.stdout}\n{result.stderr}"
    if result.returncode == 0:
        raise ValidationError("CMake configured the consumer even though the selected prefix has no RandX config")
    if "Could not find a package configuration file provided by \"RandX\"" not in output:
        raise ValidationError(f"The missing-config probe failed for an unexpected reason:\n{output}")
    print("Confirmed: CMake rejects a missing RandX config even when another package prefix is available.")


def _parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake", help="CMake executable")
    parser.add_argument("--ctest", help="CTest executable; defaults to the CMake sibling or PATH")
    parser.add_argument("--consumer-source-dir", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True, help="Selected install or build-tree prefix")
    parser.add_argument("--config-dir", type=Path, help="Directory containing the selected RandXConfig.cmake")
    parser.add_argument("--include-root", type=Path, required=True, help="Root that must own exported include paths")
    parser.add_argument("--install-build-dir", type=Path, help="Install this build tree into --prefix before validation")
    parser.add_argument("--decoy-prefix", action="append", type=Path, default=[], help="An alternate prefix that must not be selected")
    parser.add_argument("--generator")
    parser.add_argument("--generator-platform")
    parser.add_argument("--generator-toolset")
    parser.add_argument("--compiler")
    parser.add_argument("--make-program")
    parser.add_argument("--resource-compiler")
    parser.add_argument("--manifest-tool")
    parser.add_argument("--toolchain-file")
    parser.add_argument(
        "--crosscompiling-emulator",
        help="Semicolon-separated CMake command list used to run target executables",
    )
    parser.add_argument("--build-type")
    parser.add_argument("--configuration", help="Configuration for multi-config generators")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args: argparse.Namespace = _parse_args(argv)
    prefix: Path = args.prefix.resolve()
    work_dir: Path = args.work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)

    if args.install_build_dir:
        install_command: list[str] = [args.cmake, "--install", str(args.install_build_dir.resolve()), "--prefix", str(prefix)]
        if args.configuration:
            install_command.extend(("--config", args.configuration))
        _run(install_command)

    prefix.mkdir(parents=True, exist_ok=True)
    config_dir: Path = args.config_dir.resolve() if args.config_dir else select_config_dir(
        prefix,
        tuple(prefix.rglob(CONFIG_FILE_NAME)) if prefix.is_dir() else (),
    )
    decoy_prefixes: list[Path] = [*args.decoy_prefix, prefix]

    for standard in SUPPORTED_STANDARDS:
        build_dir: Path = work_dir / f"cpp{standard}"
        prefix_path: str = _cmake_prefix_path(decoy_prefixes, prefix)
        _run(
            _configure_command(
                args,
                build_dir=build_dir,
                config_dir=config_dir,
                include_root=args.include_root,
                prefix_path=prefix_path,
                standard=standard,
            )
        )
        build_command: list[str] = [args.cmake, "--build", str(build_dir), "--target", CONSUMER_TARGET_NAME]
        if args.configuration:
            build_command.extend(("--config", args.configuration))
        _run(build_command)
        _run(_ctest_command(args, build_dir=build_dir))
        print(f"Passed: C++{standard} consumer built and ran from {prefix}.")

    _check_missing_config_fails(
        args,
        work_dir=work_dir,
        prefix=prefix,
        decoy_prefixes=decoy_prefixes,
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValidationError as error:
        print(f"Consumer validation failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
