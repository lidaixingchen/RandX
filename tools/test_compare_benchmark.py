#!/usr/bin/env python3
"""tools/compare_benchmark.py 单元测试套件."""
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from compare_benchmark import (
    BenchmarkDataError,
    ComparisonResult,
    compare_results,
    load_results,
    normalize_ms,
    parse_results,
)

COMPARE_SCRIPT = Path(__file__).resolve().parent / "compare_benchmark.py"


class TestCompareBenchmark(unittest.TestCase):
    def run_compare(self, current_data: dict, baseline_data: dict, extra_args: list[str] = None):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as cur_f, \
             tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as base_f:
            json.dump(current_data, cur_f)
            json.dump(baseline_data, base_f)
            cur_path = cur_f.name
            base_path = base_f.name

        try:
            cmd = [sys.executable, str(COMPARE_SCRIPT), cur_path, base_path]
            if extra_args:
                cmd.extend(extra_args)
            res = subprocess.run(cmd, capture_output=True, text=True)
            return res.returncode, res.stdout, res.stderr
        finally:
            Path(cur_path).unlink(missing_ok=True)
            Path(base_path).unlink(missing_ok=True)

    def test_missing_benchmark_in_current_fails(self):
        baseline = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
                {"name": "BM_B/median", "aggregate_name": "median", "cpu_time": 20.0, "time_unit": "ns"},
            ]
        }
        current = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
            ]
        }
        code, out, err = self.run_compare(current, baseline)
        self.assertEqual(code, 2)
        self.assertIn("BM_B/median", err)

    def test_missing_benchmark_with_allow_missing_passes(self):
        baseline = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
                {"name": "BM_B/median", "aggregate_name": "median", "cpu_time": 20.0, "time_unit": "ns"},
            ]
        }
        current = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
            ]
        }
        code, out, err = self.run_compare(current, baseline, extra_args=["--allow-missing"])
        self.assertEqual(code, 0)
        self.assertIn("BM_A/median", out)

    def test_error_occurred_in_current_fails(self):
        baseline = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
                {"name": "BM_B/median", "aggregate_name": "median", "cpu_time": 20.0, "time_unit": "ns"},
            ]
        }
        current = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
                {"name": "BM_B/median", "error_occurred": True, "error_message": "operation failed"},
            ]
        }
        code, out, err = self.run_compare(current, baseline)
        self.assertEqual(code, 2)
        self.assertIn("BM_B/median", err)
        self.assertIn("operation failed", err)

    def test_nan_cpu_time_fails(self):
        baseline = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
            ]
        }
        current = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": float("nan"), "time_unit": "ns"},
            ]
        }
        code, out, err = self.run_compare(current, baseline)
        self.assertEqual(code, 2)
        self.assertIn("cpu_time", err)

    def test_negative_cpu_time_fails(self):
        baseline = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
            ]
        }
        current = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": -10.0, "time_unit": "ns"},
            ]
        }
        code, out, err = self.run_compare(current, baseline)
        self.assertEqual(code, 2)
        self.assertIn("cpu_time", err)

    def test_regression_detected(self):
        baseline = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
            ]
        }
        current = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 100.0, "time_unit": "ns"},
            ]
        }
        code, out, err = self.run_compare(current, baseline, extra_args=["--tolerance", "0.25"])
        self.assertEqual(code, 1)
        self.assertIn("REGRESSION", out)

    def test_no_regression_passes(self):
        baseline = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 10.0, "time_unit": "ns"},
            ]
        }
        current = {
            "benchmarks": [
                {"name": "BM_A/median", "aggregate_name": "median", "cpu_time": 11.0, "time_unit": "ns"},
            ]
        }
        code, out, err = self.run_compare(current, baseline, extra_args=["--tolerance", "0.25"])
        self.assertEqual(code, 0)
        self.assertIn("OK", out)

    def test_exact_tolerance_boundary_is_not_a_regression(self):
        baseline = {"benchmarks": [{"name": "BM_A", "aggregate_name": "median", "cpu_time": 100.0, "time_unit": "ms"}]}
        current = {"benchmarks": [{"name": "BM_A", "aggregate_name": "median", "cpu_time": 125.0, "time_unit": "ms"}]}
        code, out, err = self.run_compare(current, baseline, extra_args=["--tolerance", "0.25"])
        self.assertEqual(code, 0)
        self.assertIn("+25.0%", out)
        self.assertIn("OK", out)
        self.assertEqual(err, "")

    def test_duplicate_median_fails(self):
        result = {
            "benchmarks": [
                {"name": "BM_A_median", "run_name": "BM_A", "aggregate_name": "median", "cpu_time": 1.0, "time_unit": "ms"},
                {"name": "BM_A_median", "run_name": "BM_A", "aggregate_name": "median", "cpu_time": 2.0, "time_unit": "ms"},
            ]
        }
        code, out, err = self.run_compare(result, result)
        self.assertEqual(code, 2)
        self.assertIn("重复 median", err)


class ComparisonApiTests(unittest.TestCase):
    def test_normalize_ms_supports_all_benchmark_units(self):
        self.assertEqual(normalize_ms(1_000_000.0, "ns"), 1.0)
        self.assertEqual(normalize_ms(1_000.0, "us"), 1.0)
        self.assertEqual(normalize_ms(1.0, "ms"), 1.0)
        self.assertEqual(normalize_ms(0.001, "s"), 1.0)

    def test_zero_baseline_uses_fixed_unit_change_and_strict_threshold(self):
        baseline = parse_results({"benchmarks": [{"name": "BM_A", "aggregate_name": "median", "cpu_time": 0.0, "time_unit": "ms"}]})
        same: ComparisonResult = compare_results(
            parse_results({"benchmarks": [{"name": "BM_A", "aggregate_name": "median", "cpu_time": 0.0, "time_unit": "ms"}]}),
            baseline,
            0.0,
        )
        self.assertEqual(same.items[0].change, 0.0)
        self.assertFalse(same.items[0].regression)

        current = parse_results({"benchmarks": [{"name": "BM_A", "aggregate_name": "median", "cpu_time": 2.0, "time_unit": "ms"}]})
        equal: ComparisonResult = compare_results(current, baseline, 1.0)
        self.assertEqual(equal.items[0].change, 1.0)
        self.assertFalse(equal.items[0].regression)
        exceeded: ComparisonResult = compare_results(current, baseline, 0.99)
        self.assertTrue(exceeded.items[0].regression)

    def test_invalid_api_data_raises_data_error_without_exiting(self):
        with self.assertRaises(BenchmarkDataError):
            parse_results({"benchmarks": [{"name": "BM_A", "aggregate_name": "median", "cpu_time": float("nan"), "time_unit": "ms"}]})
        with self.assertRaises(BenchmarkDataError):
            load_results(Path("missing-benchmark-results.json"))
        with self.assertRaises(BenchmarkDataError):
            normalize_ms(1.0, "minutes")
        with self.assertRaises(BenchmarkDataError):
            compare_results({}, {}, -0.1)


if __name__ == "__main__":
    unittest.main()
