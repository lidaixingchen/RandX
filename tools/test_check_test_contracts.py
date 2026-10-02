"""共享测试注册检查器的标准库单测。"""

from __future__ import annotations

import io
import os
import subprocess
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
from xml.etree import ElementTree

from check_test_contracts import (
    ContractError,
    TestManifest,
    _manifest_from_collected,
    _validate_pair,
    _validate_matrix,
    _write_manifest,
    main,
    parse_doctest_xml,
    validate_doctest_execution,
    validate_manifest,
)

SINGLE_REGISTRATION: int = 1
DUPLICATE_REGISTRATION_COUNT: int = 2
ENTROPY_SUITES: tuple[str, str] = ("公共/基础/安全熵源故障", "内部/熵源")


class ParseDoctestXmlTests(unittest.TestCase):
    def test_extracts_suite_case_pairs_from_xml_reporter(self) -> None:
        output: str = (
            '<?xml version="1.0" encoding="UTF-8"?>'
            '<doctest><TestCase name="KAT &amp; state" '
            'testsuite="公共/基础/引擎"/><TestCase name="KAT &amp; state" '
            'testsuite="公共/基础/引擎"/></doctest>'
        )
        self.assertEqual(
            parse_doctest_xml(output),
            {("公共/基础/引擎", "KAT & state"): 2},
        )

    def test_rejects_output_without_valid_xml(self) -> None:
        with self.assertRaises(ContractError):
            parse_doctest_xml("no doctest output")


class DoctestExecutionTests(unittest.TestCase):
    TEST_CASES: tuple[str, str] = ("entropy read succeeds", "seed succeeds")

    @staticmethod
    def _case_xml(name: str, *, skipped: bool = False, success: bool = True) -> str:
        if skipped:
            return f'<TestCase name="{name}" skipped="true"/>'
        result: str = "true" if success else "false"
        failures: int = 0 if success else 1
        return (
            f'<TestCase name="{name}"><OverallResultsAsserts '
            f'successes="1" failures="{failures}" test_case_success="{result}"'
            "/></TestCase>"
        )

    @classmethod
    def _xml(cls, cases: str) -> str:
        test_cases: ElementTree.Element = ElementTree.fromstring(f"<root>{cases}</root>")
        executed_results: list[ElementTree.Element] = [
            result
            for test_case in test_cases.iter("TestCase")
            for result in test_case.findall("OverallResultsAsserts")
        ]
        failures: int = sum(
            result.get("test_case_success", "").lower() != "true"
            for result in executed_results
        )
        successes: int = len(executed_results) - failures
        return (
            "<doctest><TestSuite>"
            f"{cases}"
            f"</TestSuite><OverallResultsTestCases successes=\"{successes}\" "
            f"failures=\"{failures}\""
            " skipped=\"0\"/></doctest>"
        )

    def _run_cli(
        self, output: str, returncode: int, test_cases: tuple[str, ...] | None = None
    ) -> tuple[int, subprocess.CompletedProcess[str], str]:
        selected_cases: tuple[str, ...] = test_cases or self.TEST_CASES
        with TemporaryDirectory() as temporary_directory:
            binary: Path = Path(temporary_directory) / "doctest-binary"
            binary.touch()
            arguments: list[str] = ["run-selected", "--binary", str(binary)]
            for case in selected_cases:
                arguments.extend(("--test-case", case))
            result: subprocess.CompletedProcess[str] = subprocess.CompletedProcess(
                args=[], returncode=returncode, stdout=output, stderr=""
            )
            with patch("check_test_contracts.subprocess.run", return_value=result) as run:
                error_output: io.StringIO = io.StringIO()
                with redirect_stdout(io.StringIO()):
                    with redirect_stderr(error_output):
                        status: int = main(arguments)
            self.assertEqual(run.call_count, 1)
            command: list[str] = run.call_args.args[0]
            filter_arguments: list[str] = [
                argument for argument in command if argument.startswith("--test-case=")
            ]
            expected_filter: str = ",".join(
                case.replace("\\", "\\\\").replace(",", "\\,")
                for case in selected_cases
            )
            self.assertEqual(filter_arguments, [f"--test-case={expected_filter}"])
            return status, result, error_output.getvalue()

    def test_run_selected_accepts_each_requested_case_once_and_successful(self) -> None:
        output: str = self._xml(
            self._case_xml(self.TEST_CASES[0]) + self._case_xml(self.TEST_CASES[1])
        )
        status, _, error = self._run_cli(output, 0)
        self.assertEqual(status, 0)
        self.assertEqual(error, "")

    def test_run_selected_rejects_zero_matches_even_when_doctest_succeeds(self) -> None:
        output: str = self._xml(
            self._case_xml(self.TEST_CASES[0], skipped=True)
            + self._case_xml(self.TEST_CASES[1], skipped=True)
        )
        status, _, error = self._run_cli(output, 0)
        self.assertEqual(status, 1)
        self.assertIn("未执行指定用例", error)

    def test_run_selected_rejects_a_missing_requested_case(self) -> None:
        output: str = self._xml(
            self._case_xml(self.TEST_CASES[0])
            + self._case_xml(self.TEST_CASES[1], skipped=True)
        )
        status, _, error = self._run_cli(output, 0)
        self.assertEqual(status, 1)
        self.assertIn(self.TEST_CASES[1], error)

    def test_run_selected_rejects_a_case_executed_more_than_once(self) -> None:
        output: str = self._xml(
            self._case_xml(self.TEST_CASES[0])
            + self._case_xml(self.TEST_CASES[0])
            + self._case_xml(self.TEST_CASES[1])
        )
        status, _, error = self._run_cli(output, 0)
        self.assertEqual(status, 1)
        self.assertIn("执行次数不为一次", error)

    def test_run_selected_rejects_a_real_case_failure(self) -> None:
        output: str = self._xml(
            self._case_xml(self.TEST_CASES[0], success=False)
            + self._case_xml(self.TEST_CASES[1])
        )
        status, _, error = self._run_cli(output, 1)
        self.assertEqual(status, 1)
        self.assertIn("用例失败", error)

    def test_rejects_duplicate_requested_case_names(self) -> None:
        output: str = self._xml(self._case_xml(self.TEST_CASES[0]))
        with self.assertRaisesRegex(ContractError, "不能重复"):
            validate_doctest_execution(output, (self.TEST_CASES[0], self.TEST_CASES[0]), 0)

    def test_escapes_doctest_filter_separators_in_case_names(self) -> None:
        selected_cases: tuple[str, str] = ("first,case", "second\\case")
        output: str = self._xml(
            self._case_xml(selected_cases[0]) + self._case_xml(selected_cases[1])
        )
        status, _, error = self._run_cli(output, 0, selected_cases)
        self.assertEqual(status, 0)
        self.assertEqual(error, "")


