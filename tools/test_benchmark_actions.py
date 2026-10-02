"""验证性能工作流的平台输入、状态收集和文件接线。"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

import benchmark_actions
import run_benchmark_group


def workflow_run(run_id: int) -> dict[str, Any]:
    return {"id": run_id, "status": "completed", "conclusion": "success"}


class ContextTests(unittest.TestCase):
    def test_context_uses_actual_checkout_and_normalizes_platform_values(self) -> None:
        environment: dict[str, str] = {
            "BENCHMARK_REPOSITORY": "owner/project",
            "BENCHMARK_REPOSITORY_ID": "123456",
            "BENCHMARK_EVENT": "pull_request",
            "BENCHMARK_REF": "refs/pull/15/merge",
            "BENCHMARK_RUN_ID": "456789",
            "BENCHMARK_RUN_ATTEMPT": "2",
            "BENCHMARK_HEAD_COMMIT": "head-commit",
            "BENCHMARK_FORK_PR": "true",
            "BENCHMARK_FORCE_UPDATE_BASELINE": "false",
            "BENCHMARK_CANCELLED": "false",
        }
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_path: Path = Path(temporary_directory) / "context.json"
            with patch.dict(os.environ, environment, clear=True), patch.object(
                benchmark_actions, "current_commit", return_value="merge-commit"
            ):
                context: dict[str, Any] = benchmark_actions.create_context(output_path)

            self.assertEqual(context["candidate_commit"], "merge-commit")
            self.assertEqual(context["head_commit"], "head-commit")
            self.assertEqual(context["repository_id"], 123456)
            self.assertEqual(context["run_attempt"], 2)
            self.assertIs(context["fork_pr"], True)
            self.assertIs(context["force_update_baseline"], False)
            self.assertEqual(json.loads(output_path.read_text(encoding="utf-8")), context)

    def test_query_collects_every_page_and_fetches_each_run_once(self) -> None:
        artifact_one: dict[str, Any] = {
            "id": 11,
            "name": "benchmark-baseline",
            "workflow_run": {"id": 88},
        }
        artifact_two: dict[str, Any] = {
            "id": 12,
            "name": "benchmark-baseline",
            "workflow_run": {"id": 89},
        }
        duplicated_run: dict[str, Any] = {
            "id": 13,
            "name": "benchmark-baseline",
            "workflow_run": {"id": 88},
        }
        pages: list[dict[str, Any]] = [
            {"total_count": 3, "artifacts": [artifact_one]},
            {"artifacts": [artifact_two, duplicated_run]},
        ]
        run_values: dict[str, dict[str, Any]] = {"88": workflow_run(88), "89": workflow_run(89)}
        calls: list[list[str]] = []

        def fake_gh_api(arguments: list[str]) -> str:
            calls.append(arguments)
            if "--paginate" in arguments:
                return json.dumps(pages)
            run_id: str = arguments[-1].rsplit("/", maxsplit=1)[-1]
            return json.dumps(run_values[run_id])

        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            policy_path: Path = root / "policy.json"
            context_path: Path = root / "context.json"
            output_path: Path = root / "query.json"
            policy_path.write_text(json.dumps({"baseline": {"name": "benchmark-baseline"}}), encoding="utf-8")
            context_path.write_text(json.dumps({"repository": "owner/project"}), encoding="utf-8")
            with patch.object(benchmark_actions, "gh_api", side_effect=fake_gh_api):
                result: dict[str, Any] = benchmark_actions.query_baselines(policy_path, context_path, output_path)

            self.assertIs(result["success"], True)
            self.assertEqual(len(result["artifacts"]), 3)
            self.assertEqual(set(result["runs"]), {"88", "89"})
            self.assertEqual(len(calls), 3)
            self.assertTrue(calls[0][0:2] == ["--paginate", "--slurp"])
            self.assertEqual(json.loads(output_path.read_text(encoding="utf-8")), result)

    def test_query_failure_keeps_partial_artifacts_and_failure_reason(self) -> None:
        artifact: dict[str, Any] = {"id": 19, "name": "benchmark-baseline", "workflow_run": {"id": 91}}

        def fake_gh_api(arguments: list[str]) -> str:
            if "--paginate" in arguments:
                return json.dumps([{"artifacts": [artifact]}])
            raise RuntimeError("API unavailable")

        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            policy_path: Path = root / "policy.json"
            context_path: Path = root / "context.json"
            output_path: Path = root / "query.json"
            policy_path.write_text(json.dumps({"baseline": {"name": "benchmark-baseline"}}), encoding="utf-8")
            context_path.write_text(json.dumps({"repository": "owner/project"}), encoding="utf-8")
            with patch.object(benchmark_actions, "gh_api", side_effect=fake_gh_api):
                result: dict[str, Any] = benchmark_actions.query_baselines(policy_path, context_path, output_path)

            self.assertIs(result["success"], False)
            self.assertEqual(result["artifacts"], [artifact])
            self.assertEqual(result["runs"], {})
            self.assertIn("run 91 元数据查询失败", result["error"])

    def test_plan_outputs_add_only_the_candidate_checkout_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            plan_path: Path = root / "plan.json"
            output_path: Path = root / "github-output.txt"
            plan_path.write_text(
                json.dumps({"valid": True, "mode": "COMPARE", "context": {"candidate_commit": "candidate-sha"}}),
                encoding="utf-8",
            )
            values: dict[str, str] = benchmark_actions.write_plan_outputs(plan_path, output_path)
            self.assertEqual(values, {"candidate_commit": "candidate-sha"})
            self.assertEqual(output_path.read_text(encoding="utf-8"), "candidate_commit=candidate-sha\n")


class ArtifactStateTests(unittest.TestCase):
    def test_existing_baseline_refresh_records_candidate_only_across_adapters(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            plan_path: Path = root / "plan.json"
            plan_path.write_text(json.dumps({
                "mode": "REFRESH",
                "required_steps": {"general": []},
                "context": {"candidate_commit": "candidate", "run_id": 17, "run_attempt": 1},
                "baseline": {"commit": "historical-baseline"},
            }), encoding="utf-8")
            hardware: subprocess.CompletedProcess[str] = subprocess.CompletedProcess(
                ["lscpu", "--json"], 0, stdout='{"lscpu": []}', stderr="",
            )
            with (
                patch.object(run_benchmark_group, "workspace_root", return_value=root),
                patch.object(run_benchmark_group, "actual_commit", return_value="candidate"),
                patch.object(run_benchmark_group, "first_line", return_value="version"),
                patch.object(run_benchmark_group, "run_logged", return_value=hardware),
            ):
                run_benchmark_group.initialize(plan_path, "general")
            result: dict[str, Any] = benchmark_actions.collect_group(
                plan_path, "general", root / "groups", "{}",
            )
            self.assertIsNone(result["baseline_commit"])
            self.assertIsNone(result["environment"]["baseline_commit"])
            self.assertEqual(result["candidate_commit"], "candidate")

    def test_general_metadata_preserves_actual_baseline_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            plan_path: Path = root / "plan.json"
            plan_path.write_text(json.dumps({
                "required_steps": {"general": []},
                "context": {"candidate_commit": "candidate", "run_id": 17, "run_attempt": 1},
                "baseline": {"commit": "planned-baseline"},
            }), encoding="utf-8")
            directory: Path = root / "groups" / "general"
            directory.mkdir(parents=True)
            (directory / "environment.json").write_text(json.dumps({
                "candidate_commit": "candidate", "baseline_commit": "actual-baseline",
            }), encoding="utf-8")
            result: dict[str, Any] = benchmark_actions.collect_group(
                plan_path, "general", root / "groups", "{}",
            )
            self.assertEqual(result["baseline_commit"], "actual-baseline")

    def test_group_metadata_uses_plan_identity_and_real_step_outcomes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            plan_path: Path = root / "plan.json"
            groups_directory: Path = root / "groups"
            plan: dict[str, Any] = {
                "required_steps": {
                    "sampling_cpp17": [
                        "checkout_candidate", "build_candidate", "enumerate_candidate", "measure_candidate",
                        "checkout_baseline", "build_baseline", "enumerate_baseline", "measure_baseline",
                        "aggregate", "confirm",
                    ]
                },
                "context": {
                    "run_id": 17,
                    "run_attempt": 3,
                    "candidate_commit": "candidate-commit",
                },
                "baseline": None,
                "sampling_commit": "sampling-commit",
            }
            plan_path.write_text(json.dumps(plan), encoding="utf-8")
            group_directory: Path = groups_directory / "sampling_cpp17"
            group_directory.mkdir(parents=True)
            (group_directory / "environment.json").write_text(
                json.dumps({"candidate_commit": "candidate-commit", "baseline_commit": "sampling-commit", "cpu": 4}),
                encoding="utf-8",
            )
            (group_directory / "expected.json").write_text(
                json.dumps({"candidate": ["case/size:1"], "baseline": ["case/size:1"]}),
                encoding="utf-8",
            )
            step_values: dict[str, dict[str, str]] = {
                step_id: {"outcome": "success", "conclusion": "success"}
                for step_id in (
                    "checkout_candidate", "build_candidate", "enumerate_candidate",
                    "checkout_baseline", "build_baseline", "enumerate_baseline",
                    "measure_sampling_rounds", "aggregate", "confirm",
                )
            }
            step_json: str = json.dumps(step_values)

            result: dict[str, Any] = benchmark_actions.collect_group(
                plan_path, "sampling_cpp17", groups_directory, step_json
            )
            metadata: dict[str, Any] = json.loads((group_directory / "group.json").read_text(encoding="utf-8"))

            self.assertEqual(result["run_id"], 17)
            self.assertEqual(result["candidate_commit"], "candidate-commit")
            self.assertEqual(result["baseline_commit"], "sampling-commit")
            self.assertEqual(metadata["steps"]["measure_candidate"], "success")
            self.assertEqual(metadata["steps"]["measure_baseline"], "success")
            self.assertEqual(metadata["environment"]["cpu"], 4)
            self.assertTrue((groups_directory / "sampling_cpp17" / "group.json").is_file())

    def test_execution_records_each_job_and_download_without_collapse(self) -> None:
        needs: dict[str, Any] = {
            "benchmark_plan": {"result": "success"},
            "measure_general": {"result": "success"},
            "measure_sampling_cpp17": {"result": "failure"},
            "measure_sampling_cpp23": {"result": "success"},
        }
        steps: dict[str, Any] = {
            "download_general": {"outcome": "success", "conclusion": "success"},
            "download_sampling_cpp17": {"outcome": "failure", "conclusion": "success"},
            "download_sampling_cpp23": {"outcome": "skipped", "conclusion": "skipped"},
        }
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_path: Path = Path(temporary_directory) / "execution.json"
            result: dict[str, Any] = benchmark_actions.capture_execution(
                "success", "success", json.dumps(needs), json.dumps(steps), output_path
            )
            self.assertEqual(result["jobs"]["sampling_cpp17"], "failure")
            self.assertEqual(result["downloads"]["sampling_cpp17"], "failure")
            self.assertEqual(result["downloads"]["sampling_cpp23"], "skipped")
            self.assertEqual(len(result["errors"]), 2)
            self.assertEqual(json.loads(output_path.read_text(encoding="utf-8")), result)

    def test_finish_rejects_missing_or_inconsistent_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            report_path: Path = Path(temporary_directory) / "report.json"
            report_path.write_text(json.dumps({"report_ready": True, "exit_code": 1}), encoding="utf-8")
            self.assertEqual(benchmark_actions.finish(report_path, "true", "1"), 1)
            self.assertEqual(benchmark_actions.finish(report_path, "false", "1"), 2)
            self.assertEqual(benchmark_actions.finish(report_path, "true", ""), 2)
            self.assertEqual(benchmark_actions.finish(report_path.with_name("missing.json"), "true", "1"), 2)


class WorkflowContractTests(unittest.TestCase):
    def test_ci_runs_action_tests_when_benchmark_workflow_changes(self) -> None:
        ci_workflow_path: Path = Path(__file__).resolve().parents[1] / ".github" / "workflows" / "ci.yml"
        ci_workflow: str = ci_workflow_path.read_text(encoding="utf-8")
        self.assertEqual(ci_workflow.count("      - '.github/workflows/benchmark.yml'"), 2)

    def test_final_job_directly_needs_all_three_measure_jobs(self) -> None:
        workflow_path: Path = Path(__file__).resolve().parents[1] / ".github" / "workflows" / "benchmark.yml"
        workflow: str = workflow_path.read_text(encoding="utf-8")
        self.assertRegex(
            workflow,
            r"(?ms)^  benchmark:\n.*?^    needs: \[benchmark_plan, measure_general, measure_sampling_cpp17, measure_sampling_cpp23\]$",
        )
        self.assertRegex(workflow, r"(?ms)^  benchmark:\n.*?^    if: always\(\) && !cancelled\(\)$")
        self.assertIn("--output benchmark-gate.json", workflow)
        self.assertIn("--summary benchmark-summary.md", workflow)
        self.assertRegex(workflow, r"(?ms)^      - name: Evaluate benchmark gate\n        id: evaluate\n        if: always\(\) && !cancelled\(\)")
        self.assertRegex(workflow, r"(?ms)^      - name: Return benchmark gate result\n        if: always\(\) && !cancelled\(\)")

    def test_each_group_downloads_to_canonical_directory_and_uses_unique_artifact(self) -> None:
        workflow_path: Path = Path(__file__).resolve().parents[1] / ".github" / "workflows" / "benchmark.yml"
        workflow: str = workflow_path.read_text(encoding="utf-8")
        for group in ("general", "sampling_cpp17", "sampling_cpp23"):
            self.assertIn(f"path: groups/{group}", workflow)
            self.assertIn(
                f"benchmark-${{{{ github.run_id }}}}-${{{{ github.run_attempt }}}}-{group}",
                workflow,
            )
            self.assertIn(f"path: groups/{group}/", workflow)
        self.assertEqual(workflow.count("name: ${{ needs.benchmark_plan.outputs.baseline_name }}"), 1)
        self.assertEqual(workflow.count("continue-on-error: true"), 4)
        self.assertIn("steps.evaluate.outputs.publish_baseline == 'true'", workflow)
        self.assertIn("BENCHMARK_EXIT_CODE: ${{ steps.evaluate.outputs.exit_code }}", workflow)

    def test_all_benchmark_measurements_use_policy_driven_runner(self) -> None:
        workflow_path: Path = Path(__file__).resolve().parents[1] / ".github" / "workflows" / "benchmark.yml"
        workflow: str = workflow_path.read_text(encoding="utf-8")
        self.assertIn("run_benchmark_group.py measure --plan plan_artifact/benchmark-plan.json --group general --variant candidate", workflow)
        self.assertIn("run_benchmark_group.py measure --plan plan_artifact/benchmark-plan.json --group general --variant baseline", workflow)
        self.assertIn("id: measure_sampling_rounds", workflow)
        self.assertIn("--benchmark_list_tests=true", (Path(__file__).resolve().parent / "run_benchmark_group.py").read_text(encoding="utf-8"))
        self.assertNotIn("pull_request_target", workflow)
        self.assertNotIn("hashFiles(", workflow)


if __name__ == "__main__":
    unittest.main()
