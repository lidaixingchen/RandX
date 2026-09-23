#!/usr/bin/env python3
"""tools/pract_rand/run_practrand.py 驱动与输出解析单元测试套件."""

import unittest
from pathlib import Path
import sys

# 将上一级目录加入 sys.path
TESTS_DIR = Path(__file__).resolve().parent
PRACTRAND_DIR = TESTS_DIR.parent
FIXTURES_DIR = TESTS_DIR / "fixtures"

if str(PRACTRAND_DIR) not in sys.path:
    sys.path.insert(0, str(PRACTRAND_DIR))

from run_practrand import (
    classify_result,
    compute_exit_code,
    compute_overall_exit_code,
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


class TestPractRandStatusCategorization(unittest.TestCase):
    """测试状态分类规则（覆盖方案 3.5 要求的 12 种场景）."""

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

    def test_with_test_result_objects(self):
        r1 = TestResult(status="pass")
        r2 = TestResult(status="statistical_failure")
        self.assertEqual(compute_overall_exit_code([r1, r2]), 1)


if __name__ == "__main__":
    unittest.main()
