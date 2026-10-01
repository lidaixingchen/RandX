"""抽样前后比较工具的标准库单元测试。"""

from __future__ import annotations

import json
import io
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from typing import Any
from unittest.mock import patch

from compare_sampling import (
    CompareSamplingError,
    CompareConfig,
    build_command,
    compare_outputs,
    _parse_standard_library_version,
    main,
)


COMMON_CASES: tuple[str, ...] = (
    "iterator-zero",
    "iterator-empty",
    "iterator-full",
    "iterator-oversample",
    "iterator-hash-threshold",
    "iterator-index-threshold",
    "container-bitmap-at-threshold",
    "container-bitmap-above-threshold",
    "reservoir-default",
    "default-iterator-consecutive",
    "iterator-explicit-hash-threshold",
    "iterator-explicit-index-threshold",
    "iterator-explicit-zero",
    "iterator-explicit-empty",
    "iterator-explicit-full",
    "iterator-explicit-oversample",
    "container-explicit-bitmap-threshold",
    "container-explicit-bitmap-above-threshold",
    "container-explicit-bitmap-word",
    "container-explicit-bitmap-word-above",
    "container-explicit-zero",
    "container-explicit-empty",
    "container-explicit-full",
    "container-explicit-oversample",
    "iterator-explicit-32-bit",
    "iterator-explicit-nonzero-minimum",
    "iterator-explicit-noncopyable-engine",
    "iterator-engine-failure",
    "iterator-hash-copy-failure",
    "iterator-index-copy-failure",
    "container-bitmap-copy-failure",
    "container-full-copy-failure",
    "reservoir-initial-copy-failure",
    "reservoir-explicit-zero",
    "reservoir-explicit-empty",
    "reservoir-explicit-full",
    "reservoir-explicit-oversample",
    "reservoir-replacement-assignment-failure",
    "reservoir-explicit",
    "explicit-iterator-consecutive",
)
CPP23_REQUIRED_CASES: tuple[str, ...] = (
    "cpp23-counted-random-access-sentinel",
    "cpp23-move-only-istream-range",
    "cpp23-move-only-istream-explicit",
)


class ConsoleEncodingTests(unittest.TestCase):
    def test_cli_outputs_utf8_with_legacy_console_encoding(self) -> None:
        script: Path = Path(__file__).with_name("compare_sampling.py")
        environment: dict[str, str] = {
            **os.environ, "PYTHONIOENCODING": "cp1252", "PYTHONUTF8": "0",
        }
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            failed_arguments: list[str] = [
                "--baseline-ref", "HEAD", "--candidate-root", str(root / "missing"),
                "--compiler", "g++", "--compiler-family", "gcc", "--standard", "c++17",
                "--build-mode", "release", "--output-dir", str(root / "output"),
            ]
            for arguments, expected_status, stream_name, expected_text in (
                (["--help"], 0, "stdout", "基点"),
                (failed_arguments, 2, "stderr", "候选根目录不存在"),
            ):
                with self.subTest(arguments=arguments):
                    result: subprocess.CompletedProcess[bytes] = subprocess.run(
                        [sys.executable, str(script), *arguments],
                        env=environment, capture_output=True, check=False,
                    )
                    self.assertEqual(result.returncode, expected_status, result.stderr)
                    output: str = getattr(result, stream_name).decode("utf-8")
                    self.assertIn(expected_text, output)
                    self.assertNotIn("UnicodeEncodeError", output)
CPP23_OPTIONAL_WIDE_CASES: tuple[str, ...] = (
    "cpp23-wide-difference-zero",
    "cpp23-wide-difference-length-error",
)


def _case_output(case_ids: tuple[str, ...]) -> bytes:
    return "".join(f"[case] {case_id} seed=19\nsample=2,4\n" for case_id in case_ids).encode()


