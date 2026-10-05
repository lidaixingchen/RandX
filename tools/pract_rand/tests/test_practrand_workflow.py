"""PractRand workflow 计划、矩阵、汇总和产物接线检查."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT: Path = Path(__file__).resolve().parents[3]
WORKFLOW_PATH: Path = REPO_ROOT / ".github" / "workflows" / "practrand-nightly.yml"
RUNNER_PATH: Path = REPO_ROOT / "tools" / "pract_rand" / "run_practrand.py"
SUMMARIZER_PATH: Path = REPO_ROOT / "tools" / "pract_rand" / "summarize_practrand.py"


class TestPractRandWorkflow(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow: str = WORKFLOW_PATH.read_text(encoding="utf-8")
        cls.runner: str = RUNNER_PATH.read_text(encoding="utf-8")
        cls.summarizer: str = SUMMARIZER_PATH.read_text(encoding="utf-8")

    def test_workflow_uses_plan_matrix_and_always_run_summary(self) -> None:
        self.assertIn("Create frozen PractRand plan", self.workflow)
        self.assertIn("--plan-output practrand-plan.json", self.workflow)
        self.assertIn("matrix: ${{ fromJSON(needs.plan.outputs.matrix) }}", self.workflow)
        self.assertIn("fail-fast: false", self.workflow)
        self.assertIn("summarize:", self.workflow)
        self.assertIn("needs: [plan, practrand]", self.workflow)
        self.assertRegex(self.workflow, r"summarize:[\s\S]*?if: always\(\)")

    def test_each_matrix_job_and_plan_uploads_independent_artifacts_on_failure(self) -> None:
        self.assertEqual(self.workflow.count("if: always()\n        uses: actions/upload-artifact@v4"), 3)
        self.assertIn("practrand-plan-${{ github.run_id }}-${{ github.run_attempt }}", self.workflow)
        self.assertIn("practrand-result-${{ github.run_id }}-${{ github.run_attempt }}-${{ matrix.engine }}", self.workflow)
        self.assertIn("tools/pract_rand/logs/", self.workflow)
        self.assertIn("practrand-summary-${{ github.run_id }}-${{ github.run_attempt }}", self.workflow)
        self.assertIn('reports/${{ matrix.engine }}.json', self.workflow)

    def test_workflow_runs_frozen_item_and_structured_summary(self) -> None:
        self.assertIn("Download matrix frozen plan", self.workflow)
        self.assertIn('Path("artifacts/plan/practrand-plan.json").read_text', self.workflow)
        self.assertNotIn("toJSON(matrix.plan_item)", self.workflow)
        self.assertIn("--plan-item plan-item.json --output-json", self.workflow)
        self.assertIn("--plan artifacts/plan/practrand-plan.json", self.workflow)
        self.assertIn("--matrix-result \"$MATRIX_RESULT\"", self.workflow)
        self.assertIn("--github-summary \"$GITHUB_STEP_SUMMARY\"", self.workflow)
        self.assertIn("statusLabels", self.workflow)
        self.assertIn("github.rest.issues.create", self.workflow)
        self.assertIn("steps.summary.outcome != 'success'", self.workflow)
        self.assertIn("--profile", self.runner)
        self.assertIn("--checkpoint-min", self.runner)
        self.assertIn("--timeout", self.runner)
        self.assertIn("compare_identity", self.summarizer)

    def test_scheduled_and_manual_runs_select_initially_unverified_profiles(self) -> None:
        self.assertIn("if [ \"$EVENT_NAME\" = \"schedule\" ]; then PROFILE=nightly; fi", self.workflow)
        self.assertIn("default: quick", self.workflow)
        policy: dict[str, object] = __import__("json").loads(
            (REPO_ROOT / "tools" / "pract_rand" / "practrand_policy.json").read_text(encoding="utf-8")
        )
        profiles: dict[str, dict[str, object]] = policy["profiles"]  # type: ignore[assignment]
        self.assertEqual(policy["default_profile"], "nightly")
        self.assertEqual(profiles["quick"]["acceptance_status"], "initial_unverified")
        self.assertEqual(profiles["nightly"]["acceptance_status"], "initial_unverified")
        for profile_name, profile in profiles.items():
            target_lengths: dict[str, str] = profile["target_lengths"]  # type: ignore[assignment]
            with self.subTest(profile=profile_name):
                self.assertEqual(set(target_lengths.values()), {"4GB"})


if __name__ == "__main__":
    unittest.main()