class ManifestValidationTests(unittest.TestCase):
    def test_rejects_empty_base_public_manifest(self) -> None:
        manifest: TestManifest = self._manifest({("专属/C++17", "用例"): 1})
        with self.assertRaisesRegex(ContractError, "基础公共清单为空"):
            validate_manifest(manifest, "fixture")

    def test_rejects_duplicate_public_registration(self) -> None:
        manifest: TestManifest = self._manifest(
            {("公共/基础/引擎", "KAT"): 2}
        )
        with self.assertRaisesRegex(ContractError, "恰好注册一次"):
            validate_manifest(manifest, "fixture")

    def test_rejects_malformed_condition_prefix(self) -> None:
        manifest: TestManifest = self._manifest(
            {
                ("公共/基础/引擎", "KAT"): 1,
                ("公共/条件/char8_t", "字符"): 1,
            }
        )
        with self.assertRaises(ContractError):
            validate_manifest(manifest, "fixture")

    def test_rejects_invalid_metadata_before_writing_manifest(self) -> None:
        registrations: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        invalid_metadata: list[dict[str, str]] = [
            {
                "compiler": "",
                "platform": "windows-2025",
                "standard": "c++17",
                "build_mode": "release",
                "variant": "cpp17",
            },
            {
                "compiler": "msvc",
                "platform": "windows-2025",
                "standard": "c++17",
                "build_mode": "release",
                "variant": "cpp23",
            },
        ]
        for metadata in invalid_metadata:
            with self.subTest(metadata=metadata), TemporaryDirectory() as temporary_directory:
                output: Path = Path(temporary_directory) / "invalid.json"
                with self.assertRaises(ContractError):
                    _write_manifest(output, metadata, registrations)
                self.assertFalse(output.exists())

    def test_allows_local_build_mode_only_for_binary_comparison(self) -> None:
        metadata: dict[str, str] = {
            "compiler": "local",
            "platform": "local",
            "standard": "c++17",
            "build_mode": "local",
            "variant": "cpp17",
        }
        registrations: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        manifest: TestManifest = TestManifest(metadata=metadata, registrations=registrations)
        with self.assertRaisesRegex(ContractError, "构建模式"):
            validate_manifest(manifest, "fixture")
        collected: TestManifest = _manifest_from_collected(registrations, metadata)
        self.assertEqual(collected.metadata["build_mode"], "local")

    @staticmethod
    def _manifest(registrations: dict[tuple[str, str], int]) -> TestManifest:
        metadata: dict[str, str] = {
            "compiler": "gcc-14",
            "platform": "ubuntu-24.04",
            "standard": "c++17",
            "build_mode": "debug",
            "variant": "cpp17",
        }
        return TestManifest(metadata=metadata, registrations=registrations)