class OutputComparisonTests(unittest.TestCase):
    def test_reports_first_changed_case_and_context(self) -> None:
        baseline: bytes = (
            b"case=iterator-default seed=19\n"
            b"sample=2,4,6\n"
            b"state=abc\n"
            b"case=reservoir-explicit seed=19\n"
            b"sample=1,3\n"
            b"exception=none progress=8\n"
        )
        candidate: bytes = baseline.replace(b"sample=1,3", b"sample=1,4")

        comparison: Any = compare_outputs(baseline, candidate)

        self.assertFalse(comparison.identical)
        self.assertEqual(comparison.first_difference_case, "reservoir-explicit")
        self.assertIn("sample=1,3", comparison.report)
        self.assertIn("sample=1,4", comparison.report)
        self.assertIn("exception=none progress=8", comparison.report)

    def test_reports_case_set_and_exception_record_differences(self) -> None:
        baseline: bytes = (
            b"case=iterator-default seed=19\n"
            b"exception=length_error progress=0\n"
        )
        candidate: bytes = (
            b"case=reservoir-explicit seed=19\n"
            b"exception=runtime_error progress=2\n"
        )

        comparison: Any = compare_outputs(baseline, candidate)

        self.assertFalse(comparison.identical)
        self.assertEqual(comparison.first_difference_case, "iterator-default")
        self.assertEqual(comparison.missing_cases, ("iterator-default",))
        self.assertEqual(comparison.added_cases, ("reservoir-explicit",))
        self.assertIn("length_error", comparison.report)
        self.assertIn("runtime_error", comparison.report)

    def test_identical_output_passes(self) -> None:
        output: bytes = b"case=iterator-default seed=19\nsample=2,4,6\n"

        comparison: Any = compare_outputs(output, output)

        self.assertTrue(comparison.identical)
        self.assertEqual(comparison.report, "抽样输出逐字节一致。")

    def test_rejects_two_empty_outputs(self) -> None:
        with self.assertRaises(CompareSamplingError):
            compare_outputs(b"", b"")

    def test_rejects_two_outputs_without_case_identifiers(self) -> None:
        output: bytes = b"sampling observations finished\n"

        with self.assertRaises(CompareSamplingError):
            compare_outputs(output, output)

    def test_rejects_a_case_missing_from_both_outputs(self) -> None:
        output: bytes = b"[case] required-a seed=19\nsample=1\n"

        with self.assertRaises(CompareSamplingError):
            compare_outputs(output, output, required_cases=("required-a", "required-b"))

    def test_rejects_cpp23_output_missing_a_required_modern_case(self) -> None:
        observed_cases: tuple[str, ...] = (*COMMON_CASES, *CPP23_REQUIRED_CASES[:2])
        output: bytes = _case_output(observed_cases)

        with self.assertRaises(CompareSamplingError):
            compare_outputs(
                output,
                output,
                required_cases=(*COMMON_CASES, *CPP23_REQUIRED_CASES),
            )

    def test_accepts_the_common_observation_contract(self) -> None:
        self.assertEqual(len(COMMON_CASES), 40)
        output: bytes = _case_output(COMMON_CASES)

        comparison: Any = compare_outputs(output, output, required_cases=COMMON_CASES)

        self.assertTrue(comparison.identical)

    def test_accepts_cpp23_contract_with_or_without_platform_wide_cases(self) -> None:
        required_cases: tuple[str, ...] = (*COMMON_CASES, *CPP23_REQUIRED_CASES)
        for optional_cases in ((), CPP23_OPTIONAL_WIDE_CASES):
            with self.subTest(optional_cases=optional_cases):
                output: bytes = _case_output((*required_cases, *optional_cases))

                comparison: Any = compare_outputs(
                    output,
                    output,
                    required_cases=required_cases,
                )

                self.assertTrue(comparison.identical)

    def test_reports_line_ending_only_difference_as_bytes(self) -> None:
        baseline: bytes = b"case=iterator-default\r\nsample=2,4\r\n"
        candidate: bytes = b"case=iterator-default\nsample=2,4\n"

        comparison: Any = compare_outputs(baseline, candidate)

        self.assertFalse(comparison.identical)
        self.assertIn("首个差异字节偏移", comparison.report)
        self.assertIn(r"\r\n", comparison.report)


