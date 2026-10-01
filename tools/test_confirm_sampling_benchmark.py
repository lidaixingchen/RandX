"""抽样性能固定确认与完整门禁的契约测试。"""

from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch
from typing import Any

from confirm_sampling_benchmark import confirm, replace_measurements, select_confirmation_cases
from merge_benchmark_repetitions import merge_results


def measurement(case: str, value: float) -> dict[str, Any]:
    return {"name": case, "run_type": "iteration", "time_unit": "ns", "cpu_time": value, "real_time": value, "iterations": 100}


def aggregated(values: dict[str, float]) -> dict[str, Any]:
    return merge_results([{"context": {}, "benchmarks": [measurement(case, value) for case, value in values.items()]}])


def medians(result: dict[str, Any]) -> dict[str, dict]:
    return {entry["name"]: entry for entry in result["benchmarks"] if entry.get("aggregate_name") == "median"}


class SelectionTests(unittest.TestCase):
    def test_selects_failure_and_adjacent_requests(self) -> None:
        values: dict[str, float] = {f"sample/range_size:100/request:{request}": 100 for request in (0, 1, 4, 5, 50)}
        candidate: dict[str, float] = {**values, "sample/range_size:100/request:4": 130}
        selected: list[str] = select_confirmation_cases(medians(aggregated(values)), medians(aggregated(candidate)), 0.25)
        self.assertEqual(set(selected), {f"sample/range_size:100/request:{request}" for request in (1, 4, 5)})

    def test_empty_range_includes_nearest_nonempty_range(self) -> None:
        values: dict[str, float] = {f"sample/range_size:{size}/request:{request}": 100 for size, request in ((0, 1), (1, 1), (1, 2), (256, 1))}
        candidate: dict[str, float] = {**values, "sample/range_size:0/request:1": 130}
        self.assertEqual(len(select_confirmation_cases(medians(aggregated(values)), medians(aggregated(candidate)), 0.25)), 3)

    def test_set_difference_fails_before_selection(self) -> None:
        entries: dict[str, dict] = medians(aggregated({"sample/range_size:1/request:1": 1}))
        with self.assertRaises(ValueError):
            select_confirmation_cases(entries, {}, 0.25)

    def test_unknown_parameter_format_fails(self) -> None:
        entries: dict[str, dict] = medians(aggregated({"sample": 1}))
        with self.assertRaises(ValueError):
            select_confirmation_cases(entries, entries, 0.25)

    def test_confirmation_set_must_match_selected_cases(self) -> None:
        result: dict[str, Any] = aggregated({"sample/range_size:1/request:1": 1})
        with self.assertRaises(ValueError):
            replace_measurements(result, result, ["sample/range_size:1/request:2"])


class ConfirmationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root: Path = Path(self.temporary.name)
        self.case: str = "sample/range_size:256/request:0"
        self.other: str = "other/range_size:256/request:1"
        self.baseline: Path = self.root / "baseline-initial.json"
        self.candidate: Path = self.root / "candidate-initial.json"
        self.output: Path = self.root / "confirmation"
        self.baseline.write_text(json.dumps(aggregated({self.case: 100, self.other: 100})), encoding="utf-8")
        self.candidate.write_text(json.dumps(aggregated({self.case: 130, self.other: 100})), encoding="utf-8")

    def runner(self, candidate_time: float = 100, returncode: int = 0) -> Any:
        def run(command: list[str], **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
            output: Path = Path(next(argument.split("=", 1)[1] for argument in command if argument.startswith("--benchmark_out=")))
            value: float = candidate_time if Path(command[0]).name == "candidate" else 100
            output.write_text(json.dumps({"context": {}, "benchmarks": [measurement(self.case, value)]}), encoding="utf-8")
            return subprocess.CompletedProcess(command, returncode, b"raw stdout", b"raw stderr")
        return run

    def run_confirmation(self) -> None:
        with redirect_stdout(io.StringIO()):
            confirm(self.baseline, self.candidate, Path("baseline"), Path("candidate"), self.output, 6, "1s", 0.25, None)

    def test_fixed_balanced_rounds_replace_only_confirmed_cases(self) -> None:
        with patch("confirm_sampling_benchmark.subprocess.run", side_effect=self.runner()) as runner:
            self.run_confirmation()
        self.assertEqual(runner.call_count, 12)
        metadata: dict[str, Any] = json.loads((self.output / "metadata.json").read_text(encoding="utf-8"))
        self.assertEqual([run["variant"] for run in metadata["runs"][::2]], ["baseline", "candidate"] * 3)
        self.assertTrue(metadata["complete"])
        for call in runner.call_args_list:
            self.assertIn("--benchmark_min_time=1s", call.args[0])
        accepted: dict[str, Any] = json.loads((self.output / "candidate.json").read_text(encoding="utf-8"))
        entries: dict[str, dict] = medians(accepted)
        self.assertEqual(len(entries), 2)
        self.assertEqual(entries[self.case + "_median"]["cpu_time"], 100)
        self.assertEqual(entries[self.other + "_median"]["cpu_time"], 100)
        self.assertEqual(len(list(self.output.glob("*.stdout.txt"))), 12)
        self.assertEqual(json.loads(self.candidate.read_text(encoding="utf-8"))["benchmarks"][-2]["cpu_time"], 130)

    def test_stable_regression_remains_a_gate_failure(self) -> None:
        with patch("confirm_sampling_benchmark.subprocess.run", side_effect=self.runner(candidate_time=130)) as runner:
            self.run_confirmation()
        self.assertEqual(runner.call_count, 12)
        process: subprocess.CompletedProcess[bytes] = subprocess.run(
            [sys.executable, str(Path(__file__).with_name("compare_benchmark.py")),
             str(self.output / "candidate.json"), str(self.output / "baseline.json"), "--tolerance", "0.25"],
            capture_output=True, check=False,
        )
        self.assertEqual(process.returncode, 1, process.stderr)

    def test_process_failure_keeps_raw_logs_and_stops_confirmation(self) -> None:
        with patch("confirm_sampling_benchmark.subprocess.run", side_effect=self.runner(returncode=9)) as runner:
            with self.assertRaises(ValueError):
                self.run_confirmation()
        self.assertEqual(runner.call_count, 1)
        self.assertEqual((self.output / "baseline-1.stderr.txt").read_bytes(), b"raw stderr")
        self.assertFalse((self.output / "candidate.json").exists())

    def test_no_regression_preserves_full_initial_results(self) -> None:
        self.candidate.write_bytes(self.baseline.read_bytes())
        with patch("confirm_sampling_benchmark.subprocess.run") as runner:
            self.run_confirmation()
        runner.assert_not_called()
        self.assertEqual(json.loads((self.output / "candidate.json").read_text(encoding="utf-8")), json.loads(self.candidate.read_text(encoding="utf-8")))

    def test_unbalanced_round_count_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            confirm(self.baseline, self.candidate, Path("baseline"), Path("candidate"), self.output, 5, "1s", 0.25, None)

    def test_native_result_encoding_is_explicit_and_raw_file_is_preserved(self) -> None:
        def run(command: list[str], **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
            output: Path = Path(next(argument.split("=", 1)[1] for argument in command if argument.startswith("--benchmark_out=")))
            result: dict[str, Any] = {"context": {"host_name": "中文主机"}, "benchmarks": [measurement(self.case, 100)]}
            output.write_text(json.dumps(result, ensure_ascii=False), encoding="gbk")
            return subprocess.CompletedProcess(command, 0, b"", b"")
        with patch("confirm_sampling_benchmark.subprocess.run", side_effect=run), redirect_stdout(io.StringIO()):
            confirm(self.baseline, self.candidate, Path("baseline"), Path("candidate"), self.output, 6, "1s", 0.25, None, "gbk")
        self.assertIn("中文主机".encode("gbk"), (self.output / "baseline-1.json").read_bytes())
        self.assertTrue((self.output / "candidate.json").is_file())


if __name__ == "__main__":
    unittest.main()
