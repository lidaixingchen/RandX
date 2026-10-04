"""验证三角基准记录的验收契约。"""
import copy
import unittest

from check_triangular_benchmark import EXPECTED_CALLS, check_results
from compare_benchmark import BenchmarkDataError


def fixture():
    entries = []
    for name, calls in EXPECTED_CALLS.items():
        common = {"name": name, "run_name": name, "cpu_time": 1.0, "time_unit": "ns",
                  "engine_calls_per_sample": calls}
        entries.append({**common, "run_type": "iteration", "repetition_index": 0})
        entries.append({**common, "run_type": "aggregate", "aggregate_name": "median"})
    return {"benchmarks": entries}


class TriangularBenchmarkTests(unittest.TestCase):
    def test_complete_measurement(self):
        check_results(fixture(), 1)

    def test_nondegenerate_zero_consumption(self):
        data = fixture()
        data["benchmarks"][0]["engine_calls_per_sample"] = 0
        with self.assertRaisesRegex(BenchmarkDataError, "引擎消耗"):
            check_results(data, 1)

    def test_degenerate_consumption(self):
        data = fixture()
        next(row for row in data["benchmarks"] if "/degenerate" in row["name"])["engine_calls_per_sample"] = 1
        with self.assertRaisesRegex(BenchmarkDataError, "引擎消耗"):
            check_results(data, 1)

    def test_benchmark_error(self):
        data = fixture()
        data["benchmarks"][0]["error_occurred"] = True
        with self.assertRaises(BenchmarkDataError):
            check_results(data, 1)

    def test_missing_scenario(self):
        data = fixture()
        data["benchmarks"] = data["benchmarks"][2:]
        with self.assertRaisesRegex(BenchmarkDataError, "全部"):
            check_results(data, 1)

    def test_missing_repetition(self):
        with self.assertRaisesRegex(BenchmarkDataError, "重复"):
            check_results(fixture(), 2)

    def test_duplicate_repetition_index(self):
        data = fixture()
        data["benchmarks"] += [copy.deepcopy(row) for row in data["benchmarks"] if row["run_type"] == "iteration"]
        with self.assertRaisesRegex(BenchmarkDataError, "重复"):
            check_results(data, 2)


if __name__ == "__main__":
    unittest.main()
