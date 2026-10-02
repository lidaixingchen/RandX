"""验证分组测量的同机顺序、完整清单与归档契约。"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any, Sequence
from unittest.mock import patch

import run_benchmark_group

TEST_CACHE_LINE_BYTES: int = 64

def group_config(group: str) -> dict[str, Any]:
    if group == "general":
        return {
            "kind": "general", "standard": 23, "target": "benchmark_gbench",
            "repetitions": 5, "min_time": "0.5s", "tolerance": 0.25,
        }
    standard: int = 17 if group == "sampling_cpp17" else 23
    return {
        "kind": "sampling", "standard": standard,
        "target": f"benchmark_gbench_{group}", "repetitions": 5,
        "min_time": "0.1s", "tolerance": 0.25,
        "confirmation_repetitions": 6, "confirmation_min_time": "1s",
    }


def plan_value() -> dict[str, Any]:
    return {
        "policy": {
            "groups": {
                "general": group_config("general"),
                "sampling_cpp17": group_config("sampling_cpp17"),
                "sampling_cpp23": group_config("sampling_cpp23"),
            }
        }
    }


def write_benchmark_result(path: Path, run_name: str, value: float, aggregate: bool) -> None:
    entry: dict[str, Any] = {
        "name": f"{run_name}_median" if aggregate else run_name,
        "cpu_time": value,
        "real_time": value,
        "time_unit": "ns",
    }
    if aggregate:
        entry.update({"run_name": run_name, "run_type": "aggregate", "aggregate_name": "median"})
    else:
        entry["run_type"] = "iteration"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"context": {}, "benchmarks": [entry]}), encoding="utf-8")


class BuildCommandTests(unittest.TestCase):
    def test_general_baseline_uses_historical_cmake_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            configuration: dict[str, Any] = group_config("general")
            configured: tuple[list[str], Path, Path] = run_benchmark_group.cmake_configure_command(
                root, "general", "baseline", configuration
            )
            command, source, build = configured
            self.assertEqual(source, root / "baseline-src")
            self.assertEqual(build, root / "baseline-src" / "build" / "general-baseline")
            self.assertEqual(command[command.index("-S") + 1], str(source))
            self.assertIn("-DCMAKE_CXX_STANDARD=23", command)
            self.assertIn("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", command)
            self.assertIn(f"-DFETCHCONTENT_BASE_DIR={root / 'build' / '_deps'}", command)

    def test_sampling_variants_use_one_source_shared_dependencies_and_one_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            configuration: dict[str, Any] = group_config("sampling_cpp17")
            command, source, build = run_benchmark_group.cmake_configure_command(
                root, "sampling_cpp17", "baseline", configuration
            )
            self.assertEqual(source, root)
            self.assertEqual(build, root / "build" / "sampling" / "sampling_cpp17-baseline")
            self.assertIn(f"-DRANDX_SAMPLING_HEADER_DIR={root / 'sampling-baseline-src'}", command)
            self.assertIn(f"-DFETCHCONTENT_BASE_DIR={root / 'build' / '_deps'}", command)
            self.assertEqual(configuration["target"], "benchmark_gbench_sampling_cpp17")

    def test_sampling_variants_use_the_same_cache_line_alignment(self) -> None:
        root: Path = Path("workspace")
        for variant in ("candidate", "baseline"):
            command, _, _ = run_benchmark_group.cmake_configure_command(
                root, "sampling_cpp17", variant, group_config("sampling_cpp17"), TEST_CACHE_LINE_BYTES,
            )
            self.assertIn(
                f"-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -falign-loops={TEST_CACHE_LINE_BYTES}", command
            )

    def test_cache_line_size_comes_from_l1_instruction_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            for index, cache_type in enumerate(("Data", "Instruction", "Unified")):
                cache: Path = root / "cpu3" / "cache" / f"index{index}"
                cache.mkdir(parents=True)
                (cache / "level").write_text("1" if cache_type != "Unified" else "2", encoding="utf-8")
                (cache / "type").write_text(cache_type, encoding="utf-8")
                line_size: int = TEST_CACHE_LINE_BYTES if cache_type == "Instruction" else TEST_CACHE_LINE_BYTES * 2
                (cache / "coherency_line_size").write_text(str(line_size), encoding="utf-8")
            self.assertEqual(run_benchmark_group.instruction_cache_line_bytes(3, root), TEST_CACHE_LINE_BYTES)

    def test_missing_instruction_cache_information_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaisesRegex(RuntimeError, "一级指令缓存行"):
                run_benchmark_group.instruction_cache_line_bytes(3, Path(temporary_directory))

    def test_measurement_arguments_use_policy_and_never_add_a_filter(self) -> None:
        configuration: dict[str, Any] = group_config("sampling_cpp23")
        arguments: list[str] = run_benchmark_group.benchmark_arguments(
            configuration, Path("round.json"), repetitions=1
        )
        self.assertIn("--benchmark_repetitions=1", arguments)
        self.assertIn("--benchmark_min_time=0.1s", arguments)
        self.assertIn("--benchmark_format=json", arguments)
        self.assertFalse(any(argument.startswith("--benchmark_filter") for argument in arguments))

    def test_build_invokes_only_its_configured_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            plan_path: Path = root / "plan.json"
            plan_path.write_text(json.dumps(plan_value()), encoding="utf-8")
            directory: Path = root / "groups" / "sampling_cpp17"
            directory.mkdir(parents=True)
            (directory / "environment.json").write_text(
                json.dumps({"loop_alignment": TEST_CACHE_LINE_BYTES}), encoding="utf-8"
            )
            calls: list[list[str]] = []

            def fake_run_logged(command: Sequence[str], log_path: Path, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
                calls.append(list(command))
                return subprocess.CompletedProcess(list(command), 0, "", "")

            with patch.dict(os.environ, {"GITHUB_WORKSPACE": str(root)}), patch.object(
                run_benchmark_group, "run_logged", side_effect=fake_run_logged
            ), patch.object(run_benchmark_group, "first_line", return_value="tool version"), patch.object(
                run_benchmark_group, "read_cmake_generator", return_value="Unix Makefiles"
            ), patch.object(run_benchmark_group, "actual_commit", return_value="baseline-commit"), patch.object(
                run_benchmark_group, "save_build_diagnostics"
            ) as save_diagnostics:
                run_benchmark_group.build(plan_path, "sampling_cpp17", "baseline")

            self.assertEqual(len(calls), 2)
            self.assertEqual(calls[1][calls[1].index("--target") + 1], "benchmark_gbench_sampling_cpp17")
            self.assertEqual(calls[1].count("--target"), 1)
            resolved_root: Path = root.resolve()
            build_directory: Path = resolved_root / "build" / "sampling" / "sampling_cpp17-baseline"
            save_diagnostics.assert_called_once_with(
                build_directory, build_directory / "benchmark_gbench_sampling_cpp17",
                resolved_root / "groups" / "sampling_cpp17", "baseline",
            )

    def test_build_diagnostics_preserve_commands_and_linked_disassembly(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            build: Path = root / "build"
            build.mkdir()
            commands: str = '[{"command": "g++ -O3 -DNDEBUG benchmark.cpp"}]\n'
            (build / "compile_commands.json").write_text(commands, encoding="utf-8")
            binary: Path = build / "benchmark_gbench"

            def fake_disassemble(command: Sequence[str], **kwargs: Any) -> subprocess.CompletedProcess[str]:
                self.assertEqual(list(command), ["objdump", "-d", "-C", str(binary)])
                self.assertTrue(kwargs["check"])
                kwargs["stdout"].write("<benchmark_loop>:\n ret\n")
                return subprocess.CompletedProcess(list(command), 0, "", "")

            with patch.object(run_benchmark_group.subprocess, "run", side_effect=fake_disassemble):
                run_benchmark_group.save_build_diagnostics(build, binary, root / "group", "candidate")
            diagnostics: Path = root / "group" / "compiler" / "candidate"
            self.assertEqual((diagnostics / "compile_commands.json").read_text(encoding="utf-8"), commands)
            self.assertIn("<benchmark_loop>", (diagnostics / "disassembly.txt").read_text(encoding="utf-8"))

    def test_disassembly_failure_propagates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            (root / "compile_commands.json").write_text("[]", encoding="utf-8")
            with patch.object(
                run_benchmark_group.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "objdump")
            ), self.assertRaises(subprocess.CalledProcessError):
                run_benchmark_group.save_build_diagnostics(root, root / "binary", root / "group", "baseline")


class SamplingMeasurementTests(unittest.TestCase):
    def test_sampling_rounds_alternate_both_versions_and_validate_every_round(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            plan_path: Path = root / "plan.json"
            plan_path.write_text(json.dumps(plan_value()), encoding="utf-8")
            directory: Path = root / "groups" / "sampling_cpp17"
            directory.mkdir(parents=True)
            (directory / "expected.json").write_text(
                json.dumps({"candidate": ["case/range_size:10/request:5"], "baseline": ["case/range_size:10/request:5"]}),
                encoding="utf-8",
            )
            measurement_order: list[str] = []

            def fake_run_logged(
                command: Sequence[str], log_path: Path, cwd: Path | None = None,
            ) -> subprocess.CompletedProcess[str]:
                command_values: list[str] = list(command)
                binary: str = command_values[3]
                output_argument: str = next(value for value in command_values if value.startswith("--benchmark_out="))
                output_path: Path = Path(output_argument.partition("=")[2])
                variant: str = "candidate" if "-candidate" in binary else "baseline"
                measurement_order.append(variant)
                write_benchmark_result(output_path, "case/range_size:10/request:5", 1.0, aggregate=False)
                return subprocess.CompletedProcess(command_values, 0, "", "")

            with patch.dict(os.environ, {"GITHUB_WORKSPACE": str(root)}), patch.object(
                run_benchmark_group, "sampling_cpu", return_value=6
            ), patch.object(run_benchmark_group, "run_logged", side_effect=fake_run_logged):
                run_benchmark_group.measure_sampling_rounds(plan_path, "sampling_cpp17")

            self.assertEqual(measurement_order, ["baseline", "candidate", "candidate", "baseline"] * 2 + ["baseline", "candidate"])
            raw_results: list[Path] = sorted((directory / "rounds").glob("*.json"))
            self.assertEqual(len(raw_results), 10)
            self.assertEqual(json.loads((directory / "environment.json").read_text(encoding="utf-8"))["cpu"], 6)

    def test_cpu_binding_uses_a_cpu_from_the_available_affinity_set(self) -> None:
        with patch.object(run_benchmark_group.os, "sched_getaffinity", return_value={8, 3}, create=True):
            self.assertEqual(run_benchmark_group.sampling_cpu(), 3)
        with patch.object(run_benchmark_group.os, "sched_getaffinity", return_value=set(), create=True):
            with self.assertRaisesRegex(RuntimeError, "未提供可用 CPU"):
                run_benchmark_group.sampling_cpu()

    def test_confirmation_copies_metadata_and_raw_rounds_to_gate_paths(self) -> None:
        case: str = "family/range_size:10/request:5"
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            plan_path: Path = root / "plan.json"
            plan_path.write_text(json.dumps(plan_value()), encoding="utf-8")
            directory: Path = root / "groups" / "sampling_cpp23"
            directory.mkdir(parents=True)
            (directory / "expected.json").write_text(
                json.dumps({"candidate": [case], "baseline": [case]}), encoding="utf-8"
            )
            (directory / "environment.json").write_text(json.dumps({"cpu": 6}), encoding="utf-8")
            write_benchmark_result(directory / "candidate-initial.json", case, 2.0, aggregate=True)
            write_benchmark_result(directory / "baseline-initial.json", case, 1.0, aggregate=True)

            def fake_run_logged(
                command: Sequence[str], log_path: Path, cwd: Path | None = None,
            ) -> subprocess.CompletedProcess[str]:
                command_values: list[str] = list(command)
                output_directory: Path = Path(command_values[command_values.index("--output-dir") + 1])
                output_directory.mkdir(parents=True, exist_ok=True)
                runs: list[dict[str, Any]] = []
                for round_number in range(1, 7):
                    for variant in ("baseline", "candidate"):
                        runs.append({"variant": variant, "round": round_number, "returncode": 0})
                        write_benchmark_result(
                            output_directory / f"{variant}-{round_number}.json", case, 1.0, aggregate=False
                        )
                        (output_directory / f"{variant}-{round_number}.stdout.txt").write_text("stdout", encoding="utf-8")
                        (output_directory / f"{variant}-{round_number}.stderr.txt").write_text("stderr", encoding="utf-8")
                write_benchmark_result(output_directory / "candidate.json", case, 1.0, aggregate=True)
                write_benchmark_result(output_directory / "baseline.json", case, 1.0, aggregate=True)
                (output_directory / "metadata.json").write_text(
                    json.dumps({
                        "complete": True,
                        "cases": [case],
                        "repetitions": 6,
                        "min_time": "1s",
                        "tolerance": 0.25,
                        "cpu": 6,
                        "runs": runs,
                    }),
                    encoding="utf-8",
                )
                return subprocess.CompletedProcess(command_values, 0, "done", "")

            with patch.dict(os.environ, {"GITHUB_WORKSPACE": str(root)}), patch.object(
                run_benchmark_group, "run_logged", side_effect=fake_run_logged
            ):
                run_benchmark_group.confirm_sampling(plan_path, "sampling_cpp23")

            confirmation: Path = directory / "confirmation"
            self.assertTrue((confirmation / "metadata.json").is_file())
            self.assertTrue((confirmation / "candidate.json").is_file())
            self.assertTrue((confirmation / "baseline.json").is_file())
            self.assertTrue((confirmation / "rounds" / "candidate-6.json").is_file())
            self.assertTrue((confirmation / "rounds" / "baseline-6.stderr.txt").is_file())


if __name__ == "__main__":
    unittest.main()
