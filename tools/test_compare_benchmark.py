#!/usr/bin/env python3
"""tools/compare_benchmark.py 单元测试套件."""
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

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


if __name__ == "__main__":
    unittest.main()