class MatrixComparisonTests(unittest.TestCase):
    def test_accepts_base_and_char8_t_contracts_for_supported_standards(self) -> None:
        base: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        common_condition: dict[tuple[str, str], int] = {
            ("公共/条件/long_double/数值", "范围"): 1
        }
        char8_condition: dict[tuple[str, str], int] = {
            ("公共/条件/char8_t/字符", "编码单元"): 1
        }
        manifests: list[TestManifest] = [
            self._manifest("c++17", "cpp17", {**base, **common_condition}),
            self._manifest(
                "c++20",
                "cpp17",
                {**base, **common_condition, **char8_condition},
            ),
            self._manifest(
                "c++23",
                "cpp23",
                {**base, **common_condition, **char8_condition},
            ),
        ]
        _validate_matrix(manifests)

    def test_accepts_msvc_release_without_cxx20_manifest(self) -> None:
        base: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        manifests: list[TestManifest] = [
            self._manifest("c++17", "cpp17", base, "msvc", "windows-2025", "release"),
            self._manifest("c++23", "cpp23", base, "msvc", "windows-2025", "release"),
        ]
        _validate_matrix(manifests)

    def test_rejects_missing_expected_groups_with_only_msvc_release(self) -> None:
        base: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        manifests: list[TestManifest] = [
            self._manifest("c++17", "cpp17", base, "msvc", "windows-2025", "release"),
            self._manifest("c++23", "cpp23", base, "msvc", "windows-2025", "release"),
        ]
        with self.assertRaisesRegex(ContractError, "缺少矩阵组"):
            _validate_matrix(manifests, self._expected_groups())

    def test_accepts_complete_six_group_matrix(self) -> None:
        manifests: list[TestManifest] = []
        for compiler, platform, build_mode in self._expected_groups():
            manifests.extend(self._manifests_for_group(compiler, platform, build_mode))
        _validate_matrix(manifests, self._expected_groups())

    def test_rejects_unexpected_matrix_group(self) -> None:
        manifests: list[TestManifest] = []
        for compiler, platform, build_mode in self._expected_groups():
            manifests.extend(self._manifests_for_group(compiler, platform, build_mode))
        manifests.extend(self._manifests_for_group("gcc-14", "windows-2025", "debug"))
        with self.assertRaisesRegex(ContractError, "额外矩阵组"):
            _validate_matrix(manifests, self._expected_groups())

    def test_rejects_char8_t_in_cxx17(self) -> None:
        base: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        char8_condition: dict[tuple[str, str], int] = {
            ("公共/条件/char8_t/字符", "编码单元"): 1
        }
        manifests: list[TestManifest] = [
            self._manifest("c++17", "cpp17", {**base, **char8_condition}),
            self._manifest("c++20", "cpp17", base),
            self._manifest("c++23", "cpp23", base),
        ]
        with self.assertRaisesRegex(ContractError, r"C\+\+17.*char8_t"):
            _validate_matrix(manifests)

    def test_rejects_char8_t_registration_difference(self) -> None:
        base: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        char8_condition: dict[tuple[str, str], int] = {
            ("公共/条件/char8_t/字符", "编码单元"): 1
        }
        manifests: list[TestManifest] = [
            self._manifest("c++17", "cpp17", base),
            self._manifest("c++20", "cpp17", base),
            self._manifest("c++23", "cpp23", {**base, **char8_condition}),
        ]
        with self.assertRaisesRegex(ContractError, "char8_t 条件公共注册清单不一致"):
            _validate_matrix(manifests)

    def test_rejects_shared_char8_t_registration_omission(self) -> None:
        base: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        manifests: list[TestManifest] = [
            self._manifest("c++17", "cpp17", base),
            self._manifest("c++20", "cpp17", base),
            self._manifest("c++23", "cpp23", base),
        ]
        with self.assertRaisesRegex(ContractError, "char8_t.*公共注册清单为空"):
            _validate_matrix(manifests)

    def test_suite_mode_rejects_internal_omission_when_public_entries_match(self) -> None:
        public: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): SINGLE_REGISTRATION}
        common_entropy: dict[tuple[str, str], int] = {
            (ENTROPY_SUITES[0], "安全字节失败传播"): SINGLE_REGISTRATION
        }
        internal_entropy: dict[tuple[str, str], int] = {
            (ENTROPY_SUITES[1], "填充读取错误传播"): SINGLE_REGISTRATION
        }
        compat: TestManifest = self._manifest(
            "c++17", "cpp17", {**public, **common_entropy, **internal_entropy}
        )
        main: TestManifest = self._manifest(
            "c++23", "cpp23", {**public, **common_entropy}
        )

        with self.assertRaisesRegex(ContractError, r"c\+\+23.*缺少指定套件"):
            _validate_pair(compat, main, "c++17", "c++23", False, suites=ENTROPY_SUITES)

    def test_suite_mode_reports_suite_missing_from_both_manifests(self) -> None:
        registrations: dict[tuple[str, str], int] = {
            (ENTROPY_SUITES[0], "安全字节失败传播"): SINGLE_REGISTRATION
        }
        compat: TestManifest = self._manifest("c++17", "cpp17", registrations)
        main: TestManifest = self._manifest("c++23", "cpp23", registrations)

        with self.assertRaisesRegex(ContractError, r"c\+\+17.*缺少指定套件.*c\+\+23.*缺少指定套件"):
            _validate_pair(compat, main, "c++17", "c++23", False, suites=ENTROPY_SUITES)

    def test_suite_mode_rejects_duplicate_internal_registration(self) -> None:
        registrations: dict[tuple[str, str], int] = {
            (ENTROPY_SUITES[0], "安全字节失败传播"): SINGLE_REGISTRATION,
            (ENTROPY_SUITES[1], "填充读取错误传播"): DUPLICATE_REGISTRATION_COUNT,
        }
        compat: TestManifest = self._manifest("c++17", "cpp17", registrations)
        main: TestManifest = self._manifest("c++23", "cpp23", registrations)

        with self.assertRaisesRegex(ContractError, "恰好注册一次"):
            _validate_pair(compat, main, "c++17", "c++23", False, suites=ENTROPY_SUITES)

    def test_suite_mode_accepts_complete_equal_suites_without_public_cases(self) -> None:
        registrations: dict[tuple[str, str], int] = {
            (ENTROPY_SUITES[0], "安全字节失败传播"): SINGLE_REGISTRATION,
            (ENTROPY_SUITES[1], "填充读取错误传播"): SINGLE_REGISTRATION,
        }
        manifests: list[TestManifest] = [
            self._manifest("c++17", "cpp17", registrations),
            self._manifest("c++20", "cpp17", registrations),
            self._manifest("c++23", "cpp23", registrations),
        ]

        _validate_matrix(manifests, suites=ENTROPY_SUITES)

    def test_suite_matrix_requires_cpp20_unless_standard_pair_is_declared(self) -> None:
        registrations: dict[tuple[str, str], int] = {
            (suite, "熵源契约"): SINGLE_REGISTRATION for suite in ENTROPY_SUITES
        }
        manifests: list[TestManifest] = [
            self._manifest("c++17", "cpp17", registrations),
            self._manifest("c++23", "cpp23", registrations),
        ]
        with self.assertRaisesRegex(ContractError, r"缺少矩阵清单.*c\+\+20"):
            _validate_matrix(manifests, suites=ENTROPY_SUITES)
        _validate_matrix(
            manifests, suites=ENTROPY_SUITES,
            standard_pair_groups=[("gcc-14", "ubuntu-24.04", "debug")],
        )

    def test_suite_matrix_rejects_shared_omission_in_another_compiler_group(self) -> None:
        complete: dict[tuple[str, str], int] = {
            (suite, "熵源契约"): SINGLE_REGISTRATION for suite in ENTROPY_SUITES
        }
        complete[(ENTROPY_SUITES[1], "读取进度")] = SINGLE_REGISTRATION
        omitted = {key: count for key, count in complete.items() if key[1] != "读取进度"}
        manifests: list[TestManifest] = [
            self._manifest(standard, variant, registrations, compiler=compiler)
            for compiler, registrations in (("gcc-14", complete), ("clang-18", omitted))
            for standard, variant in (("c++17", "cpp17"), ("c++20", "cpp17"), ("c++23", "cpp23"))
        ]
        with self.assertRaisesRegex(ContractError, "指定套件矩阵注册清单不一致"):
            _validate_matrix(manifests, suites=ENTROPY_SUITES)

    @staticmethod
    def _manifest(
        standard: str,
        variant: str,
        registrations: dict[tuple[str, str], int],
        compiler: str = "gcc-14",
        platform: str = "ubuntu-24.04",
        build_mode: str = "debug",
    ) -> TestManifest:
        metadata: dict[str, str] = {
            "compiler": compiler,
            "platform": platform,
            "standard": standard,
            "build_mode": build_mode,
            "variant": variant,
        }
        return TestManifest(metadata=metadata, registrations=registrations)

    @staticmethod
    def _expected_groups() -> list[tuple[str, str, str]]:
        return [
            ("gcc-14", "ubuntu-24.04", "debug"),
            ("gcc-14", "ubuntu-24.04", "release"),
            ("clang-18", "ubuntu-24.04", "debug"),
            ("clang-18", "ubuntu-24.04", "release"),
            ("msvc", "windows-2025", "debug"),
            ("msvc", "windows-2025", "release"),
        ]

    @classmethod
    def _manifests_for_group(
        cls, compiler: str, platform: str, build_mode: str
    ) -> list[TestManifest]:
        base: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        char8_condition: dict[tuple[str, str], int] = {
            ("公共/条件/char8_t/字符", "编码单元"): 1
        }
        manifests: list[TestManifest] = [
            cls._manifest("c++17", "cpp17", base, compiler, platform, build_mode)
        ]
        if compiler == "msvc" and build_mode == "release":
            manifests.append(
                cls._manifest("c++23", "cpp23", base, compiler, platform, build_mode)
            )
            return manifests
        manifests.extend(
            [
                cls._manifest(
                    "c++20",
                    "cpp17",
                    {**base, **char8_condition},
                    compiler,
                    platform,
                    build_mode,
                ),
                cls._manifest(
                    "c++23",
                    "cpp23",
                    {**base, **char8_condition},
                    compiler,
                    platform,
                    build_mode,
                ),
            ]
        )
        return manifests


