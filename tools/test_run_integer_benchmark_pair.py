"""整数基准配对入口的单元与模拟执行测试。"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path
from typing import Any, Sequence
from unittest.mock import patch

import run_integer_benchmark_pair as pair


class PairDataContractTests(unittest.TestCase):
    def test_msvc_standard_library_is_identified_from_the_compiler_name(self) -> None:
        def unexpected_probe(*args: Any, **kwargs: Any) -> subprocess.CompletedProcess[str]:
            self.fail("MSVC 默认标准库识别不使用 GNU 宏探测参数")

        metadata: dict[str, Any] = pair.standard_library_metadata("cl.exe", 17, {}, unexpected_probe)
        self.assertEqual(metadata["name"], "MSVC STL")
        self.assertEqual(metadata["detection"], "compiler toolchain default")

    @unittest.skipUnless(os.name == "nt", "Windows 基准输出编码")
    def test_native_json_is_normalized_without_losing_original_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path: Path = Path(temporary) / "round.json"
            data: dict[str, Any] = {"context": {"host_name": "café"}, "benchmarks": []}
            content: bytes = json.dumps(data, ensure_ascii=False).encode("mbcs")
            try:
                content.decode("utf-8")
            except UnicodeDecodeError:
                pass
            else:
                self.skipTest("系统本地输出已经使用 UTF-8")
            path.write_bytes(content)
            self.assertEqual(pair.read_benchmark_json(path), data)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), data)
            self.assertEqual(path.with_suffix(".native.json").read_bytes(), content)

    def test_policy_uses_general_group_values(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            policy_path: Path = Path(temporary) / "policy.json"
            policy_path.write_text(
                json.dumps(
                    {
                        "groups": {
                            "general": {"repetitions": 3, "min_time": "0.2s", "tolerance": 0.3}
                        }
                    }
                ),
                encoding="utf-8",
            )
            self.assertEqual(
                pair.load_policy(policy_path),
                {"repetitions": 3, "min_time": "0.2s", "tolerance": 0.3},
            )

    def test_measurement_arguments_use_one_raw_round(self) -> None:
        arguments: list[str] = pair.benchmark_arguments(Path("round one.json"), "0.2s")
        self.assertIn("--benchmark_filter=^BM_Default", arguments)
        self.assertIn("--benchmark_repetitions=1", arguments)
        self.assertIn("--benchmark_min_time=0.2s", arguments)
        self.assertIn("--benchmark_out=round one.json", arguments)

    def test_rounds_alternate_the_first_side(self) -> None:
        self.assertEqual(pair.alternating_sides(1), ("baseline", "candidate"))
        self.assertEqual(pair.alternating_sides(2), ("candidate", "baseline"))
        self.assertEqual(pair.alternating_sides(3), ("baseline", "candidate"))

    def test_compile_arguments_preserve_include_paths_with_spaces(self) -> None:
        entry: dict[str, Any] = {
            "arguments": ["g++", "-I", "C:/source snapshots/candidate", "/I", "C:/other headers"]
        }
        self.assertEqual(
            pair.compile_include_directories(entry),
            ["C:/source snapshots/candidate", "C:/other headers"],
        )

    def test_duplicate_and_missing_raw_names_are_reported_separately(self) -> None:
        row: dict[str, Any] = {
            "name": "BM_DefaultInt",
            "run_type": "iteration",
            "time_unit": "ns",
            "cpu_time": 10.0,
            "real_time": 11.0,
        }
        data: dict[str, Any] = {"context": {}, "benchmarks": [dict(row), dict(row)]}
        with self.assertRaises(pair.BenchmarkPairError) as raised:
            pair.validate_raw_round(data, ["BM_DefaultInt", "BM_DefaultBool"], "cpp23 第 1 轮 baseline")

        self.assertEqual(raised.exception.kind, "data_error")
        self.assertEqual(raised.exception.details["missing"], ["BM_DefaultBool"])
        self.assertEqual(raised.exception.details["duplicates"], [{"name": "BM_DefaultInt", "count": 2}])

    def test_response_files_keep_include_precedence_and_spaces(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory: Path = Path(temporary)
            (directory / "includes CXX.rsp").write_text('-I"C:/snapshot headers" -I"C:/project"', encoding="utf-8")
            (directory / "includes.rsp").write_text('-I"C:/snapshot headers" -I"C:/project"', encoding="utf-8")
            for command_entry in (
                {"command": 'g++ @"includes CXX.rsp" -I"C:/last"'},
                {"command": 'g++ @includes.rsp -I"C:/last"'},
                {"arguments": ["g++", "@includes CXX.rsp", "-I", "C:/last"]},
            ):
                with self.subTest(entry=command_entry):
                    entry: dict[str, Any] = {"directory": str(directory), **command_entry}
                    self.assertEqual(pair.compile_include_directories(entry), ["C:/snapshot headers", "C:/project", "C:/last"])

    def test_aggregate_rows_do_not_count_as_duplicate_raw_measurements(self) -> None:
        raw: dict[str, Any] = {
            "name": "BM_DefaultInt",
            "run_type": "iteration",
            "time_unit": "ns",
            "cpu_time": 10.0,
            "real_time": 11.0,
        }
        aggregate: dict[str, Any] = {
            "name": "BM_DefaultInt_median",
            "run_name": "BM_DefaultInt",
            "run_type": "aggregate",
            "aggregate_name": "median",
            "time_unit": "ns",
            "cpu_time": 10.0,
            "real_time": 11.0,
        }
        validation: dict[str, Any] = pair.validate_raw_round(
            {"context": {}, "benchmarks": [raw, aggregate]},
            ["BM_DefaultInt"],
            "cpp23 第 1 轮 baseline",
        )
        self.assertEqual(validation["raw_run_count"], 1)
        self.assertEqual(validation["names"], ["BM_DefaultInt"])

    def test_google_benchmark_measurement_errors_are_classified(self) -> None:
        failed: dict[str, Any] = {
            "name": "BM_DefaultInt",
            "error_occurred": True,
            "error_message": "benchmark setup failed",
        }
        with self.assertRaises(pair.BenchmarkPairError) as raised:
            pair.validate_raw_round({"context": {}, "benchmarks": [failed]}, ["BM_DefaultInt"], "cpp17 baseline")
        self.assertEqual(raised.exception.kind, "measurement_error")
        self.assertEqual(
            raised.exception.details["measurement_errors"],
            [{"name": "BM_DefaultInt", "message": "benchmark setup failed"}],
        )

    def test_all_four_builds_must_enumerate_the_same_names(self) -> None:
        names: dict[str, list[str]] = {
            "cpp23/baseline": ["BM_DefaultInt"],
            "cpp23/candidate": ["BM_DefaultInt"],
            "cpp17/baseline": ["BM_DefaultInt"],
            "cpp17/candidate": ["BM_DefaultBool"],
        }
        with self.assertRaises(pair.BenchmarkPairError) as raised:
            pair.verify_expected_sets(names)
        self.assertEqual(raised.exception.kind, "data_error")
        self.assertEqual(raised.exception.details["differences"]["cpp17/candidate"]["missing"], ["BM_DefaultInt"])
        self.assertEqual(raised.exception.details["differences"]["cpp17/candidate"]["extra"], ["BM_DefaultBool"])


class BuildOutputOverrideTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root: Path = Path(self.temporary.name)
        self.build_directory: Path = self.root / "build directory"
        self.build_directory.mkdir()
        self.snapshot: Path = self.build_directory / "snapshot headers"
        self.shadow: Path = self.build_directory / "shadow headers"
        self.snapshot.mkdir()
        self.shadow.mkdir()
        for directory in (self.snapshot, self.shadow):
            (directory / "RandX.hpp").write_text("// header\n", encoding="utf-8")
        self.source_directory: Path = self.root / "source directory"
        self.source_directory.mkdir()
        self.source: Path = self.source_directory / "benchmark_gbench.cpp"
        self.source.touch()
        self.default_source: Path = self.source_directory / "benchmark_gbench_default_cpp23.cpp"
        self.default_source.touch()
        self.compiler: Path = self.root / "compiler tools" / "cl.exe"
        self.compiler.parent.mkdir()
        self.compiler.touch()

    def compile_command(
        self,
        include_directories: Sequence[Path],
        target: str = "benchmark_gbench",
        source: Path | None = None,
    ) -> str:
        includes: str = " ".join(f'/I"{directory}"' for directory in include_directories)
        source_path: Path = source or self.source
        object_path: Path = self.build_directory / "CMakeFiles" / f"{target}.dir" / f"{source_path.name}.obj"
        return f'"{self.compiler}" {includes} /Fo"{object_path}" /c "{source_path}"'

    def complete_compile_output(self, include_directories: Sequence[Path]) -> str:
        return "\n".join(
            (
                self.compile_command(include_directories),
                self.compile_command(include_directories, source=self.default_source),
            )
        )

    def verify(self, build_output: str) -> dict[str, Any]:
        return pair.verify_build_output_override(
            build_output,
            self.build_directory,
            self.snapshot,
            "RandX.hpp",
            "benchmark_gbench",
            str(self.compiler),
        )

    def test_shadow_include_before_snapshot_is_rejected(self) -> None:
        with self.assertRaises(pair.BenchmarkPairError) as raised:
            self.verify(self.compile_command([self.shadow, self.snapshot]))

        self.assertEqual(raised.exception.kind, "data_error")
        self.assertEqual(raised.exception.details["first_header_directory"], str(self.shadow.resolve()))

    def test_msvc_command_with_spaces_and_correct_include_order_is_verified(self) -> None:
        evidence: dict[str, Any] = self.verify(self.complete_compile_output([self.snapshot]))

        self.assertTrue(evidence["verified"])
        self.assertEqual(evidence["selected_directory"], str(self.snapshot.resolve()))
        self.assertEqual(len(evidence["commands"]), 2)
        self.assertTrue(all("compiler tools" in row["compile_command"] for row in evidence["commands"]))

    def test_msvc_verbose_output_checks_the_second_translation_unit(self) -> None:
        output: str = "\n".join(
            (
                self.compile_command([self.snapshot]),
                self.compile_command([self.shadow, self.snapshot], source=self.default_source),
            )
        )

        with self.assertRaises(pair.BenchmarkPairError) as raised:
            self.verify(output)

        self.assertEqual(raised.exception.details["source"], self.default_source.name)
        self.assertEqual(raised.exception.details["first_header_directory"], str(self.shadow.resolve()))

    def test_compile_database_requires_both_cpp23_translation_units(self) -> None:
        compile_commands: Path = self.build_directory / "compile_commands.json"
        pair.write_json(
            compile_commands,
            [
                {
                    "directory": str(self.build_directory),
                    "file": str(self.source),
                    "command": self.compile_command([self.snapshot]),
                }
            ],
        )

        with self.assertRaises(pair.BenchmarkPairError) as raised:
            pair.verify_compile_database_override(
                compile_commands,
                self.snapshot,
                "RandX.hpp",
                "benchmark_gbench",
            )

        self.assertEqual(
            raised.exception.details["missing_sources"],
            ["benchmark_gbench_default_cpp23.cpp"],
        )

    def test_compile_database_records_include_selection_for_each_cpp23_source(self) -> None:
        compile_commands: Path = self.build_directory / "compile_commands.json"
        entries: list[dict[str, Any]] = []
        for source in (self.source, self.default_source):
            command: str = self.compile_command([self.snapshot], source=source)
            entries.append(
                {
                    "directory": str(self.build_directory),
                    "file": str(source),
                    "command": command,
                }
            )
        pair.write_json(compile_commands, entries)

        evidence: dict[str, Any] = pair.verify_compile_database_override(
            compile_commands,
            self.snapshot,
            "RandX.hpp",
            "benchmark_gbench",
        )

        self.assertEqual(len(evidence["commands"]), 2)
        self.assertEqual(
            {Path(str(row["source"])).name for row in evidence["commands"]},
            {self.source.name, self.default_source.name},
        )

    def test_compile_database_checks_second_translation_unit_include_order(self) -> None:
        compile_commands: Path = self.build_directory / "compile_commands.json"
        entries: list[dict[str, Any]] = []
        for source, include_directories in (
            (self.source, [self.snapshot]),
            (self.default_source, [self.shadow, self.snapshot]),
        ):
            entries.append(
                {
                    "directory": str(self.build_directory),
                    "file": str(source),
                    "command": self.compile_command(include_directories, source=source),
                }
            )
        pair.write_json(compile_commands, entries)

        with self.assertRaises(pair.BenchmarkPairError) as raised:
            pair.verify_compile_database_override(
                compile_commands,
                self.snapshot,
                "RandX.hpp",
                "benchmark_gbench",
            )

        self.assertEqual(Path(str(raised.exception.details["source"])).name, self.default_source.name)
        self.assertEqual(raised.exception.details["first_header_directory"], str(self.shadow.resolve()))

    def test_other_target_paths_in_the_log_do_not_count_as_target_evidence(self) -> None:
        unrelated_command: str = self.compile_command([self.shadow], "dependent_benchmark_gbench")
        target_compile_output: str = self.complete_compile_output([self.snapshot])
        output: str = f"configured snapshot: {self.snapshot}\n{unrelated_command}\n{target_compile_output}"

        evidence: dict[str, Any] = self.verify(output)

        self.assertEqual(len(evidence["commands"]), 2)
        self.assertEqual(evidence["commands"][0]["selected_directory"], str(self.snapshot.resolve()))

    def test_other_target_compiling_the_same_source_cannot_verify_the_snapshot(self) -> None:
        with self.assertRaises(pair.BenchmarkPairError) as raised:
            self.verify(self.compile_command([self.snapshot], "dependent_benchmark_gbench"))
        self.assertFalse(raised.exception.details["target_compile_command_found"])

    def test_snapshot_path_without_a_target_compile_command_is_not_verified(self) -> None:
        output: str = (
            f"configured snapshot: {self.snapshot}\n"
            f'"{self.compiler}" /I"{self.snapshot}" /c "{self.source}"'
        )

        with self.assertRaises(pair.BenchmarkPairError) as raised:
            self.verify(output)

        self.assertEqual(raised.exception.kind, "data_error")
        self.assertFalse(raised.exception.details["target_compile_command_found"])

    def test_response_file_is_resolved_from_build_directory_and_keeps_include_order(self) -> None:
        response_file: Path = self.build_directory / "compile options.rsp"
        relative_snapshot: str = "snapshot headers"
        relative_object: str = "CMakeFiles/benchmark_gbench.dir/benchmark_gbench.cpp.obj"
        relative_source: str = "../source directory/benchmark_gbench.cpp"
        response_file.write_text(
            f'/I"{relative_snapshot}" /Fo"{relative_object}" /c "{relative_source}"',
            encoding="utf-8",
        )
        command: str = "\n".join(
            (
                f'"{self.compiler}" @"{response_file.name}"',
                self.compile_command([self.snapshot], source=self.default_source),
            )
        )

        evidence: dict[str, Any] = self.verify(command)

        self.assertEqual(evidence["commands"][0]["selected_directory"], str(self.snapshot.resolve()))

    def test_unreadable_target_response_file_is_a_data_error(self) -> None:
        command: str = (
            f'"{self.compiler}" /Fo"CMakeFiles/benchmark_gbench.dir/benchmark_gbench.cpp.obj" '
            f'/c "{self.source}" @"missing options.rsp"'
        )

        with self.assertRaises(pair.BenchmarkPairError) as raised:
            self.verify(command)

        self.assertEqual(raised.exception.kind, "data_error")
        self.assertEqual(raised.exception.scope, "benchmark_gbench")

    def test_cpp17_entry_verifies_its_header_and_rejects_shadow_precedence(self) -> None:
        target: str = "benchmark_gbench_default_cpp17"
        header: str = "RandX_Cpp17.hpp"
        source: Path = self.source_directory / "benchmark_gbench_default_cpp17.cpp"
        source.touch()
        for directory in (self.snapshot, self.shadow):
            (directory / header).write_text("// header\n", encoding="utf-8")
        object_path: Path = self.build_directory / "CMakeFiles" / f"{target}.dir" / "benchmark_gbench_default_cpp17.cpp.obj"
        for include_directories, expected_success in (
            ([self.snapshot], True),
            ([self.shadow, self.snapshot], False),
        ):
            with self.subTest(include_directories=include_directories):
                includes: str = " ".join(f'/I"{directory}"' for directory in include_directories)
                command: str = f'"{self.compiler}" {includes} /Fo"{object_path}" /c "{source}"'
                if expected_success:
                    evidence: dict[str, Any] = pair.verify_build_output_override(
                        command, self.build_directory, self.snapshot, header, target, str(self.compiler)
                    )
                    self.assertTrue(evidence["verified"])
                    self.assertEqual(evidence["commands"][0]["source"], source.name)
                else:
                    with self.assertRaises(pair.BenchmarkPairError) as raised:
                        pair.verify_build_output_override(
                            command, self.build_directory, self.snapshot, header, target, str(self.compiler)
                        )
                    self.assertEqual(raised.exception.details["first_header_directory"], str(self.shadow.resolve()))

    def test_msvc_utf16_response_file_preserves_header_selection(self) -> None:
        response_file: Path = self.build_directory / "MSVC options.rsp"
        response_file.write_text(
            f'/I"{self.snapshot}" /Fo"CMakeFiles/benchmark_gbench.dir/benchmark_gbench.cpp.obj" /c "{self.source}"',
            encoding="utf-16",
        )
        build_output: str = "\n".join(
            (
                f'"{self.compiler}" @"{response_file.name}"',
                self.compile_command([self.snapshot], source=self.default_source),
            )
        )
        evidence: dict[str, Any] = self.verify(build_output)
        self.assertTrue(evidence["verified"])


class SimulatedPairRunTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root: Path = Path(self.temporary.name)
        self.baseline_headers: Path = self.root / "baseline-headers"
        self.candidate_headers: Path = self.root / "candidate-headers"
        for header_directory in (self.baseline_headers, self.candidate_headers):
            header_directory.mkdir()
            for header_name in pair.HEADER_NAMES:
                (header_directory / header_name).write_text("// simulated snapshot\n", encoding="utf-8")

        self.compiler: Path = self.root / "g++"
        self.cmake: Path = self.root / "cmake"
        self.compiler.touch()
        self.cmake.touch()
        self.policy: Path = self.root / "policy.json"
        self.policy.write_text(
            json.dumps(
                {
                    "groups": {
                        "general": {"repetitions": 2, "min_time": "0.01s", "tolerance": 0.25}
                    }
                }
            ),
            encoding="utf-8",
        )
        self.build_root: Path = self.root / "build"
        self.output_root: Path = self.root / "output"
        self.regression: bool = False
        self.omit_case: bool = False
        self.duplicate_case: bool = False
        self.measurement_calls: dict[tuple[str, str], int] = {}

    def arguments(self) -> Namespace:
        return Namespace(
            baseline_header_dir=self.baseline_headers,
            candidate_header_dir=self.candidate_headers,
            compiler=str(self.compiler),
            cmake=str(self.cmake),
            build_root=self.build_root,
            output_dir=self.output_root,
            policy=self.policy,
            generator="MinGW Makefiles",
            configuration="Release",
            baseline_cmake_arg=["-DRANDX_BUILD_TESTS=OFF"],
            candidate_cmake_arg=["-DRANDX_BUILD_TESTS=OFF"],
        )

    def fake_run(self, command: Sequence[str], **kwargs: Any) -> subprocess.CompletedProcess[str]:
        arguments: list[str] = list(command)
        executable: str = arguments[0]
        if executable == str(self.compiler.resolve()) and "--version" in arguments:
            return subprocess.CompletedProcess(arguments, 0, "GNU C++ simulated version\n", "")
        if executable == str(self.compiler.resolve()) and "-dM" in arguments:
            return subprocess.CompletedProcess(arguments, 0, "#define __GLIBCXX__ 20261003\n", "")
        if executable == str(self.cmake.resolve()) and "--build" in arguments:
            build_directory: Path = Path(arguments[arguments.index("--build") + 1])
            target: str = arguments[arguments.index("--target") + 1]
            suffix: str = ".exe" if os.name == "nt" else ""
            binary: Path = build_directory / f"{target}{suffix}"
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.write_text("simulated executable\n", encoding="utf-8")
            return subprocess.CompletedProcess(arguments, 0, "verbose compiler command\n", "")
        if executable == str(self.cmake.resolve()) and "-S" in arguments:
            build_directory = Path(arguments[arguments.index("-B") + 1])
            header_argument: str = next(item for item in arguments if item.startswith("-DRANDX_SAMPLING_HEADER_DIR="))
            header_directory = Path(header_argument.split("=", 1)[1])
            standard_argument: str = next(item for item in arguments if item.startswith("-DCMAKE_CXX_STANDARD="))
            standard: str = standard_argument.split("=", 1)[1]
            build_directory.mkdir(parents=True, exist_ok=True)
            cache_lines: list[str] = [
                f"CMAKE_GENERATOR:INTERNAL={arguments[arguments.index('-G') + 1]}",
                f"CMAKE_CXX_COMPILER:FILEPATH={self.compiler}",
                "CMAKE_CXX_COMPILER_ID:STRING=GNU",
                "CMAKE_CXX_COMPILER_VERSION:STRING=1.0",
                f"CMAKE_CXX_STANDARD:STRING={standard}",
                "CMAKE_BUILD_TYPE:STRING=Release",
                "CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG",
                f"RANDX_SAMPLING_HEADER_DIR:PATH={header_directory}",
            ]
            (build_directory / "CMakeCache.txt").write_text("\n".join(cache_lines) + "\n", encoding="utf-8")
            target: str = next(
                configured_target
                for _, configured_standard, configured_target in pair.STANDARD_CONFIGURATIONS
                if str(configured_standard) == standard
            )
            compile_database: list[dict[str, Any]] = []
            for source_name in pair.BENCHMARK_TARGET_SOURCES[target]:
                source_file: Path = pair.PROJECT_ROOT / source_name
                compile_database.append(
                    {
                        "directory": str(pair.PROJECT_ROOT),
                        "file": str(source_file),
                        "arguments": [
                            str(self.compiler),
                            "-I",
                            str(header_directory),
                            "-I",
                            str(pair.PROJECT_ROOT),
                            "-O3",
                            "-std=c++17" if standard == "17" else "-std=c++23",
                            "-c",
                            str(source_file),
                        ],
                    }
                )
            pair.write_json(build_directory / "compile_commands.json", compile_database)
            return subprocess.CompletedProcess(arguments, 0, "configured\n", "")

        binary_path: Path = Path(executable)
        if binary_path.is_file() and pair.BENCHMARK_LIST_FLAG in arguments:
            return subprocess.CompletedProcess(
                arguments,
                0,
                "BM_DefaultInt\nBM_DefaultInt/1024\n",
                "",
            )
        if binary_path.is_file() and any(item.startswith("--benchmark_out=") for item in arguments):
            output_argument: str = next(item for item in arguments if item.startswith("--benchmark_out="))
            output_path: Path = Path(output_argument.split("=", 1)[1])
            if not output_path.parent.is_dir():
                return subprocess.CompletedProcess(arguments, 1, "", "invalid file name")
            side: str = binary_path.parent.name
            standard: str = binary_path.parent.parent.name
            call_key: tuple[str, str] = (standard, side)
            round_index: int = self.measurement_calls.get(call_key, 0)
            self.measurement_calls[call_key] = round_index + 1
            baseline_value: float = 100.0 if round_index == 0 else 120.0
            candidate_value: float = 0.121 if round_index == 0 else 0.099
            if self.regression and side == "candidate":
                candidate_value = 0.143
            value: float = baseline_value if side == "baseline" else candidate_value
            unit: str = "ns" if side == "baseline" else "us"
            names: list[str] = ["BM_DefaultInt", "BM_DefaultInt/1024"]
            if self.omit_case and side == "baseline" and round_index == 0:
                names.remove("BM_DefaultInt/1024")
            rows: list[dict[str, Any]] = [
                {
                    "name": name,
                    "run_type": "iteration",
                    "time_unit": unit,
                    "iterations": 100,
                    "cpu_time": value,
                    "real_time": value * 1.1,
                }
                for name in names
            ]
            if self.duplicate_case and side == "baseline" and round_index == 0:
                rows.append(dict(rows[0]))
            rows.append(
                {
                    "name": "BM_DefaultInt_median",
                    "run_name": "BM_DefaultInt",
                    "run_type": "aggregate",
                    "aggregate_name": "median",
                    "time_unit": unit,
                    "cpu_time": value,
                    "real_time": value * 1.1,
                }
            )
            pair.write_json(output_path, {"context": {"simulated": True}, "benchmarks": rows})
            return subprocess.CompletedProcess(arguments, 0, "measured\n", "")
        return subprocess.CompletedProcess(arguments, 0, "", "")

    def run_pair(self) -> tuple[int, dict[str, Any]]:
        with patch.object(pair, "disassembler_path", return_value=None):
            return pair._run_pair(self.arguments(), self.fake_run)

    def test_full_run_records_builds_rounds_order_and_unit_normalized_comparisons(self) -> None:
        exit_code: int
        report: dict[str, Any]
        exit_code, report = self.run_pair()

        self.assertEqual(exit_code, 0)
        self.assertEqual(report["status"], "passed")
        self.assertEqual(len(report["builds"]), 4)
        self.assertEqual(report["policy"]["repetitions"], 2)
        self.assertEqual(report["standards"]["cpp17"]["target"], "benchmark_gbench_default_cpp17")
        self.assertEqual(len(report["run_order"]), 8)
        self.assertEqual(
            [(row["round"], row["side"]) for row in report["run_order"][:4]],
            [(1, "baseline"), (1, "candidate"), (2, "candidate"), (2, "baseline")],
        )
        cpp23_comparison: dict[str, Any] = report["standards"]["cpp23"]["comparison"]
        self.assertAlmostEqual(cpp23_comparison["items"][0]["baseline_ms"], 0.00011)
        self.assertAlmostEqual(cpp23_comparison["items"][0]["current_ms"], 0.00011)
        self.assertFalse(cpp23_comparison["items"][0]["regression"])
        self.assertEqual(
            report["builds"]["cpp23/baseline"]["override_evidence"]["selected_directory"],
            str(self.baseline_headers.resolve()),
        )
        cpp23_override: dict[str, Any] = report["builds"]["cpp23/baseline"]["override_evidence"]
        self.assertEqual(
            {Path(str(row["source"])).name for row in cpp23_override["commands"]},
            set(pair.BENCHMARK_TARGET_SOURCES["benchmark_gbench"]),
        )
        cpp17_build: dict[str, Any] = report["builds"]["cpp17/candidate"]
        self.assertEqual(cpp17_build["standard_library"]["name"], "libstdc++")
        self.assertEqual(cpp17_build["optimization_and_lto_options"]["build_type"], "Release")
        baseline_command: list[str] = report["builds"]["cpp23/baseline"]["configure_command"]
        candidate_command: list[str] = report["builds"]["cpp23/candidate"]["configure_command"]
        self.assertIn("-DRANDX_BUILD_TESTS=OFF", baseline_command)
        self.assertIn("-DRANDX_BUILD_TESTS=OFF", candidate_command)

        output_directory: Path = Path(report["output_directory"])
        self.assertTrue((output_directory / "report.json").is_file())
        self.assertTrue((output_directory / "report.md").is_file())
        self.assertTrue((output_directory / "cpp23" / "rounds" / "baseline-1.json").is_file())
        self.assertTrue((output_directory / "cpp17" / "rounds" / "candidate-2.json").is_file())
        markdown: str = (output_directory / "report.md").read_text(encoding="utf-8")
        self.assertIn("C++23 基准", markdown)
        self.assertIn("实际测量顺序", markdown)

    def test_different_build_options_are_rejected_before_building(self) -> None:
        arguments: Namespace = self.arguments()
        arguments.candidate_cmake_arg = ["-DCMAKE_CXX_FLAGS=-O0"]
        exit_code, report = pair._run_pair(arguments, self.fake_run)
        self.assertEqual(exit_code, 2)
        self.assertEqual(report["errors"][0]["scope"], "configuration")
        self.assertEqual(report["builds"], {})

    def test_missing_round_data_exits_two_and_reports_missing_name(self) -> None:
        self.omit_case = True
        exit_code: int
        report: dict[str, Any]
        exit_code, report = self.run_pair()

        self.assertEqual(exit_code, 2)
        self.assertEqual(report["status"], "error")
        self.assertEqual(report["errors"][0]["kind"], "data_error")
        self.assertEqual(report["errors"][0]["details"]["missing"], ["BM_DefaultInt/1024"])

    def test_same_round_duplicate_measurement_is_classified_as_duplicate(self) -> None:
        self.duplicate_case = True
        exit_code: int
        report: dict[str, Any]
        exit_code, report = self.run_pair()

        self.assertEqual(exit_code, 2)
        details: dict[str, Any] = report["errors"][0]["details"]
        self.assertEqual(details["missing"], [])
        self.assertEqual(details["duplicates"], [{"name": "BM_DefaultInt", "count": 2}])

    def test_regression_exits_one_using_policy_tolerance(self) -> None:
        self.regression = True
        exit_code: int
        report: dict[str, Any]
        exit_code, report = self.run_pair()

        self.assertEqual(exit_code, 1)
        self.assertEqual(report["status"], "regression")
        self.assertTrue(report["standards"]["cpp23"]["comparison"]["regressions"])


if __name__ == "__main__":
    unittest.main()
