from __future__ import annotations

import os
import unittest
from pathlib import Path
from unittest.mock import patch

from validate_installed_consumers import (
    ValidationError,
    select_config_dir,
    _parse_args,
    _configure_command,
    _ctest_command,
    _resolve_ctest,
    main,
)


class ConfigSelectionTests(unittest.TestCase):
    def test_explicit_build_program_with_spaces_reaches_child_configuration(self) -> None:
        prefix: Path = Path("selected-prefix")
        make_program: str = "C:/Build Tools/Ninja/ninja.exe"
        resource_compiler: str = "C:/Windows SDK/rc.exe"
        manifest_tool: str = "C:/Windows SDK/mt.exe"
        args = _parse_args([
            "--consumer-source-dir", "examples/consumer_validation", "--work-dir", "work",
            "--prefix", str(prefix), "--include-root", str(prefix), "--generator", "Ninja",
            "--make-program", make_program,
            "--resource-compiler", resource_compiler, "--manifest-tool", manifest_tool,
        ])
        command: list[str] = _configure_command(
            args, build_dir=Path("work/cpp17"), config_dir=prefix, include_root=prefix,
            prefix_path=str(prefix), standard="17",
        )
        self.assertIn(f"-DCMAKE_MAKE_PROGRAM={make_program}", command)
        self.assertIn(f"-DCMAKE_RC_COMPILER={resource_compiler}", command)
        self.assertIn(f"-DCMAKE_MT={manifest_tool}", command)

    def test_selects_the_config_below_the_requested_prefix(self) -> None:
        prefix: Path = Path("C:/packages/randx")
        selected_config: Path = prefix / "share" / "RandX" / "RandXConfig.cmake"
        decoy_config: Path = Path("C:/build/RandXConfig.cmake")

        result: Path = select_config_dir(prefix, (decoy_config, selected_config))

        self.assertEqual(result, selected_config.parent.resolve())

    def test_missing_config_keeps_the_requested_prefix_as_the_only_search_location(self) -> None:
        prefix: Path = Path("C:/packages/randx")
        decoy_config: Path = Path("C:/build/RandXConfig.cmake")

        result: Path = select_config_dir(prefix, (decoy_config,))

        self.assertEqual(result, prefix.resolve())

    def test_rejects_multiple_configs_inside_the_requested_prefix(self) -> None:
        prefix: Path = Path("C:/packages/randx")
        configs: tuple[Path, ...] = (
            prefix / "share" / "RandX" / "RandXConfig.cmake",
            prefix / "lib" / "cmake" / "RandX" / "RandXConfig.cmake",
        )

        with self.assertRaisesRegex(ValidationError, "multiple RandXConfig.cmake files"):
            select_config_dir(prefix, configs)


class ConsumerExecutionTests(unittest.TestCase):
    def test_crosscompiling_emulator_reaches_child_configuration(self) -> None:
        prefix: Path = Path("selected-prefix")
        emulator: str = "C:/Program Files/QEMU/qemu.exe;--sysroot;C:/target sysroot"
        args = _parse_args([
            "--consumer-source-dir", "examples/consumer_validation", "--work-dir", "work",
            "--prefix", str(prefix), "--include-root", str(prefix), "--crosscompiling-emulator", emulator,
        ])

        command: list[str] = _configure_command(
            args, build_dir=Path("work/cpp17"), config_dir=prefix, include_root=prefix,
            prefix_path=str(prefix), standard="17",
        )

        self.assertIn(f"-DCMAKE_CROSSCOMPILING_EMULATOR={emulator}", command)

    def test_ctest_command_selects_the_consumer_test_and_configuration(self) -> None:
        args = _parse_args([
            "--consumer-source-dir", "examples/consumer_validation", "--work-dir", "work",
            "--prefix", "selected-prefix", "--include-root", "selected-prefix",
            "--ctest", "C:/Build Tools/ctest.exe", "--configuration", "Release",
        ])

        command: list[str] = _ctest_command(args, build_dir=Path("work/cpp17"))

        self.assertEqual(command[0], "C:/Build Tools/ctest.exe")
        self.assertIn("--output-on-failure", command)
        self.assertIn("-R", command)
        self.assertIn("^randx_consumer_run$", command)
        self.assertEqual(command[command.index("-C") + 1], "Release")

    def test_ctest_defaults_to_the_cmake_sibling(self) -> None:
        tool_directory: Path = Path("build/review-consumers/test-cli").resolve()
        tool_directory.mkdir(parents=True, exist_ok=True)
        cmake_name: str = "cmake.exe" if os.name == "nt" else "cmake"
        ctest_name: str = "ctest.exe" if os.name == "nt" else "ctest"
        cmake_path: Path = tool_directory / cmake_name
        ctest_path: Path = tool_directory / ctest_name
        cmake_path.touch()
        ctest_path.touch()
        args = _parse_args([
            "--cmake", str(cmake_path), "--consumer-source-dir", "examples/consumer_validation",
            "--work-dir", "work", "--prefix", "selected-prefix", "--include-root", "selected-prefix",
        ])

        self.assertEqual(_resolve_ctest(args), str(ctest_path.resolve()))

    def test_main_builds_then_runs_each_standard_through_ctest(self) -> None:
        fixture_root: Path = Path("build/review-consumers/main-flow").resolve()
        fixture_root.mkdir(parents=True, exist_ok=True)
        prefix: Path = fixture_root / "prefix"
        work_dir: Path = fixture_root / "work"
        prefix.mkdir(parents=True, exist_ok=True)
        emulator: str = "python.exe;emulator.py;emulator.log"
        commands: list[list[str]] = []

        def record_command(command: list[str]) -> None:
            commands.append(command)

        with (
            patch("validate_installed_consumers._run", side_effect=record_command),
            patch("validate_installed_consumers.select_config_dir", return_value=prefix / "share" / "RandX"),
            patch("validate_installed_consumers._check_missing_config_fails"),
        ):
            result: int = main([
                "--consumer-source-dir", "examples/consumer_validation", "--work-dir", str(work_dir),
                "--prefix", str(prefix), "--include-root", str(prefix), "--ctest", "ctest.exe",
                "--crosscompiling-emulator", emulator, "--configuration", "Release",
            ])

        self.assertEqual(result, 0)
        configure_commands: list[list[str]] = [command for command in commands if "-S" in command]
        self.assertEqual(len(configure_commands), 2)
        for command in configure_commands:
            self.assertIn(f"-DCMAKE_CROSSCOMPILING_EMULATOR={emulator}", command)

        ctest_commands: list[list[str]] = [command for command in commands if command[0] == "ctest.exe"]
        self.assertEqual(len(ctest_commands), 2)
        for command in ctest_commands:
            self.assertIn("--output-on-failure", command)
            self.assertIn("^randx_consumer_run$", command)
            self.assertEqual(command[command.index("-C") + 1], "Release")


if __name__ == "__main__":
    unittest.main()
