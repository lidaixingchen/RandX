"""PractRand profile、覆盖项与冻结计划契约测试."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

PRACTRAND_DIR: Path = Path(__file__).resolve().parents[1]
if str(PRACTRAND_DIR) not in sys.path:
    sys.path.insert(0, str(PRACTRAND_DIR))

from run_practrand import (
    build_practrand_plan,
    load_practrand_policy,
    parse_length_to_bytes,
    read_plan_item,
    validate_plan_item,
    write_github_plan_output,
)


class TestPractRandPolicy(unittest.TestCase):
    def setUp(self) -> None:
        self.policy: dict[str, object] = load_practrand_policy()

    def build_plan(self, **overrides: object) -> dict[str, object]:
        options: dict[str, object] = {
            "policy": self.policy,
            "profile_name": "quick",
            "randx_commit": "randx-commit",
            "practrand_commit": "practrand-commit",
        }
        options.update(overrides)
        return build_practrand_plan(**options)  # type: ignore[arg-type]

    def test_verified_profiles_plan_all_eight_engines_at_four_gibibytes(self) -> None:
        for profile_name in ("quick", "nightly"):
            with self.subTest(profile=profile_name):
                plan: dict[str, object] = self.build_plan(profile_name=profile_name)
                items: list[dict[str, object]] = plan["items"]  # type: ignore[assignment]
                self.assertEqual(len(items), 8)
                self.assertEqual({int(item["target_bytes"]) for item in items}, {parse_length_to_bytes("4GB")})
                self.assertEqual(plan["profile_acceptance_status"], "runner_verified")
                self.assertEqual(items[0]["test_parameters"], ["stdin64", "-tlmin", "1M", "-tlmax", "4G", "-te", "1"])
                self.assertEqual(items[2]["test_parameters"][0], "stdin32")

    def test_profile_lengths_and_explicit_cli_values_resolve_before_plan(self) -> None:
        nightly: dict[str, object] = self.build_plan(profile_name="nightly", engine_name="chacha20")
        item: dict[str, object] = nightly["items"][0]  # type: ignore[index]
        self.assertEqual(item["target_bytes"], parse_length_to_bytes("4GB"))
        self.assertEqual(item["seed_strategy"], "os_entropy")
        self.assertIsNone(item["seed_value"])

        overridden: dict[str, object] = self.build_plan(
            engine_name="sfc64",
            length_override="4096MB",
            checkpoint_min_override="512KB",
            timeout_override=1200,
        )
        overridden_item: dict[str, object] = overridden["items"][0]  # type: ignore[index]
        self.assertEqual(overridden_item["target_bytes"], parse_length_to_bytes("4GB"))
        self.assertEqual(overridden_item["checkpoint_min_bytes"], parse_length_to_bytes("512KB"))
        self.assertEqual(overridden_item["timeout_seconds"], 1200)
        self.assertEqual(overridden_item["seed_value"], self.policy["fixed_seed"])

    def test_invalid_target_checkpoint_and_budget_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            self.build_plan(engine_name="sfc64", length_override="1MB", checkpoint_min_override="2MB")
        with self.assertRaises(ValueError):
            self.build_plan(timeout_override=18001)
        with self.assertRaises(ValueError):
            self.build_plan(profile_name="unlisted")
        with self.assertRaises(ValueError):
            self.build_plan(engine_name="unknown")

    def test_plan_item_rejects_parameters_that_do_not_match_frozen_identity(self) -> None:
        plan: dict[str, object] = self.build_plan(engine_name="xoshiro128")
        item: dict[str, object] = dict(plan["items"][0])  # type: ignore[index]
        item["test_parameters"] = ["stdin64", "-tlmin", "1M", "-tlmax", "4G", "-te", "1"]
        with self.assertRaises(ValueError):
            validate_plan_item(item)

    def test_plan_item_file_and_github_matrix_keep_frozen_values(self) -> None:
        plan: dict[str, object] = self.build_plan(engine_name="sfc64")
        item: dict[str, object] = plan["items"][0]  # type: ignore[index]
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path: Path = Path(temporary_directory)
            plan_item_path: Path = temporary_path / "item.json"
            output_path: Path = temporary_path / "github-output.txt"
            plan_item_path.write_text(json.dumps(item), encoding="utf-8")
            loaded: dict[str, object] = read_plan_item(plan_item_path)
            write_github_plan_output(output_path, plan)
            output: str = output_path.read_text(encoding="utf-8")

        self.assertEqual(loaded, item)
        self.assertEqual(loaded["seed_value"], self.policy["fixed_seed"])
        matrix: dict[str, object] = json.loads(output.splitlines()[0].partition("=")[2])
        self.assertEqual(matrix, {"include": [{"engine": "sfc64"}]})
        self.assertIn("acceptance_status=runner_verified", output)
        self.assertIn("job_timeout_minutes=360", output)

    def test_policy_rejects_duplicate_engines_and_bad_lengths(self) -> None:
        policy_path: Path = PRACTRAND_DIR / "practrand_policy.json"
        source: dict[str, object] = json.loads(policy_path.read_text(encoding="utf-8"))
        source["engines"].append(dict(source["engines"][0]))  # type: ignore[index,union-attr]
        with tempfile.TemporaryDirectory() as temporary_directory:
            duplicate_path: Path = Path(temporary_directory) / "duplicate.json"
            duplicate_path.write_text(json.dumps(source), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_practrand_policy(duplicate_path)

        with self.assertRaises(ValueError):
            self.build_plan(engine_name="sfc64", length_override="4")


if __name__ == "__main__":
    unittest.main()
