"""交替基准测量的汇总契约。"""

import unittest
from merge_benchmark_repetitions import merge_results


class MergeBenchmarkTests(unittest.TestCase):
    def result(self, cpu_time: float, unit: str = "ns") -> dict:
        return {"context": {}, "benchmarks": [{
            "name": "sample/64", "run_type": "iteration", "time_unit": unit,
            "iterations": 100, "cpu_time": cpu_time, "real_time": cpu_time,
        }]}

    def test_median_preserves_case_and_individual_measurements(self) -> None:
        result = merge_results([self.result(value) for value in (9, 1, 3, 2, 4)])
        rows = result["benchmarks"]
        self.assertEqual(len(rows), 6)
        self.assertEqual(rows[-1]["name"], "sample/64_median")
        self.assertEqual(rows[-1]["aggregate_name"], "median")
        self.assertEqual(rows[-1]["cpu_time"], 3)
        self.assertEqual(rows[-1]["repetitions"], 5)

    def test_missing_case_fails(self) -> None:
        missing = {"context": {}, "benchmarks": []}
        with self.assertRaises(ValueError):
            merge_results([self.result(1), missing])

    def test_mixed_units_fail(self) -> None:
        with self.assertRaises(ValueError):
            merge_results([self.result(1), self.result(1, "ms")])

    def test_failed_measurement_fails(self) -> None:
        failed = self.result(1)
        failed["benchmarks"][0]["error_occurred"] = True
        with self.assertRaises(ValueError):
            merge_results([self.result(1), failed])

    def test_invalid_raw_time_is_not_hidden_by_median(self) -> None:
        for invalid in (float("nan"), float("inf"), -1):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                merge_results([self.result(invalid), self.result(1), self.result(2)])


if __name__ == "__main__":
    unittest.main()
