"""PractRand 计划与运行报告汇总决策测试."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

PRACTRAND_DIR: Path = Path(__file__).resolve().parents[1]
if str(PRACTRAND_DIR) not in sys.path:
    sys.path.insert(0, str(PRACTRAND_DIR))

from summarize_practrand import summarize, aggregate_status


class TestSummarizePractRand(unittest.TestCase):
    def test_confirmed_statistical_failure_keeps_priority_with_report_errors(self) -> None:
        self.assertEqual(
            aggregate_status([{"status": "statistical_failure"}], ["另一项目报告损坏"]),
            "statistical_failure",
        )

    def setUp(self) -> None:
        self.expected: dict[str, Any] = {
            "engine": "sfc64",
            "randx_commit": "same-randx-commit",
            "practrand_commit": "same-practrand-commit",
            "profile": "quick",
            "profile_acceptance_status": "initial_unverified",
            "input_width_bits": 64,
            "seed_strategy": "fixed",
            "seed_value": 11400714819323198485,
            "checkpoint_min_bytes": 1024 * 1024,
            "target_bytes": 4 * 1024 * 1024 * 1024,
            "timeout_seconds": 1800,
            "test_parameters": ["stdin64", "-tlmin", "1M", "-tlmax", "4G", "-te", "1"],
        }
        self.actual: dict[str, Any] = {
            **self.expected,
            "status": "pass",
            "phase": "final",
            "execution_status": "ok",
            "statistical_status": "pass",
            "reason": "complete",
            "reported_tested_bytes": self.expected["target_bytes"],
            "test_count": 126,
            "run_suspicious_count": 1,
            "suspicious_markers": [
                {
                    "test_name": "BCFN mildly suspicious",
                    "checkpoint_bytes": self.expected["target_bytes"],
                    "line_number": 4,
                    "log_file": "logs/sfc64.log",
                }
            ],
            "log_file": "logs/sfc64.log",
        }

    def _summarize(self, expected: dict[str, Any] | None = None, actual: dict[str, Any] | None = None,
                   matrix_result: str = "success", plan_result: str = "success") -> dict[str, Any]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            results_dir: Path = root / "results"
            results_dir.mkdir()
            plan_path: Path = root / "plan.json"
            report_path: Path = results_dir / "sfc64.json"
            plan_data: dict[str, Any] = {
                "schema_version": "1.0",
                "profile": "quick",
                "profile_acceptance_status": "initial_unverified",
                "items": [expected or self.expected],
            }
            plan_path.write_text(json.dumps(plan_data), encoding="utf-8")
            if actual is not None:
                report_path.write_text(json.dumps([actual]), encoding="utf-8")
            return summarize(
                plan_path,
                results_dir,
                plan_result=plan_result,
                matrix_result=matrix_result,
            )

    def test_complete_matching_report_passes_and_keeps_run_wide_markers(self) -> None:
        result: dict[str, Any] = self._summarize(actual=self.actual)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["projects"][0]["run_suspicious_count"], 1)
        self.assertEqual(result["projects"][0]["suspicious_markers"][0]["log_file"], "logs/sfc64.log")

    def test_execution_status_is_required_for_complete_pass(self) -> None:
        for status in (None, "unexpected"):
            actual: dict[str, Any] = dict(self.actual)
            if status is None:
                del actual["execution_status"]
            else:
                actual["execution_status"] = status
            with self.subTest(status=status):
                self.assertEqual(self._summarize(actual=actual)["status"], "environment_error")

    def test_identity_mismatches_fail_even_when_commits_still_match(self) -> None:
        mismatched_fields: tuple[tuple[str, Any], ...] = (
            ("seed_value", self.expected["seed_value"] + 1),
            ("input_width_bits", 32),
            ("checkpoint_min_bytes", self.expected["checkpoint_min_bytes"] * 2),
            ("target_bytes", self.expected["target_bytes"] * 2),
            ("test_parameters", ["stdin64", "-tlmin", "2M", "-tlmax", "4G", "-te", "1"]),
        )
        for field, value in mismatched_fields:
            with self.subTest(field=field):
                actual: dict[str, Any] = dict(self.actual)
                actual[field] = value
                result: dict[str, Any] = self._summarize(actual=actual)
                self.assertEqual(result["status"], "environment_error")
                self.assertIn(field, result["projects"][0]["reason"])

    def test_required_identity_field_absence_is_data_mismatch(self) -> None:
        actual: dict[str, Any] = dict(self.actual)
        del actual["seed_value"]
        result: dict[str, Any] = self._summarize(actual=actual)
        self.assertEqual(result["status"], "environment_error")
        self.assertIn("seed_value", result["projects"][0]["reason"])

    def test_missing_cancelled_and_incomplete_projects_are_not_passes(self) -> None:
        missing: dict[str, Any] = self._summarize(actual=None, matrix_result="failure")
        self.assertEqual(missing["status"], "environment_error")
        self.assertTrue(missing["projects"][0]["missing_report"])

        cancelled: dict[str, Any] = self._summarize(actual=None, matrix_result="cancelled")
        self.assertEqual(cancelled["status"], "inconclusive")

        short_result: dict[str, Any] = dict(self.actual)
        short_result["reported_tested_bytes"] = self.expected["target_bytes"] // 2
        short: dict[str, Any] = self._summarize(actual=short_result)
        self.assertEqual(short["status"], "inconclusive")

        running_result: dict[str, Any] = dict(self.actual)
        running_result["phase"] = "running"
        running: dict[str, Any] = self._summarize(actual=running_result, matrix_result="cancelled")
        self.assertEqual(running["status"], "inconclusive")

    def test_statistical_failure_remains_distinct_from_execution_failure(self) -> None:
        failed_result: dict[str, Any] = dict(self.actual)
        failed_result["status"] = "statistical_failure"
        failed_result["statistical_status"] = "failure"
        failed: dict[str, Any] = self._summarize(actual=failed_result)
        self.assertEqual(failed["status"], "statistical_failure")

        environment_result: dict[str, Any] = dict(self.actual)
        environment_result["status"] = "environment_error"
        environment_result["execution_status"] = "failed"
        environment: dict[str, Any] = self._summarize(actual=environment_result)
        self.assertEqual(environment["status"], "environment_error")

    def test_empty_plan_corrupt_report_and_plan_failure_are_environment_errors(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            plan_path: Path = root / "empty-plan.json"
            results_dir: Path = root / "results"
            results_dir.mkdir()
            plan_path.write_text(json.dumps({"items": []}), encoding="utf-8")
            self.assertEqual(summarize(plan_path, results_dir)["status"], "environment_error")

            plan_path.write_text(json.dumps({"items": [self.expected]}), encoding="utf-8")
            (results_dir / "broken.json").write_text("{broken", encoding="utf-8")
            corrupted: dict[str, Any] = summarize(plan_path, results_dir)
            self.assertEqual(corrupted["status"], "environment_error")
            self.assertTrue(any("无法读取" in error for error in corrupted["errors"]))
            (results_dir / "broken.json").unlink()
            failed_plan: dict[str, Any] = summarize(plan_path, results_dir, plan_result="failure")
            self.assertEqual(failed_plan["status"], "environment_error")


if __name__ == "__main__":
    unittest.main()
