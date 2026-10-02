"""tools/benchmark_gate.py 策略与文件适配测试。"""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

from benchmark_gate import evaluate_run, plan_run
from compare_benchmark import parse_results
from confirm_sampling_benchmark import replace_measurements, select_confirmation_cases
from merge_benchmark_repetitions import merge_results


TOOLS_DIR: Path = Path(__file__).resolve().parent
POLICY_PATH: Path = TOOLS_DIR / "benchmark_policy.json"
GATE_SCRIPT: Path = TOOLS_DIR / "benchmark_gate.py"
BENCHMARK_NAME: str = "BM_Sample/range_size:1/request:1"


def policy_value() -> dict[str, Any]:
    return json.loads(POLICY_PATH.read_text(encoding="utf-8"))


def run_context(
    event: str = "pull_request",
    ref: str = "refs/pull/17/merge",
    force_update: bool = False,
    fork_pr: bool = False,
    cancelled: bool = False,
) -> dict[str, Any]:
    return {
        "repository": "example/RandX",
        "repository_id": 73001,
        "event": event,
        "ref": ref,
        "run_id": "9100",
        "run_attempt": 1,
        "candidate_commit": "candidate-sha",
        "head_commit": "head-sha",
        "fork_pr": fork_pr,
        "force_update_baseline": force_update,
        "cancelled": cancelled,
    }


