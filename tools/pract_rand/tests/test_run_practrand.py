#!/usr/bin/env python3
"""tools/pract_rand/run_practrand.py 驱动与输出解析单元测试套件."""

import json
import os
import sys
import tempfile
import unittest
from dataclasses import asdict
from pathlib import Path

# 将上一级目录加入 sys.path
TESTS_DIR = Path(__file__).resolve().parent
PRACTRAND_DIR = TESTS_DIR.parent
FIXTURES_DIR = TESTS_DIR / "fixtures"
MOCK_RUNNER = TESTS_DIR / "helpers" / "mock_runner.py"

if str(PRACTRAND_DIR) not in sys.path:
    sys.path.insert(0, str(PRACTRAND_DIR))

from run_practrand import (
    atomic_write_json,
    classify_result,
    compute_exit_code,
    compute_overall_exit_code,
    format_bytes_for_practrand,
    parse_length_to_bytes,
    parse_practrand_output,
    test_engine,
    TestResult,
)


class TestPractRandLengthParser(unittest.TestCase):
    """测试人类可读长度到字节数的转换."""

    def test_units(self):
        self.assertEqual(parse_length_to_bytes("1KB"), 1024)
        self.assertEqual(parse_length_to_bytes("32MB"), 32 * 1024 * 1024)
        self.assertEqual(parse_length_to_bytes("4GB"), 4 * 1024 * 1024 * 1024)
        self.assertEqual(parse_length_to_bytes("1TB"), 1024 * 1024 * 1024 * 1024)
        self.assertEqual(parse_length_to_bytes("1024B"), 1024)
        self.assertEqual(parse_length_to_bytes("1K"), 1024)
        self.assertEqual(parse_length_to_bytes("32M"), 32 * 1024 * 1024)
        self.assertEqual(parse_length_to_bytes("4G"), 4 * 1024 * 1024 * 1024)

    def test_bare_number_rejected(self):
        with self.assertRaises(ValueError):
            parse_length_to_bytes("30")
        with self.assertRaises(ValueError):
            parse_length_to_bytes("1024")

    def test_format_bytes_for_practrand(self):
        self.assertEqual(format_bytes_for_practrand(1024), "1K")
        self.assertEqual(format_bytes_for_practrand(32 * 1024 * 1024), "32M")
        self.assertEqual(format_bytes_for_practrand(4 * 1024 * 1024 * 1024), "4G")
        self.assertEqual(format_bytes_for_practrand(1024 * 1024 * 1024 * 1024), "1T")
        self.assertEqual(format_bytes_for_practrand(30), "30B")