class CompareCommandTests(unittest.TestCase):
    def test_compare_binaries_accepts_repeated_suite_names(self) -> None:
        registrations: dict[tuple[str, str], int] = {
            (ENTROPY_SUITES[0], "安全字节失败传播"): SINGLE_REGISTRATION,
            (ENTROPY_SUITES[1], "填充读取错误传播"): SINGLE_REGISTRATION,
        }
        arguments: list[str] = [
            "compare-binaries",
            "--cpp17-binary",
            "entropy-cpp17",
            "--cpp23-binary",
            "entropy-cpp23",
            "--suite",
            ENTROPY_SUITES[0],
            "--suite",
            ENTROPY_SUITES[1],
        ]
        with patch(
            "check_test_contracts.collect_registrations",
            side_effect=[registrations, registrations],
        ) as collect, redirect_stdout(io.StringIO()):
            self.assertEqual(main(arguments), 0)
        self.assertEqual(collect.call_count, 2)

    def test_compare_command_accepts_suite_only_manifests(self) -> None:
        registrations: dict[tuple[str, str], int] = {
            (ENTROPY_SUITES[0], "安全字节失败传播"): SINGLE_REGISTRATION,
            (ENTROPY_SUITES[1], "填充读取错误传播"): SINGLE_REGISTRATION,
        }
        with TemporaryDirectory() as temporary_directory:
            manifest_dir: Path = Path(temporary_directory)
            for standard, variant in (("c++17", "cpp17"), ("c++20", "cpp17"), ("c++23", "cpp23")):
                metadata: dict[str, str] = {
                    "compiler": "gcc-14",
                    "platform": "ubuntu-24.04",
                    "standard": standard,
                    "build_mode": "debug",
                    "variant": variant,
                }
                _write_manifest(manifest_dir / f"{standard}.json", metadata, registrations)

            arguments: list[str] = [
                "compare",
                "--manifest-dir",
                str(manifest_dir),
                "--suite",
                ENTROPY_SUITES[0],
                "--suite",
                ENTROPY_SUITES[1],
            ]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(main(arguments), 0)

    def test_repeated_expected_groups_reach_matrix_validation(self) -> None:
        registrations: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        metadata_by_standard: list[dict[str, str]] = [
            {
                "compiler": "msvc",
                "platform": "windows-2025",
                "standard": "c++17",
                "build_mode": "release",
                "variant": "cpp17",
            },
            {
                "compiler": "msvc",
                "platform": "windows-2025",
                "standard": "c++23",
                "build_mode": "release",
                "variant": "cpp23",
            },
        ]
        with TemporaryDirectory() as temporary_directory:
            manifest_dir: Path = Path(temporary_directory)
            for metadata in metadata_by_standard:
                output: Path = manifest_dir / f"{metadata['standard']}.json"
                _write_manifest(output, metadata, registrations)

            valid_arguments: list[str] = [
                "compare",
                "--manifest-dir",
                str(manifest_dir),
                "--expected-group",
                "msvc",
                "windows-2025",
                "release",
            ]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(main(valid_arguments), 0)

            incomplete_arguments: list[str] = [
                *valid_arguments,
                "--expected-group",
                "msvc",
                "windows-2025",
                "debug",
            ]
            error_output: io.StringIO = io.StringIO()
            with redirect_stderr(error_output):
                self.assertEqual(main(incomplete_arguments), 1)
            self.assertIn("缺少矩阵组", error_output.getvalue())

            invalid_build_mode_arguments: list[str] = [
                "compare",
                "--manifest-dir",
                str(manifest_dir),
                "--expected-group",
                "msvc",
                "windows-2025",
                "production",
            ]
            with redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as result:
                    main(invalid_build_mode_arguments)
            self.assertEqual(result.exception.code, 2)