def artifact_record(
    artifact_id: int,
    run_id: str,
    created_at: str,
    run_overrides: dict[str, Any] | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    run: dict[str, Any] = {
        "id": int(run_id),
        "path": ".github/workflows/benchmark.yml@refs/heads/master",
        "event": "push",
        "repository": {"id": 73001, "full_name": "example/RandX"},
        "head_repository": {"id": 73001, "full_name": "example/RandX"},
        "head_branch": "master",
        "head_sha": f"baseline-{run_id}",
        "status": "completed",
        "conclusion": "success",
        "created_at": created_at,
    }
    if run_overrides:
        run.update(run_overrides)
    artifact: dict[str, Any] = {
        "id": artifact_id,
        "name": "benchmark-baseline",
        "expired": False,
        "created_at": created_at,
        "workflow_run": {
            "id": int(run_id),
            "repository_id": 73001,
            "head_repository_id": 73001,
        },
    }
    return artifact, run


def successful_query() -> dict[str, Any]:
    artifact, run = artifact_record(101, "8100", "2026-09-30T12:00:00Z")
    return {"success": True, "artifacts": [artifact], "runs": {"8100": run}, "error": ""}


def median_result(values: dict[str, float]) -> dict[str, Any]:
    return {
        "context": {},
        "benchmarks": [
            {
                "name": f"{name}_median",
                "run_name": name,
                "run_type": "aggregate",
                "aggregate_name": "median",
                "aggregate_unit": "time",
                "cpu_time": value,
                "real_time": value,
                "time_unit": "ns",
                "repetitions": 5,
                "iterations": 5,
            }
            for name, value in values.items()
        ],
    }


def raw_result(values: dict[str, float]) -> dict[str, Any]:
    return {
        "context": {},
        "benchmarks": [
            {
                "name": name,
                "run_type": "iteration",
                "cpu_time": value,
                "real_time": value,
                "time_unit": "ns",
                "iterations": 1,
            }
            for name, value in values.items()
        ],
    }


def execution_record() -> dict[str, Any]:
    return {
        "plan_job": "success",
        "jobs": {"general": "success", "sampling_cpp17": "success", "sampling_cpp23": "success"},
        "downloads": {"general": "success", "sampling_cpp17": "success", "sampling_cpp23": "success"},
        "errors": [],
    }


def complete_group(
    group_name: str,
    plan: dict[str, Any],
    candidate_time: float = 100.0,
    baseline_time: float = 100.0,
) -> dict[str, Any]:
    required_steps: list[str] = plan["required_steps"][group_name]
    baseline_commit: str | None
    if group_name == "general" and plan["mode"] == "REFRESH":
        baseline_commit = None
    elif group_name == "general":
        baseline_commit = plan["baseline"]["commit"]
    else:
        baseline_commit = plan["sampling_commit"]
    metadata: dict[str, Any] = {
        "format_version": 1,
        "group": group_name,
        "run_id": str(plan["context"]["run_id"]),
        "run_attempt": plan["context"]["run_attempt"],
        "candidate_commit": plan["context"]["candidate_commit"],
        "baseline_commit": baseline_commit,
        "steps": {step: "success" for step in required_steps},
        "expected": {"candidate": [BENCHMARK_NAME], "baseline": [BENCHMARK_NAME]},
        "environment": {"cpu": 1, "runner": "ubuntu-latest"},
    }
    if group_name == "general":
        metadata["expected"]["candidate"] = ["BM_General", "BM_GeneralNew"]
        metadata["expected"]["baseline"] = [] if plan["mode"] == "REFRESH" else ["BM_General"]
        files: dict[str, Any] = {
            "candidate.json": median_result({"BM_General": candidate_time, "BM_GeneralNew": 150.0}),
        }
        if plan["mode"] == "COMPARE":
            files["baseline.json"] = median_result({"BM_General": baseline_time})
        return {"metadata": metadata, "files": files, "load_errors": []}

    candidate_rounds: list[dict[str, Any]] = [
        raw_result({BENCHMARK_NAME: candidate_time}) for _ in range(5)
    ]
    baseline_rounds: list[dict[str, Any]] = [
        raw_result({BENCHMARK_NAME: baseline_time}) for _ in range(5)
    ]
    candidate_initial: dict[str, Any] = merge_results(candidate_rounds)
    baseline_initial: dict[str, Any] = merge_results(baseline_rounds)
    candidate_medians: dict[str, dict[str, Any]] = parse_results(candidate_initial)
    baseline_medians: dict[str, dict[str, Any]] = parse_results(baseline_initial)
    selected_cases: list[str] = select_confirmation_cases(baseline_medians, candidate_medians, 0.25)
    confirmation_rounds: dict[str, list[dict[str, Any]]] = {"candidate": [], "baseline": []}
    files = {
        "candidate-initial.json": candidate_initial,
        "baseline-initial.json": baseline_initial,
        "confirmation/metadata.json": {
            "complete": True,
            "cases": selected_cases,
            "repetitions": 6,
            "min_time": "1s",
            "tolerance": 0.25,
            "cpu": 1,
            "runs": [],
        },
    }
    for variant, measurement_time in (("candidate", candidate_time), ("baseline", baseline_time)):
        for round_number, round_result in enumerate(
            candidate_rounds if variant == "candidate" else baseline_rounds,
            start=1,
        ):
            files[f"rounds/{variant}-{round_number}.json"] = round_result
        if selected_cases:
            for round_number in range(1, 7):
                measured: dict[str, Any] = raw_result({BENCHMARK_NAME: measurement_time})
                confirmation_rounds[variant].append(measured)
                files[f"confirmation/rounds/{variant}-{round_number}.json"] = measured
                files["confirmation/metadata.json"]["runs"].append({
                    "variant": variant,
                    "round": round_number,
                    "returncode": 0,
                    "command": ["benchmark"],
                })
    if selected_cases:
        candidate_confirmation: dict[str, Any] = replace_measurements(
            candidate_initial, merge_results(confirmation_rounds["candidate"]), selected_cases,
        )
        baseline_confirmation: dict[str, Any] = replace_measurements(
            baseline_initial, merge_results(confirmation_rounds["baseline"]), selected_cases,
        )
    else:
        candidate_confirmation = candidate_initial
        baseline_confirmation = baseline_initial
    files["confirmation/candidate.json"] = candidate_confirmation
    files["confirmation/baseline.json"] = baseline_confirmation
    return {"metadata": metadata, "files": files, "load_errors": []}


def complete_groups(plan: dict[str, Any], candidate_time: float = 100.0, baseline_time: float = 100.0) -> dict[str, Any]:
    return {
        group_name: complete_group(group_name, plan, candidate_time, baseline_time)
        for group_name in plan["groups"]
    }


class PlanRunTests(unittest.TestCase):
    def test_selects_latest_acceptable_record_from_complete_list(self) -> None:
        policy: dict[str, Any] = policy_value()
        context: dict[str, Any] = run_context()
        older_artifact, older_run = artifact_record(100, "8000", "2026-09-28T12:00:00Z")
        latest_artifact, latest_run = artifact_record(102, "8200", "2026-10-01T12:00:00Z")
        wrong_workflow, wrong_workflow_run = artifact_record(
            103, "8300", "2026-10-02T12:00:00Z", {"path": ".github/workflows/ci.yml@refs/heads/master"},
        )
        fork_artifact, fork_run = artifact_record(
            104, "8400", "2026-10-03T12:00:00Z", {"head_repository": {"id": 999, "full_name": "fork/RandX"}},
        )
        failed_artifact, failed_run = artifact_record(
            105, "8500", "2026-10-04T12:00:00Z", {"conclusion": "failure"},
        )
        artifacts: list[dict[str, Any]] = [
            {"id": index, "name": "other-artifact", "expired": False}
            for index in range(1, 32)
        ]
        artifacts.extend([older_artifact, wrong_workflow, fork_artifact, failed_artifact, latest_artifact])
        runs = {
            "8000": older_run,
            "8200": latest_run,
            "8300": wrong_workflow_run,
            "8400": fork_run,
            "8500": failed_run,
        }
        plan: dict[str, Any] = plan_run(policy, context, {"success": True, "artifacts": artifacts, "runs": runs})
        self.assertTrue(plan["valid"])
        self.assertEqual(plan["baseline"]["artifact_id"], 102)
        self.assertEqual(plan["baseline"]["run_id"], "8200")
        self.assertEqual(plan["baseline"]["commit"], "baseline-8200")

    def test_excludes_current_run_even_when_it_is_newest(self) -> None:
        policy: dict[str, Any] = policy_value()
        context: dict[str, Any] = run_context("push", "refs/heads/master")
        context["run_id"] = "8200"
        artifact, run = artifact_record(102, "8200", "2026-10-01T12:00:00Z")
        plan: dict[str, Any] = plan_run(policy, context, {"success": True, "artifacts": [artifact], "runs": {"8200": run}})
        self.assertFalse(plan["valid"])
        self.assertEqual(plan["errors"][0]["status"], "BASELINE_ERROR")

    def test_incomplete_latest_source_is_not_silently_replaced_by_older_baseline(self) -> None:
        policy: dict[str, Any] = policy_value()
        context: dict[str, Any] = run_context()
        older_artifact, older_run = artifact_record(100, "8000", "2026-09-28T12:00:00Z")
        latest_artifact, latest_run = artifact_record(
            102, "8200", "2026-10-01T12:00:00Z", {"head_sha": None},
        )
        plan: dict[str, Any] = plan_run(
            policy, context,
            {"success": True, "artifacts": [older_artifact, latest_artifact], "runs": {"8000": older_run, "8200": latest_run}},
        )
        self.assertFalse(plan["valid"])
        self.assertEqual(plan["errors"][0]["status"], "DATA_ERROR")
        self.assertIn("head_sha", plan["errors"][0]["message"])

    def test_query_failure_and_empty_normal_history_are_errors(self) -> None:
        policy: dict[str, Any] = policy_value()
        context: dict[str, Any] = run_context()
        query_failure: dict[str, Any] = plan_run(
            policy, context, {"success": False, "artifacts": [], "runs": {}, "error": "API unavailable"},
        )
        self.assertEqual(query_failure["errors"][0]["status"], "EXECUTION_ERROR")
        empty_history: dict[str, Any] = plan_run(
            policy, context, {"success": True, "artifacts": [], "runs": {}, "error": ""},
        )
        self.assertEqual(empty_history["errors"][0]["status"], "BASELINE_ERROR")

    def test_explicit_target_refresh_uses_initial_sampling_commit(self) -> None:
        policy: dict[str, Any] = policy_value()
        context: dict[str, Any] = run_context("workflow_dispatch", "refs/heads/master", True)
        plan: dict[str, Any] = plan_run(
            policy, context, {"success": True, "artifacts": [], "runs": {}, "error": ""},
        )
        self.assertTrue(plan["valid"])
        self.assertEqual(plan["mode"], "REFRESH")
        self.assertIsNone(plan["baseline"])
        self.assertEqual(plan["sampling_commit"], policy["baseline"]["initial_sampling_commit"])
        self.assertEqual(len(plan["required_steps"]["general"]), 4)
        self.assertIn("confirm", plan["required_steps"]["sampling_cpp17"])

    def test_refresh_request_from_other_ref_is_context_error(self) -> None:
        context: dict[str, Any] = run_context("workflow_dispatch", "refs/heads/feature", True)
        plan: dict[str, Any] = plan_run(policy_value(), context, successful_query())
        self.assertFalse(plan["valid"])
        self.assertEqual(plan["errors"][0]["status"], "CONTEXT_ERROR")

    def test_cancelled_context_disables_plan(self) -> None:
        context: dict[str, Any] = run_context(cancelled=True)
        plan: dict[str, Any] = plan_run(policy_value(), context, successful_query())
        self.assertFalse(plan["valid"])
        self.assertEqual(plan["mode"], "CANCELLED")
        self.assertEqual(plan["errors"][0]["status"], "CANCELLED")


class EvaluateRunTests(unittest.TestCase):
    def make_valid_case(
        self,
        candidate_time: float = 100.0,
        baseline_time: float = 100.0,
        context: dict[str, Any] | None = None,
    ) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
        selected_context: dict[str, Any] = context or run_context("push", "refs/heads/master")
        plan: dict[str, Any] = plan_run(policy_value(), selected_context, successful_query())
        groups: dict[str, Any] = complete_groups(plan, candidate_time, baseline_time)
        return plan, execution_record(), groups

    def test_successful_target_push_is_publishable_after_all_groups_pass(self) -> None:
        plan, execution, groups = self.make_valid_case()
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["exit_code"], 0)
        self.assertTrue(report["publish_baseline"])
        self.assertFalse(report["notify_regression"])
        self.assertTrue(report["report_ready"])
        self.assertTrue(report["summary_ready"])

    def test_tolerance_boundary_passes_and_larger_change_regresses(self) -> None:
        plan, execution, groups = self.make_valid_case(5.0, 4.0, run_context())
        boundary: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(boundary["exit_code"], 0)
        plan, execution, groups = self.make_valid_case(5.01, 4.0, run_context())
        above_boundary: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(above_boundary["exit_code"], 1)
        self.assertEqual(above_boundary["groups"]["general"]["regressions"], ["BM_General"])
        self.assertTrue(above_boundary["notify_regression"])
        self.assertFalse(above_boundary["publish_baseline"])

    def test_ordinary_target_dispatch_compares_without_publishing(self) -> None:
        plan, execution, groups = self.make_valid_case(
            context=run_context("workflow_dispatch", "refs/heads/master"),
        )
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["exit_code"], 0)
        self.assertFalse(report["publish_baseline"])

    def test_wrong_artifact_identity_cannot_supply_regression_evidence(self) -> None:
        plan, execution, groups = self.make_valid_case(140.0, 100.0)
        for group in groups.values():
            group["metadata"]["run_attempt"] += 1
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["exit_code"], 2)
        self.assertFalse(report["notify_regression"])
        self.assertFalse(report["publish_baseline"])
        self.assertEqual(report["regressions"], [])

    def test_fork_pr_regression_is_reported_without_notification(self) -> None:
        context: dict[str, Any] = run_context(fork_pr=True)
        plan, execution, groups = self.make_valid_case(140.0, 100.0, context)
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["exit_code"], 1)
        self.assertFalse(report["notify_regression"])
        self.assertFalse(report["publish_baseline"])
        self.assertEqual(report["groups"]["sampling_cpp17"]["status"], "REGRESSION")

    def test_refresh_waives_regression_and_can_publish_complete_results(self) -> None:
        context: dict[str, Any] = run_context("workflow_dispatch", "refs/heads/master", True)
        plan: dict[str, Any] = plan_run(
            policy_value(), context, {"success": True, "artifacts": [], "runs": {}, "error": ""},
        )
        groups: dict[str, Any] = complete_groups(plan, 140.0, 100.0)
        report: dict[str, Any] = evaluate_run(plan, execution_record(), groups)
        self.assertEqual(report["exit_code"], 0)
        self.assertTrue(report["publish_baseline"])
        self.assertFalse(report["notify_regression"])
        self.assertEqual(len(report["waived_regressions"]), 2)
        self.assertTrue(report["groups"]["sampling_cpp17"]["waived"])

    def test_regression_and_other_group_data_error_are_both_retained(self) -> None:
        plan, execution, groups = self.make_valid_case(140.0, 100.0, run_context())
        groups["sampling_cpp23"]["files"]["rounds/candidate-3.json"] = raw_result({"other-case": 100.0})
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["exit_code"], 2)
        self.assertTrue(report["notify_regression"])
        self.assertEqual(report["groups"]["sampling_cpp17"]["regressions"], [BENCHMARK_NAME])
        self.assertEqual(report["groups"]["sampling_cpp23"]["status"], "DATA_ERROR")
        self.assertIn("sampling_cpp17", {entry["group"] for entry in report["regressions"]})

    def test_job_failure_keeps_valid_comparison_evidence_and_blocks_publish(self) -> None:
        plan, execution, groups = self.make_valid_case(140.0, 100.0)
        execution["jobs"]["sampling_cpp17"] = "failure"
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        group: dict[str, Any] = report["groups"]["sampling_cpp17"]
        self.assertEqual(group["status"], "EXECUTION_ERROR")
        self.assertEqual(group["regressions"], [BENCHMARK_NAME])
        self.assertTrue(report["notify_regression"])
        self.assertFalse(report["publish_baseline"])
        self.assertEqual(report["exit_code"], 2)

    def test_step_outcome_failure_is_not_hidden_by_successful_job(self) -> None:
        plan, execution, groups = self.make_valid_case()
        groups["general"]["metadata"]["steps"]["measure_candidate"] = "failure"
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["groups"]["general"]["status"], "EXECUTION_ERROR")
        self.assertEqual(report["exit_code"], 2)
        self.assertFalse(report["publish_baseline"])

    def test_missing_required_file_is_execution_error_and_wrong_set_is_data_error(self) -> None:
        plan, execution, groups = self.make_valid_case()
        del groups["sampling_cpp17"]["files"]["rounds/candidate-1.json"]
        missing_file: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(missing_file["groups"]["sampling_cpp17"]["status"], "EXECUTION_ERROR")
        plan, execution, groups = self.make_valid_case()
        groups["sampling_cpp17"]["files"]["rounds/candidate-1.json"] = raw_result({"other-case": 100.0})
        wrong_set: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(wrong_set["groups"]["sampling_cpp17"]["status"], "DATA_ERROR")

    def test_missing_group_is_execution_error_and_retains_other_groups(self) -> None:
        plan, execution, groups = self.make_valid_case()
        del groups["general"]
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["groups"]["general"]["status"], "EXECUTION_ERROR")
        self.assertEqual(report["groups"]["sampling_cpp17"]["status"], "PASS")
        self.assertFalse(report["publish_baseline"])

    def test_both_sides_missing_expected_case_is_data_error(self) -> None:
        plan, execution, groups = self.make_valid_case()
        for variant in ("candidate", "baseline"):
            groups["sampling_cpp17"]["metadata"]["expected"][variant].append("missing-case")
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["groups"]["sampling_cpp17"]["status"], "DATA_ERROR")
        self.assertFalse(report["notify_regression"])
        self.assertFalse(report["publish_baseline"])

    def test_confirmation_metadata_must_match_selected_cases_and_policy(self) -> None:
        plan, execution, groups = self.make_valid_case(140.0, 100.0, run_context())
        metadata: dict[str, Any] = groups["sampling_cpp17"]["files"]["confirmation/metadata.json"]
        metadata["complete"] = False
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["groups"]["sampling_cpp17"]["status"], "DATA_ERROR")
        self.assertIn("complete", report["groups"]["sampling_cpp17"]["errors"][0]["message"])

    def test_cancelled_run_never_publishes_or_notifies(self) -> None:
        plan, execution, groups = self.make_valid_case(140.0, 100.0)
        plan["context"]["cancelled"] = True
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["status"], "CANCELLED")
        self.assertEqual(report["exit_code"], 3)
        self.assertFalse(report["publish_baseline"])
        self.assertFalse(report["notify_regression"])

    def test_cancelled_platform_job_disables_qualifications(self) -> None:
        plan, execution, groups = self.make_valid_case(140.0, 100.0)
        execution["jobs"]["general"] = "cancelled"
        report: dict[str, Any] = evaluate_run(plan, execution, groups)
        self.assertEqual(report["status"], "CANCELLED")
        self.assertEqual(report["exit_code"], 3)
        self.assertFalse(report["publish_baseline"])
        self.assertFalse(report["notify_regression"])

    def test_invalid_plan_report_is_ready_but_has_no_qualification(self) -> None:
        plan: dict[str, Any] = plan_run(
            policy_value(), run_context(), {"success": False, "artifacts": [], "runs": {}, "error": "offline"},
        )
        report: dict[str, Any] = evaluate_run(plan, execution_record(), {})
        self.assertTrue(report["report_ready"])
        self.assertEqual(report["exit_code"], 2)
        self.assertFalse(report["publish_baseline"])
        self.assertFalse(report["notify_regression"])