class CompilerCommandTests(unittest.TestCase):
    def test_gcc_selects_compatibility_header_for_cxx20(self) -> None:
        command: list[str] = build_command(
            compiler=Path("C:/toolchain/g++.exe"),
            compiler_family="gcc",
            standard="c++20",
            build_mode="debug",
            source=Path("C:/work/public_source/gen_parity_sequences.cpp"),
            header_directory=Path("C:/work/baseline_headers"),
            shared_source_directory=Path("C:/work/public_source"),
            executable=Path("C:/work/baseline_build/sampling.exe"),
            platform_name="Windows",
        )

        self.assertIn("-std=c++20", command)
        self.assertIn("-DRANDX_PARITY_CPP17", command)
        self.assertIn("-D_GLIBCXX_ASSERTIONS", command)
        self.assertIn("-lbcrypt", command)
        self.assertLess(
            command.index(str(Path("C:/work/baseline_headers"))),
            command.index(str(Path("C:/work/public_source"))),
        )

    def test_msvc_uses_preview_and_utf8_for_cxx23_release(self) -> None:
        command: list[str] = build_command(
            compiler=Path("C:/VS/VC/Tools/MSVC/cl.exe"),
            compiler_family="msvc",
            standard="c++23",
            build_mode="release",
            source=Path("C:/work/public_source/gen_parity_sequences.cpp"),
            header_directory=Path("C:/work/candidate_headers"),
            shared_source_directory=Path("C:/work/public_source"),
            executable=Path("C:/work/candidate_build/sampling.exe"),
            platform_name="Windows",
        )

        self.assertIn("/std:c++23preview", command)
        self.assertIn("/utf-8", command)
        self.assertIn("/Zc:__cplusplus", command)
        self.assertIn("/O2", command)
        self.assertIn("/DNDEBUG", command)
        self.assertFalse(any("RANDX_PARITY_CPP17" in argument for argument in command))

    def test_clang_links_security_framework_on_macos(self) -> None:
        command: list[str] = build_command(
            compiler=Path("/usr/bin/clang++"),
            compiler_family="clang",
            standard="c++17",
            build_mode="release",
            source=Path("/tmp/source/gen_parity_sequences.cpp"),
            header_directory=Path("/tmp/baseline_headers"),
            shared_source_directory=Path("/tmp/source"),
            executable=Path("/tmp/build/sampling"),
            platform_name="Darwin",
        )

        self.assertIn("-framework", command)
        self.assertIn("Security", command)

    def test_extracts_standard_library_version_from_compiler_diagnostic(self) -> None:
        compiler_output: bytes = (
            b"source.cpp:12: note: '#pragma message: RANDX_STDLIB=libstdc++ 20260430'\n"
        )

        version: str = _parse_standard_library_version(compiler_output)

        self.assertEqual(version, "libstdc++ 20260430")


class CompareCommandTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        self.root: Path = Path(self.temporary_directory.name)
        self.candidate_root: Path = self.root / "candidate"
        self.output_directory: Path = self.root / "comparison"
        (self.candidate_root / "tests" / "common").mkdir(parents=True)
        (self.candidate_root / "RandX.hpp").write_text("candidate-main\n", encoding="utf-8")
        (self.candidate_root / "RandX_Cpp17.hpp").write_text("candidate-compat\n", encoding="utf-8")
        (self.candidate_root / "gen_parity_sequences.cpp").write_text(
            '#include "RandX.hpp"\n#include "tests/common/sampling_observations.hpp"\n',
            encoding="utf-8",
        )
        (self.candidate_root / "tests" / "common" / "sampling_observations.hpp").write_text(
            "// sampling support fixture\n", encoding="utf-8"
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def _arguments(self) -> list[str]:
        return [
            "--baseline-ref",
            "e0ce50a",
            "--candidate-root",
            str(self.candidate_root),
            "--compiler",
            "C:/toolchain/g++.exe",
            "--compiler-family",
            "gcc",
            "--standard",
            "c++17",
            "--build-mode",
            "debug",
            "--output-dir",
            str(self.output_directory),
        ]

    def _run_command_factory(
        self,
        baseline_output: bytes = _case_output(COMMON_CASES),
        candidate_output: bytes = _case_output(COMMON_CASES),
        failed_executable: str | None = None,
    ) -> Any:
        execution_index: int = 0

        def run_command(command: list[str], cwd: Path, input_bytes: bytes | None = None) -> Any:
            nonlocal execution_index
            if command[0] == "git" and command[1] == "rev-parse":
                return subprocess.CompletedProcess(command, 0, stdout=b"e0ce50a000000000\n", stderr=b"")
            if command[0] == "git" and command[1] == "show":
                source_path: str = command[-1].split(":", maxsplit=1)[1]
                return subprocess.CompletedProcess(
                    command, 0, stdout=f"baseline:{source_path}\n".encode(), stderr=b""
                )
            if command[-1] == "--version":
                return subprocess.CompletedProcess(command, 0, stdout=b"fixture compiler 1.0\n", stderr=b"")
            if any("standard_library_probe" in argument for argument in command):
                return subprocess.CompletedProcess(
                    command, 0, stdout=b"RANDX_STDLIB=fixture-stl\n", stderr=b""
                )
            if any(argument.endswith("gen_parity_sequences.cpp") for argument in command):
                if "-o" in command:
                    executable_index: int = command.index("-o") + 1
                    executable_path: Path = Path(command[executable_index])
                else:
                    executable_argument: str = next(
                        argument for argument in command if argument.startswith("/Fe:")
                    )
                    executable_path = Path(executable_argument.removeprefix("/Fe:"))
                executable_path.parent.mkdir(parents=True, exist_ok=True)
                executable_path.write_bytes(b"fixture executable")
                return subprocess.CompletedProcess(command, 0, stdout=b"", stderr=b"")
            if command[0].endswith("sampling.exe") or command[0].endswith("/sampling"):
                execution_index += 1
                if failed_executable is not None and failed_executable in command[0]:
                    return subprocess.CompletedProcess(command, 9, stdout=b"", stderr=b"fixture failure")
                output: bytes = baseline_output if execution_index == 1 else candidate_output
                return subprocess.CompletedProcess(command, 0, stdout=output, stderr=b"")
            return subprocess.CompletedProcess(command, 0, stdout=b"", stderr=b"")

        return run_command

    def test_copies_baseline_and_candidate_headers_to_isolated_include_roots(self) -> None:
        with (
            patch("compare_sampling.run_command", side_effect=self._run_command_factory()),
            redirect_stdout(io.StringIO()),
        ):
            result: int = main(self._arguments())

        self.assertEqual(result, 0)
        baseline_headers: Path = self.output_directory / "baseline_headers"
        candidate_headers: Path = self.output_directory / "candidate_headers"
        public_source: Path = self.output_directory / "public_source"
        self.assertEqual(
            (baseline_headers / "RandX.hpp").read_text(encoding="utf-8"),
            "baseline:RandX.hpp\n",
        )
        self.assertEqual(
            (candidate_headers / "RandX.hpp").read_text(encoding="utf-8"),
            "candidate-main\n",
        )
        self.assertTrue((public_source / "tests" / "common" / "sampling_observations.hpp").is_file())
        self.assertFalse((public_source / "RandX.hpp").exists())
        self.assertFalse((public_source / "RandX_Cpp17.hpp").exists())
        self.assertFalse((public_source / "standard_library_probe.cpp").exists())
        metadata: dict[str, Any] = json.loads(
            (self.output_directory / "metadata.json").read_text(encoding="utf-8")
        )
        baseline_command: list[str] = metadata["builds"]["baseline"]["command"]
        candidate_command: list[str] = metadata["builds"]["candidate"]["command"]
        self.assertIn(str(baseline_headers.resolve()), baseline_command)
        self.assertIn(str(candidate_headers.resolve()), candidate_command)

    def test_difference_and_child_process_failure_use_distinct_status(self) -> None:
        difference_runner: Any = self._run_command_factory(
            candidate_output=_case_output(COMMON_CASES).replace(b"sample=2,4", b"sample=1,3", 1)
        )
        with (
            patch("compare_sampling.run_command", side_effect=difference_runner),
            redirect_stdout(io.StringIO()),
        ):
            difference_status: int = main(self._arguments())
        self.assertEqual(difference_status, 1)
        self.assertTrue((self.output_directory / "diff.txt").is_file())

        failing_directory: Path = self.root / "failed"
        failed_arguments: list[str] = self._arguments()
        failed_arguments[-1] = str(failing_directory)
        failure_runner: Any = self._run_command_factory(failed_executable="baseline_build")
        with (
            patch("compare_sampling.run_command", side_effect=failure_runner),
            redirect_stderr(io.StringIO()),
        ):
            process_status: int = main(failed_arguments)
        self.assertEqual(process_status, 2)
        failed_metadata: dict[str, Any] = json.loads(
            (failing_directory / "metadata.json").read_text(encoding="utf-8")
        )
        self.assertEqual(failed_metadata["failure"]["stage"], "baseline execution")
        self.assertEqual(failed_metadata["failure"]["returncode"], 9)

    def test_missing_cpp23_case_fails_with_metadata_and_keeps_raw_outputs(self) -> None:
        observed_cases: tuple[str, ...] = (*COMMON_CASES, *CPP23_REQUIRED_CASES[:2])
        raw_output: bytes = _case_output(observed_cases)
        runner: Any = self._run_command_factory(
            baseline_output=raw_output,
            candidate_output=raw_output,
        )
        arguments: list[str] = self._arguments()
        arguments[arguments.index("c++17")] = "c++23"

        with (
            patch("compare_sampling.run_command", side_effect=runner),
            redirect_stderr(io.StringIO()),
        ):
            status: int = main(arguments)

        self.assertEqual(status, 2)
        self.assertEqual((self.output_directory / "baseline_output.txt").read_bytes(), raw_output)
        self.assertEqual((self.output_directory / "candidate_output.txt").read_bytes(), raw_output)
        metadata: dict[str, Any] = json.loads(
            (self.output_directory / "metadata.json").read_text(encoding="utf-8")
        )
        self.assertEqual(metadata["failure"]["stage"], "观察校验")

    def test_empty_child_outputs_fail_with_exit_two_and_preserve_artifacts(self) -> None:
        runner: Any = self._run_command_factory(baseline_output=b"", candidate_output=b"")

        with (
            patch("compare_sampling.run_command", side_effect=runner),
            redirect_stderr(io.StringIO()),
        ):
            status: int = main(self._arguments())

        self.assertEqual(status, 2)
        self.assertEqual((self.output_directory / "baseline_output.txt").read_bytes(), b"")
        self.assertEqual((self.output_directory / "candidate_output.txt").read_bytes(), b"")
        metadata: dict[str, Any] = json.loads(
            (self.output_directory / "metadata.json").read_text(encoding="utf-8")
        )
        self.assertEqual(metadata["failure"]["stage"], "观察校验")

    def test_shared_required_case_omission_fails_through_the_cli(self) -> None:
        missing_case: str = "iterator-engine-failure"
        observed_cases: tuple[str, ...] = tuple(
            case for case in COMMON_CASES if case != missing_case
        )
        raw_output: bytes = _case_output(observed_cases)
        runner: Any = self._run_command_factory(
            baseline_output=raw_output,
            candidate_output=raw_output,
        )

        with (
            patch("compare_sampling.run_command", side_effect=runner),
            redirect_stderr(io.StringIO()),
        ):
            status: int = main(self._arguments())

        self.assertEqual(status, 2)
        metadata: dict[str, Any] = json.loads(
            (self.output_directory / "metadata.json").read_text(encoding="utf-8")
        )
        self.assertEqual(metadata["failure"]["stage"], "观察校验")
        self.assertIn(missing_case, metadata["failure"]["message"])
        self.assertEqual((self.output_directory / "baseline_output.txt").read_bytes(), raw_output)
        self.assertEqual((self.output_directory / "candidate_output.txt").read_bytes(), raw_output)


if __name__ == "__main__":
    unittest.main()