class TestPractRandOutputParser(unittest.TestCase):
    """测试 PractRand 文本输出解析器."""

    def test_fixture_normal_complete_pass(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(
            content, 32 * 1024 * 1024
        )
        self.assertEqual(max_tested, 32 * 1024 * 1024)
        self.assertEqual(test_count, 126)
        self.assertFalse(has_failure)
        self.assertEqual(suspicious_count, 0)

    def test_fixture_complete_with_fail(self):
        content = (FIXTURES_DIR / "complete_with_fail_exit_0.txt").read_text(encoding="utf-8")
        max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(
            content, 32 * 1024 * 1024
        )
        self.assertEqual(max_tested, 32 * 1024 * 1024)
        self.assertEqual(test_count, 126)
        self.assertTrue(has_failure)

    def test_fixture_help_text(self):
        content = (FIXTURES_DIR / "help_text_exit_0.txt").read_text(encoding="utf-8")
        max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(
            content, 32 * 1024 * 1024
        )
        self.assertEqual(max_tested, 0)
        self.assertEqual(test_count, 0)
        self.assertFalse(has_failure)

    def test_fixture_banner_only(self):
        content = (FIXTURES_DIR / "banner_only.txt").read_text(encoding="utf-8")
        max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(
            content, 32 * 1024 * 1024
        )
        self.assertEqual(max_tested, 0)
        self.assertEqual(test_count, 0)
        self.assertFalse(has_failure)

    def test_fixture_zero_test_results(self):
        content = (FIXTURES_DIR / "zero_test_results.txt").read_text(encoding="utf-8")
        max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(
            content, 32 * 1024 * 1024
        )
        self.assertEqual(max_tested, 32 * 1024 * 1024)
        self.assertEqual(test_count, 0)
        self.assertFalse(has_failure)

    def test_fixture_under_target_bytes(self):
        content = (FIXTURES_DIR / "under_target_bytes.txt").read_text(encoding="utf-8")
        max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(
            content, 32 * 1024 * 1024
        )
        self.assertEqual(max_tested, 16 * 1024 * 1024)
        self.assertEqual(test_count, 60)
        self.assertFalse(has_failure)

    def test_fixture_suspicious_pass(self):
        content = (FIXTURES_DIR / "suspicious_pass.txt").read_text(encoding="utf-8")
        max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(
            content, 32 * 1024 * 1024
        )
        self.assertEqual(max_tested, 32 * 1024 * 1024)
        self.assertEqual(test_count, 126)
        self.assertFalse(has_failure)
        self.assertGreater(suspicious_count, 0)

    def test_singular_unit_checkpoints(self):
        text_1mb = (
            "RNG_test using PractRand version 0.95\n"
            "rng=RNG_stdin64, seed=0x1234\n"
            "length= 1 megabyte (2^20 bytes), time= 0.05 seconds\n"
            "  no anomalies in 126 test result(s)\n"
        )
        max_tested, test_count, has_failure, _ = parse_practrand_output(text_1mb, 1024 * 1024)
        self.assertEqual(max_tested, 1024 * 1024)
        self.assertEqual(test_count, 126)
        self.assertFalse(has_failure)

        text_1gb = (
            "RNG_test using PractRand version 0.95\n"
            "rng=RNG_stdin64, seed=0x1234\n"
            "length= 1 gigabyte (2^30 bytes), time= 5.0 seconds\n"
            "  no anomalies in 126 test result(s)\n"
        )
        max_tested, test_count, has_failure, _ = parse_practrand_output(text_1gb, 1024**3)
        self.assertEqual(max_tested, 1024**3)
        self.assertEqual(test_count, 126)
        self.assertFalse(has_failure)

        text_1tb = (
            "RNG_test using PractRand version 0.95\n"
            "rng=RNG_stdin64, seed=0x1234\n"
            "length= 1 terabyte (2^40 bytes), time= 500.0 seconds\n"
            "  no anomalies in 126 test result(s)\n"
        )
        max_tested, test_count, has_failure, _ = parse_practrand_output(text_1tb, 1024**4)
        self.assertEqual(max_tested, 1024**4)
        self.assertEqual(test_count, 126)
        self.assertFalse(has_failure)


class TestPractRandStatusCategorization(unittest.TestCase):
    """测试状态分类规则与判定场景."""

    def evaluate_status(
        self,
        full_output: str,
        target_bytes: int,
        pr_returncode: int,
        gen_returncode: int | None = 0,
        timed_out: bool = False,
    ) -> TestResult:
        status, reason, max_tested, test_count, suspicious_count = classify_result(
            full_output=full_output,
            target_bytes=target_bytes,
            pr_returncode=pr_returncode,
            gen_returncode=gen_returncode,
            timed_out=timed_out,
            length="32MB",
        )
        return TestResult(
            target_bytes=target_bytes,
            status=status,
            reason=reason,
            reported_tested_bytes=max_tested,
            test_count=test_count,
            suspicious_count=suspicious_count,
            pr_returncode=pr_returncode,
            gen_returncode=gen_returncode,
        )

    def test_scenario_1_normal_complete_pass(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0)
        self.assertEqual(res.status, "pass")

    def test_scenario_2_complete_with_fail_exit_0(self):
        content = (FIXTURES_DIR / "complete_with_fail_exit_0.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0)
        self.assertEqual(res.status, "statistical_failure")

    def test_scenario_3_help_text_exit_0(self):
        content = (FIXTURES_DIR / "help_text_exit_0.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0)
        self.assertEqual(res.status, "environment_error")

    def test_scenario_4_empty_output_exit_0(self):
        res = self.evaluate_status("", 32 * 1024 * 1024, pr_returncode=0)
        self.assertEqual(res.status, "environment_error")

    def test_scenario_5_banner_only(self):
        content = (FIXTURES_DIR / "banner_only.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0)
        self.assertEqual(res.status, "environment_error")

    def test_scenario_6_zero_test_results(self):
        content = (FIXTURES_DIR / "zero_test_results.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0)
        self.assertEqual(res.status, "inconclusive")

    def test_scenario_7_under_target_bytes(self):
        content = (FIXTURES_DIR / "under_target_bytes.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0, gen_returncode=0)
        self.assertEqual(res.status, "inconclusive")

    def test_scenario_8_generator_early_crash(self):
        content = (FIXTURES_DIR / "under_target_bytes.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0, gen_returncode=1)
        self.assertEqual(res.status, "environment_error")

    def test_scenario_9_timeout(self):
        content = (FIXTURES_DIR / "under_target_bytes.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0, timed_out=True)
        self.assertEqual(res.status, "inconclusive")

    def test_scenario_10_generator_sigpipe_normal(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0, gen_returncode=-13)
        self.assertEqual(res.status, "pass")

    def test_scenario_11_unrecognized_format(self):
        content = (FIXTURES_DIR / "unrecognized_format.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=0)
        self.assertEqual(res.status, "environment_error")

    def test_scenario_12_tester_nonzero_exit(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = self.evaluate_status(content, 32 * 1024 * 1024, pr_returncode=139)
        self.assertEqual(res.status, "environment_error")

    def test_scenario_13_tester_start_failure(self):
        res = test_engine(
            generator=sys.executable,
            practrand="/nonexistent/path/to/RNG_test",
            engine="sfc64",
            length="1KB",
        )
        self.assertEqual(res.status, "environment_error")
        self.assertIn("无法启动 PractRand", res.reason)

    def test_scenario_14_generator_start_failure(self):
        res = test_engine(
            generator="/nonexistent/path/to/gen_stream",
            practrand=sys.executable,
            engine="sfc64",
            length="1KB",
        )
        self.assertEqual(res.status, "environment_error")
        self.assertIn("无法启动生成器", res.reason)


class TestPractRandTwoAxisCategorization(unittest.TestCase):
    """测试两轴状态判定模型核心场景."""

    def test_pass_report_with_generator_crash_neg11(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = classify_result(
            full_output=content,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=-11,
            length="32MB",
        )
        self.assertEqual(res.status, "environment_error")
        self.assertEqual(res.execution_status, "failed")
        self.assertEqual(res.statistical_status, "pass")
        self.assertIn("GENERATOR_CRASH", res.reason_codes)
        self.assertIn("提前异常退出 (退出码 -11)", res.reason)

    def test_pass_report_with_generator_exit_2(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = classify_result(
            full_output=content,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=2,
            length="32MB",
        )
        self.assertEqual(res.status, "environment_error")
        self.assertEqual(res.execution_status, "failed")
        self.assertEqual(res.statistical_status, "pass")
        self.assertIn("GENERATOR_NONZERO_EXIT", res.reason_codes)

    def test_tester_none_exit_code(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = classify_result(
            full_output=content,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=None,
            gen_returncode=0,
            length="32MB",
        )
        self.assertEqual(res.status, "environment_error")
        self.assertEqual(res.execution_status, "unknown")
        self.assertIn("TESTER_UNKNOWN_EXIT", res.reason_codes)

    def test_supervisor_cleanup_after_tester_normal_exit(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = classify_result(
            full_output=content,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=-15,
            gen_cleanup_requested=True,
            generator_termination_cause="supervisor_cleanup",
            length="32MB",
        )
        self.assertEqual(res.status, "pass")
        self.assertEqual(res.execution_status, "ok")
        self.assertEqual(res.statistical_status, "pass")

    def test_cleanup_requested_does_not_mask_sigsegv(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = classify_result(
            full_output=content,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=-11,
            gen_cleanup_requested=True,
            generator_termination_cause="supervisor_cleanup",
            length="32MB",
        )
        self.assertEqual(res.status, "environment_error")
        self.assertEqual(res.execution_status, "failed")
        self.assertIn("GENERATOR_CRASH", res.reason_codes)

    def test_cleanup_requested_does_not_mask_exit_2(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = classify_result(
            full_output=content,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=2,
            gen_cleanup_requested=True,
            generator_termination_cause="supervisor_cleanup",
            length="32MB",
        )
        self.assertEqual(res.status, "environment_error")
        self.assertEqual(res.execution_status, "failed")
        self.assertIn("GENERATOR_NONZERO_EXIT", res.reason_codes)

    def test_cleanup_requested_allows_sigkill(self):
        content = (FIXTURES_DIR / "normal_complete_pass.txt").read_text(encoding="utf-8")
        res = classify_result(
            full_output=content,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=-9,
            gen_cleanup_requested=True,
            generator_termination_cause="supervisor_cleanup",
            length="32MB",
        )
        self.assertEqual(res.status, "pass")
        self.assertEqual(res.execution_status, "ok")
        self.assertEqual(res.statistical_status, "pass")

    def test_truncated_checkpoint_not_counted_as_complete(self):
        text = (
            "RNG_test using PractRand version 0.95\n"
            "RNG = RNG_stdin64, seed = 0x9e3779b97f4a7c15\n"
            "test set = core, folding = standard (64 bit)\n\n"
            "rng=RNG_stdin64, seed=0x9e3779b97f4a7c15\n"
            "length= 16 megabytes (2^24 bytes), time= 0.05 seconds\n"
            "  no anomalies in 60 test result(s)\n\n"
            "rng=RNG_stdin64, seed=0x9e3779b97f4a7c15\n"
            "length= 32 megabytes (2^25 bytes), time= 0.1 seconds\n"
        )
        res = classify_result(
            full_output=text,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=0,
            length="32MB",
        )
        self.assertEqual(res.reported_tested_bytes, 16 * 1024 * 1024)
        self.assertEqual(res.test_count, 60)
        self.assertEqual(res.statistical_status, "insufficient_evidence")
        self.assertEqual(res.status, "inconclusive")
        self.assertIn("INCOMPLETE_TEST_LENGTH", res.reason_codes)

    def test_early_fail_preserved_across_checkpoints(self):
        text = (
            "RNG_test using PractRand version 0.95\n"
            "rng=RNG_stdin64, seed=0x9e3779b97f4a7c15\n"
            "length= 16 megabytes (2^24 bytes), time= 0.05 seconds\n"
            "  Test Name: BCFN(2+0,13-0,T) ... FAIL !\n"
            "  ...and 59 other test result(s)\n\n"
            "rng=RNG_stdin64, seed=0x9e3779b97f4a7c15\n"
            "length= 32 megabytes (2^25 bytes), time= 0.1 seconds\n"
            "  no anomalies in 126 test result(s)\n"
        )
        res = classify_result(
            full_output=text,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=0,
            length="32MB",
        )
        self.assertEqual(res.statistical_status, "failure")
        self.assertEqual(res.status, "statistical_failure")
        self.assertIn("STATISTICAL_FAILURE", res.reason_codes)

    def test_double_fault_generator_crash_and_statistical_fail(self):
        content = (FIXTURES_DIR / "complete_with_fail_exit_0.txt").read_text(encoding="utf-8")
        res = classify_result(
            full_output=content,
            target_bytes=32 * 1024 * 1024,
            pr_returncode=0,
            gen_returncode=-11,
            length="32MB",
        )
        # 算法质量失败具备最高严重度优先级，即使伴随生成器崩溃也应判定为 statistical_failure
        self.assertEqual(res.status, "statistical_failure")
        self.assertEqual(res.execution_status, "failed")
        self.assertEqual(res.statistical_status, "failure")
        self.assertIn("GENERATOR_CRASH", res.reason_codes)
        self.assertIn("STATISTICAL_FAILURE", res.reason_codes)


class TestPractRandSubprocessLifecycle(unittest.TestCase):
    """测试真实子进程超时、监控与优雅回收."""

    def test_silent_subprocess_timeout_and_cleanup(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand=[sys.executable, str(MOCK_RUNNER)],
            engine="sfc64",
            length="32MB",
            timeout_seconds=0.2,
            generator_env=dict(os.environ, MOCK_MODE="silent"),
            tester_env=dict(os.environ, MOCK_MODE="silent"),
        )
        self.assertEqual(res.status, "inconclusive")
        self.assertEqual(res.execution_status, "timeout")
        self.assertEqual(res.generator["termination_cause"], "timed_out")
        self.assertEqual(res.tester["termination_cause"], "timed_out")

    def test_banner_silent_subprocess_timeout(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand=[sys.executable, str(MOCK_RUNNER)],
            engine="sfc64",
            length="32MB",
            timeout_seconds=0.2,
            generator_env=dict(os.environ, MOCK_MODE="silent"),
            tester_env=dict(os.environ, MOCK_MODE="banner_silent"),
        )
        self.assertEqual(res.status, "inconclusive")
        self.assertEqual(res.execution_status, "timeout")

    def test_no_newline_silent_timeout(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand=[sys.executable, str(MOCK_RUNNER)],
            engine="sfc64",
            length="32MB",
            timeout_seconds=0.2,
            generator_env=dict(os.environ, MOCK_MODE="silent"),
            tester_env=dict(os.environ, MOCK_MODE="no_newline_silent"),
        )
        self.assertEqual(res.status, "inconclusive")
        self.assertEqual(res.execution_status, "timeout")

    def test_generator_early_crash_reaped(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand=[sys.executable, str(MOCK_RUNNER)],
            engine="sfc64",
            length="32MB",
            timeout_seconds=1.0,
            generator_env=dict(os.environ, MOCK_MODE="crash"),
            tester_env=dict(os.environ, MOCK_MODE="silent"),
        )
        self.assertEqual(res.status, "environment_error")
        self.assertEqual(res.execution_status, "failed")
        self.assertEqual(res.gen_returncode, 1)

    def test_tester_start_failure_reaps_generator(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand="/nonexistent/binary/path/to/RNG_test",
            engine="sfc64",
            length="32MB",
            timeout_seconds=1.0,
            generator_env=dict(os.environ, MOCK_MODE="silent"),
        )
        self.assertEqual(res.status, "environment_error")
        self.assertEqual(res.execution_status, "failed")
        self.assertEqual(res.generator["termination_cause"], "cancelled")

    def test_normal_complete_reaps_generator(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand=[sys.executable, str(MOCK_RUNNER)],
            engine="sfc64",
            length="32MB",
            timeout_seconds=5.0,
            generator_env=dict(os.environ, MOCK_MODE="generator_infinite"),
            tester_env=dict(os.environ, MOCK_MODE="pass_exit_0"),
        )
        self.assertEqual(res.status, "pass")
        self.assertEqual(res.execution_status, "ok")
        self.assertEqual(res.statistical_status, "pass")
        self.assertEqual(res.tester["returncode"], 0)
        self.assertIn(
            res.generator["termination_cause"],
            ("supervisor_cleanup", "expected_sigpipe", "natural_exit"),
        )

    def test_ignore_sigterm_killed(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand=[sys.executable, str(MOCK_RUNNER)],
            engine="sfc64",
            length="32MB",
            timeout_seconds=0.2,
            generator_env=dict(os.environ, MOCK_MODE="ignore_sigterm"),
            tester_env=dict(os.environ, MOCK_MODE="ignore_sigterm"),
        )
        self.assertEqual(res.status, "inconclusive")
        self.assertEqual(res.execution_status, "timeout")

    def test_tester_crash_with_active_generator(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand=[sys.executable, str(MOCK_RUNNER)],
            engine="sfc64",
            length="32MB",
            timeout_seconds=5.0,
            generator_env=dict(os.environ, MOCK_MODE="generator_infinite"),
            tester_env=dict(os.environ, MOCK_MODE="crash"),
        )
        self.assertEqual(res.status, "environment_error")
        self.assertEqual(res.execution_status, "failed")
        self.assertIn("TESTER_NONZERO_EXIT", res.reason_codes)
        self.assertNotIn("GENERATOR_CRASH", res.reason_codes)
        self.assertIn(res.generator["termination_cause"], ("cancelled", "natural_exit", "expected_sigpipe"))

    def test_generator_stubborn_cleaned_by_supervisor(self):
        res = test_engine(
            generator=[sys.executable, str(MOCK_RUNNER)],
            practrand=[sys.executable, str(MOCK_RUNNER)],
            engine="sfc64",
            length="32MB",
            timeout_seconds=5.0,
            generator_env=dict(os.environ, MOCK_MODE="generator_stubborn"),
            tester_env=dict(os.environ, MOCK_MODE="pass_exit_0"),
        )
        self.assertEqual(res.status, "pass")
        self.assertEqual(res.generator["termination_cause"], "supervisor_cleanup")


class TestAtomicJsonWriteAndSchema(unittest.TestCase):
    """测试结构化结果 Schema 2.0 与原子 JSON 写入."""

    def test_atomic_write_json_success(self):
        with tempfile.TemporaryDirectory() as td:
            target = Path(td) / "results.json"
            data = [{"engine": "sfc64", "status": "pass"}]
            atomic_write_json(target, data)
            self.assertTrue(target.exists())
            with open(target, "r", encoding="utf-8") as f:
                loaded = json.load(f)
            self.assertEqual(loaded, data)

    def test_atomic_write_json_failure_propagates(self):
        target = Path(r"Z:\nonexistent_drive_12345\results.json") if os.name == "nt" else Path("/dev/null/forbidden/results.json")
        with self.assertRaises(OSError):
            atomic_write_json(target, [{"a": 1}])

    def test_test_result_schema_v2(self):
        tr = TestResult(
            engine="sfc64",
            status="environment_error",
            execution_status="failed",
            statistical_status="pass",
            reason="生成器提前异常退出",
            reason_codes=["GENERATOR_CRASH"],
            target_bytes=33554432,
            reported_tested_bytes=33554432,
            generator={"returncode": -11, "termination_cause": "unexpected_signal", "cleanup_requested": False},
            tester={"returncode": 0, "termination_cause": "natural_exit"},
        )
        d = asdict(tr)
        self.assertEqual(d["schema_version"], "2.0")
        self.assertEqual(d["execution_status"], "failed")
        self.assertEqual(d["statistical_status"], "pass")
        self.assertEqual(d["status"], "environment_error")
        self.assertEqual(d["generator"]["returncode"], -11)
        self.assertEqual(d["tester"]["returncode"], 0)
        self.assertIn("GENERATOR_CRASH", d["reason_codes"])


class TestOverallExitCodePriority(unittest.TestCase):
    """测试总体退出码优先级：1 (stat failure) > 2 (env error) > 3 (inconclusive) > 0 (pass)."""

    def test_all_pass(self):
        self.assertEqual(compute_overall_exit_code(["pass", "pass"]), 0)
        self.assertEqual(compute_exit_code(["pass", "pass"]), 0)

    def test_stat_failure_overrides_all(self):
        self.assertEqual(compute_overall_exit_code(["pass", "inconclusive", "statistical_failure"]), 1)
        self.assertEqual(compute_overall_exit_code(["environment_error", "statistical_failure"]), 1)

    def test_env_error_overrides_inconclusive(self):
        self.assertEqual(compute_overall_exit_code(["inconclusive", "environment_error"]), 2)
        self.assertEqual(compute_overall_exit_code(["environment_error", "inconclusive"]), 2)

    def test_inconclusive_over_pass(self):
        self.assertEqual(compute_overall_exit_code(["pass", "inconclusive"]), 3)

    def test_empty_engine_list(self):
        self.assertEqual(compute_overall_exit_code([]), 2)

    def test_with_test_result_objects(self):
        r1 = TestResult(status="pass")
        r2 = TestResult(status="statistical_failure")
        self.assertEqual(compute_overall_exit_code([r1, r2]), 1)


if __name__ == "__main__":
    unittest.main()