class CommandLineTests(unittest.TestCase):
    def test_plan_and_evaluate_file_adapters_write_report_and_literal_outputs(self) -> None:
        context: dict[str, Any] = run_context()
        plan: dict[str, Any] = plan_run(policy_value(), context, successful_query())
        groups: dict[str, Any] = complete_groups(plan)
        execution: dict[str, Any] = execution_record()
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path: Path = Path(temporary_directory)
            policy_file: Path = temporary_path / "policy.json"
            context_file: Path = temporary_path / "context.json"
            query_file: Path = temporary_path / "query.json"
            plan_file: Path = temporary_path / "plan.json"
            execution_file: Path = temporary_path / "execution.json"
            groups_directory: Path = temporary_path / "groups"
            output_file: Path = temporary_path / "report.json"
            summary_file: Path = temporary_path / "summary.md"
            github_output: Path = temporary_path / "github-output.txt"
            policy_file.write_text(json.dumps(policy_value()), encoding="utf-8")
            context_file.write_text(json.dumps(context), encoding="utf-8")
            query_file.write_text(json.dumps(successful_query()), encoding="utf-8")
            plan_process: subprocess.CompletedProcess[str] = subprocess.run(
                [
                    sys.executable, str(GATE_SCRIPT), "plan",
                    "--policy", str(policy_file), "--context", str(context_file),
                    "--query", str(query_file), "--output", str(plan_file),
                    "--github-output", str(github_output),
                ],
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            self.assertEqual(plan_process.returncode, 0, plan_process.stderr)
            loaded_plan: dict[str, Any] = json.loads(plan_file.read_text(encoding="utf-8"))
            self.assertTrue(loaded_plan["valid"])
            execution_file.write_text(json.dumps(execution), encoding="utf-8")
            for group_name, group_value in groups.items():
                group_directory: Path = groups_directory / group_name
                group_directory.mkdir(parents=True)
                (group_directory / "group.json").write_text(json.dumps(group_value["metadata"]), encoding="utf-8")
                for file_name, file_value in group_value["files"].items():
                    result_path: Path = group_directory / file_name
                    result_path.parent.mkdir(parents=True, exist_ok=True)
                    result_path.write_text(json.dumps(file_value), encoding="utf-8")
            evaluate_process: subprocess.CompletedProcess[str] = subprocess.run(
                [
                    sys.executable, str(GATE_SCRIPT), "evaluate",
                    "--plan", str(plan_file), "--execution", str(execution_file),
                    "--groups-dir", str(groups_directory), "--output", str(output_file),
                    "--summary", str(summary_file), "--github-output", str(github_output),
                ],
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            self.assertEqual(evaluate_process.returncode, 0, evaluate_process.stderr)
            report: dict[str, Any] = json.loads(output_file.read_text(encoding="utf-8"))
            self.assertEqual(report["exit_code"], 0)
            self.assertTrue(report["report_ready"])
            self.assertTrue(summary_file.exists())
            outputs: str = github_output.read_text(encoding="utf-8")
            self.assertIn("plan_valid=true", outputs)
            self.assertIn("publish_baseline=false", outputs)
            self.assertIn("notify_regression=false", outputs)
            self.assertIn("report_ready=true", outputs)
            self.assertIn("summary_ready=true", outputs)


if __name__ == "__main__":
    unittest.main()
