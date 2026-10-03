"""验证 CI 在各编译器矩阵中比较生产与 hook 默认引擎注册。"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


WORKFLOW_PATH: Path = Path(__file__).resolve().parents[1] / ".github" / "workflows" / "ci.yml"
JOB_HEADER_PATTERN: re.Pattern[str] = re.compile(r"(?m)^  ([a-z][a-z0-9-]*):\s*$")
STEP_HEADER_PATTERN: re.Pattern[str] = re.compile(r"(?m)^      - (?:name|uses):")
MATRIX_STANDARDS: str = "std: [c++17, c++20, c++23]"
PRODUCTION_STEP_NAME: str = "构建并运行生产默认引擎套件"
HOOK_STEP_NAME: str = "Build and run entropy failure tests"
COMPARE_STEP_NAME: str = "核对生产默认引擎与熵源 hook 注册一致"
DEFAULT_ENGINE_SUITE: str = "公共/基础/默认引擎"


def _job_section(workflow: str, job_name: str) -> str:
    job_headers: list[re.Match[str]] = list(JOB_HEADER_PATTERN.finditer(workflow))
    matching_headers: list[re.Match[str]] = [
        header for header in job_headers if header.group(1) == job_name
    ]
    if len(matching_headers) != 1:
        raise AssertionError(f"CI 工作流中应恰有一个 {job_name!r} job。")

    start: int = matching_headers[0].start()
    following_headers: list[re.Match[str]] = [
        header for header in job_headers if header.start() > start
    ]
    end: int = following_headers[0].start() if following_headers else len(workflow)
    return workflow[start:end]


def _step_section(job: str, step_name: str) -> str:
    step_headers: list[re.Match[str]] = list(STEP_HEADER_PATTERN.finditer(job))
    matching_headers: list[re.Match[str]] = [
        header
        for header in step_headers
        if job.startswith(f"      - name: {step_name}\n", header.start())
    ]
    if len(matching_headers) != 1:
        raise AssertionError(f"job 中应恰有一个 {step_name!r} 步骤。")

    start: int = matching_headers[0].start()
    following_headers: list[re.Match[str]] = [
        header for header in step_headers if header.start() > start
    ]
    end: int = following_headers[0].start() if following_headers else len(job)
    return job[start:end]


class DefaultEngineCiWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow: str = WORKFLOW_PATH.read_text(encoding="utf-8")

    def test_each_compiler_matrix_compares_built_binaries_per_build_mode(self) -> None:
        compiler_invocations: dict[str, str] = {
            "gcc": "g++-14 -std='${{ matrix.std }}'",
            "clang": "clang++-18 -std='${{ matrix.std }}'",
            "msvc": "& cl $standardFlag",
        }
        runner_labels: dict[str, str] = {
            "gcc": "runs-on: ubuntu-24.04",
            "clang": "runs-on: ubuntu-24.04",
            "msvc": "runs-on: windows-2025",
        }

        for job_name, compiler_invocation in compiler_invocations.items():
            with self.subTest(job=job_name):
                job: str = _job_section(self.workflow, job_name)
                production_step: str = _step_section(job, PRODUCTION_STEP_NAME)
                hook_step: str = _step_section(job, HOOK_STEP_NAME)
                compare_step: str = _step_section(job, COMPARE_STEP_NAME)

                self.assertIn(MATRIX_STANDARDS, job)
                self.assertIn(runner_labels[job_name], job)
                self.assertLess(job.index(PRODUCTION_STEP_NAME), job.index(HOOK_STEP_NAME))
                self.assertLess(job.index(HOOK_STEP_NAME), job.index(COMPARE_STEP_NAME))
                self.assertIn(compiler_invocation, production_step)
                self.assertIn(compiler_invocation, hook_step)
                self.assertIn("if: matrix.std != 'c++20'", production_step)
                self.assertIn("if: matrix.std != 'c++20'", compare_step)
                self.assertEqual(compare_step.count("compare-binaries"), 1)
                self.assertIn(f"--suite '{DEFAULT_ENGINE_SUITE}'", compare_step)

                if job_name == "msvc":
                    self.assertIn("foreach ($buildMode in @('debug', 'release'))", compare_step)
                    self.assertIn("foreach ($buildMode in @('debug', 'release'))", production_step)
                    self.assertIn("foreach ($buildMode in @('debug', 'release'))", hook_step)
                    self.assertIn(
                        '$productionBinary = "default-engine-build/test_default_engine_${buildMode}.exe"',
                        compare_step,
                    )
                    self.assertIn(
                        '$hookBinary = "entropy-build/test_entropy_${buildMode}.exe"',
                        compare_step,
                    )
                    self.assertIn("--cpp17-binary $productionBinary", compare_step)
                    self.assertIn("--cpp23-binary $hookBinary", compare_step)
                    self.assertIn("if ($LASTEXITCODE -gt $comparisonStatus)", compare_step)
                    self.assertIn("exit $comparisonStatus", compare_step)
                    loop_start: int = compare_step.index(
                        "foreach ($buildMode in @('debug', 'release'))"
                    )
                    compare_command: int = compare_step.index("compare-binaries")
                    loop_end: int = compare_step.index("\n          }\n          exit", loop_start)
                    self.assertLess(loop_start, compare_command)
                    self.assertLess(compare_command, loop_end)
                    self.assertIn(
                        '$binary = "default-engine-build/test_default_engine_${buildMode}.exe"',
                        production_step,
                    )
                    self.assertIn(
                        '$binary = "entropy-build/test_entropy_${buildMode}.exe"', hook_step
                    )
                else:
                    self.assertIn("for build_mode in debug release; do", compare_step)
                    self.assertIn("for build_mode in debug release; do", production_step)
                    self.assertIn("for build_mode in debug release; do", hook_step)
                    self.assertIn(
                        '--cpp17-binary "default-engine-build/test_default_engine_${build_mode}"',
                        compare_step,
                    )
                    self.assertIn(
                        '--cpp23-binary "entropy-build/test_entropy_${build_mode}"',
                        compare_step,
                    )
                    loop_start = compare_step.index("for build_mode in debug release; do")
                    compare_command = compare_step.index("compare-binaries")
                    loop_end = compare_step.index("\n          done", loop_start)
                    self.assertLess(loop_start, compare_command)
                    self.assertLess(compare_command, loop_end)
                    self.assertIn('result=$?', compare_step)
                    self.assertIn('exit "$comparison_status"', compare_step)
                    self.assertIn(
                        'binary="default-engine-build/test_default_engine_${build_mode}"',
                        production_step,
                    )
                    self.assertIn(
                        '-o "entropy-build/test_entropy_${build_mode}"', hook_step
                    )


if __name__ == "__main__":
    unittest.main()