class CommandEncodingTests(unittest.TestCase):
    def test_cli_outputs_utf8_with_legacy_console_encoding(self) -> None:
        registrations: dict[tuple[str, str], int] = {("公共/基础/引擎", "KAT"): 1}
        with TemporaryDirectory() as temporary_directory:
            manifest_dir: Path = Path(temporary_directory)
            for standard, variant in (("c++17", "cpp17"), ("c++23", "cpp23")):
                metadata: dict[str, str] = {
                    "compiler": "msvc",
                    "platform": "windows-2025",
                    "standard": standard,
                    "build_mode": "release",
                    "variant": variant,
                }
                _write_manifest(manifest_dir / f"{standard}.json", metadata, registrations)

            command: list[str] = [
                sys.executable,
                str(Path(__file__).with_name("check_test_contracts.py")),
                "compare",
                "--manifest-dir",
                str(manifest_dir),
            ]
            environment: dict[str, str] = {**os.environ, "PYTHONIOENCODING": "cp1252"}
            success: subprocess.CompletedProcess[bytes] = subprocess.run(
                command, env=environment, capture_output=True, check=False
            )
            self.assertEqual(success.returncode, 0, success.stderr.decode("utf-8"))
            self.assertIn("已核对 2 份清单", success.stdout.decode("utf-8"))

            failure: subprocess.CompletedProcess[bytes] = subprocess.run(
                [*command, "--expected-group", "msvc", "windows-2025", "debug"],
                env=environment,
                capture_output=True,
                check=False,
            )
            self.assertEqual(failure.returncode, 1)
            self.assertIn("缺少矩阵组", failure.stderr.decode("utf-8"))


if __name__ == "__main__":
    unittest.main()
