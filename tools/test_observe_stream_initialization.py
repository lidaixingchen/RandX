"""流定位观察产物完整性测试。"""

import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import observe_stream_initialization as observer

from observe_stream_initialization import (
    ENGINES,
    LEGACY_IDS,
    STREAM_IDS,
    validate_first_call_observations,
    validate_steady_report,
)


class StreamObservationTests(unittest.TestCase):
    def setUp(self) -> None:
        cases: tuple[str, ...] = (
            "locate/0", "locate/1", "locate/2", "locate/3", "locate/4", "locate/5", "locate/6",
            "legacy_small_id/0", "legacy_small_id/1", "legacy_small_id/2", "legacy_small_id/4",
            "sequential_by_jump", "seed_construction",
        )
        self.repetitions: int = 5
        self.report: dict = {"benchmarks": [
            {"run_name": f"BM_StreamInitialization/{engine}/{case}",
             "run_type": "aggregate", "aggregate_name": "median", "aggregate_unit": "time",
             "repetitions": self.repetitions, "real_time": 1.0, "cpu_time": 1.0, "time_unit": "ns"}
            for engine in ("xoshiro256ss", "xoroshiro128ss", "xoshiro128ss") for case in cases
        ]}

    def test_complete_report_is_accepted(self) -> None:
        validate_steady_report(self.report, self.repetitions)

    def test_partial_and_duplicate_reports_are_rejected(self) -> None:
        for records in ([], self.report["benchmarks"][:-1],
                        self.report["benchmarks"] + self.report["benchmarks"][:1]):
            with self.subTest(count=len(records)), self.assertRaises(ValueError):
                validate_steady_report({"benchmarks": records}, self.repetitions)

    def test_wrong_protocol_and_zero_time_are_rejected(self) -> None:
        for field, value in (("repetitions", self.repetitions - 1), ("real_time", 0), ("cpu_time", 0)):
            report: dict = copy.deepcopy(self.report)
            report["benchmarks"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_steady_report(report, self.repetitions)

    def test_error_aggregate_units_and_non_finite_times_are_rejected(self) -> None:
        for field, value in (
            ("error_occurred", True),
            ("run_type", "iteration"),
            ("aggregate_unit", "items"),
            ("time_unit", "bogus"),
            ("real_time", float("inf")),
            ("cpu_time", float("nan")),
        ):
            report: dict = copy.deepcopy(self.report)
            report["benchmarks"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_steady_report(report, self.repetitions)

    def make_first_call_observations(self) -> list[dict]:
        return [
            {
                "engine": engine,
                "legacy": legacy,
                "stream_id": str(stream_id),
                "elapsed_ns": 0,
                "first_output": "123",
                "repetition": repetition,
            }
            for engine in ENGINES
            for legacy, stream_ids in ((False, STREAM_IDS), (True, LEGACY_IDS))
            for stream_id in stream_ids
            for repetition in range(self.repetitions)
        ]

    def test_complete_first_call_observations_accept_zero_time(self) -> None:
        validate_first_call_observations(self.make_first_call_observations(), self.repetitions)

    def test_first_call_protocol_and_completeness_are_checked(self) -> None:
        invalid_records: tuple[tuple[str, object], ...] = (
            ("engine", "unknown"),
            ("legacy", "false"),
            ("stream_id", "01"),
            ("elapsed_ns", -1),
            ("elapsed_ns", float("inf")),
            ("first_output", "12x"),
            ("repetition", self.repetitions),
        )
        for field, value in invalid_records:
            observations: list[dict] = self.make_first_call_observations()
            observations[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_first_call_observations(observations, self.repetitions)
        for observations in (
            self.make_first_call_observations()[:-1],
            self.make_first_call_observations() + self.make_first_call_observations()[:1],
        ):
            with self.subTest(count=len(observations)), self.assertRaises(ValueError):
                validate_first_call_observations(observations, self.repetitions)

    def test_permuted_engines_are_rejected_for_each_request(self) -> None:
        def fake_run(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
            destination: str | None = next(
                (argument.split("=", 1)[1] for argument in command if argument.startswith("--benchmark_out=")),
                None,
            )
            if destination is not None:
                Path(destination).write_text(json.dumps(self.report), encoding="utf-8")
                return subprocess.CompletedProcess(command, 0)
            requested_engine: str = command[2]
            returned_engine: str = ENGINES[(ENGINES.index(requested_engine) + 1) % len(ENGINES)]
            result: dict = {
                "engine": returned_engine,
                "legacy": command[1] == "--randx-first-stream-legacy",
                "stream_id": command[3],
                "elapsed_ns": 0,
                "first_output": "123",
            }
            return subprocess.CompletedProcess(command, 0, stdout=json.dumps(result))

        with tempfile.TemporaryDirectory() as temporary_directory:
            directory: Path = Path(temporary_directory)
            binary: Path = directory / "observer-binary"
            binary.touch()
            policy: Path = directory / "policy.json"
            policy.write_text(json.dumps({"groups": {"general": {
                "repetitions": self.repetitions, "min_time": 0.5,
            }}}), encoding="utf-8")
            arguments: list[str] = ["observe", "--binary", str(binary), "--output-dir", str(directory / "output")]
            with patch.object(sys, "argv", arguments), patch.object(observer, "POLICY_PATH", policy), \
                    patch.object(observer.subprocess, "run", side_effect=fake_run), self.assertRaises(ValueError):
                observer.main()


if __name__ == "__main__":
    unittest.main()
