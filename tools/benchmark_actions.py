"""GitHub Actions 平台适配：上下文、基线查询、产物状态和结果归档。"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from shutil import copy2
from typing import Any, Mapping, Sequence
from urllib.parse import quote


ARTIFACT_PAGE_SIZE: int = 100
GROUP_NAMES: tuple[str, ...] = ("general", "sampling_cpp17", "sampling_cpp23")
SAMPLING_GROUPS: frozenset[str] = frozenset(("sampling_cpp17", "sampling_cpp23"))
VALID_REPORT_EXIT_CODES: frozenset[int] = frozenset((0, 1, 2, 3))


def read_json(path: Path) -> dict[str, Any]:
    """读取 JSON 对象。"""
    value: Any = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON 根节点必须为对象：{path}")
    return value


def write_json(path: Path, value: Mapping[str, Any]) -> None:
    """创建父目录并写入 UTF-8 JSON。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def env_value(name: str) -> str:
    """读取必需的 Actions 环境变量。"""
    value: str | None = os.environ.get(name)
    if value is None:
        raise ValueError(f"缺少环境变量：{name}")
    return value


def parse_bool(value: str | bool, name: str) -> bool:
    """严格解析布尔值。"""
    if isinstance(value, bool):
        return value
    normalized: str = value.strip().lower()
    if normalized == "true":
        return True
    if normalized == "false":
        return False
    raise ValueError(f"{name} 必须是 true 或 false")


def github_output(path: Path, values: Mapping[str, str | int | bool]) -> None:
    """写入单行 Actions 输出，拒绝可能破坏输出边界的换行符。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    lines: list[str] = []
    for key, value in values.items():
        text: str = str(value).lower() if isinstance(value, bool) else str(value)
        if "\n" in text or "\r" in text:
            raise ValueError(f"Actions 输出不得包含换行：{key}")
        lines.append(f"{key}={text}")
    with path.open("a", encoding="utf-8", newline="\n") as output_file:
        output_file.write("\n".join(lines) + "\n")


def current_commit() -> str:
    """返回当前工作树实际检出的提交。"""
    result: subprocess.CompletedProcess[str] = subprocess.run(
        ["git", "rev-parse", "HEAD"], check=True, capture_output=True, text=True, encoding="utf-8"
    )
    commit: str = result.stdout.strip()
    if not commit:
        raise ValueError("git rev-parse HEAD 未返回提交")
    return commit


def create_context(output_path: Path) -> dict[str, Any]:
    """从平台提供的环境变量创建规范化运行上下文。"""
    repository_id_text: str = env_value("BENCHMARK_REPOSITORY_ID")
    run_id_text: str = env_value("BENCHMARK_RUN_ID")
    run_attempt_text: str = env_value("BENCHMARK_RUN_ATTEMPT")
    if not repository_id_text.isdecimal() or not run_id_text.isdecimal() or not run_attempt_text.isdecimal():
        raise ValueError("repository_id、run_id 和 run_attempt 必须为十进制整数")

    context: dict[str, Any] = {
        "repository": env_value("BENCHMARK_REPOSITORY"),
        "repository_id": int(repository_id_text),
        "event": env_value("BENCHMARK_EVENT"),
        "ref": env_value("BENCHMARK_REF"),
        "run_id": int(run_id_text),
        "run_attempt": int(run_attempt_text),
        "candidate_commit": current_commit(),
        "head_commit": env_value("BENCHMARK_HEAD_COMMIT"),
        "fork_pr": parse_bool(env_value("BENCHMARK_FORK_PR"), "fork_pr"),
        "force_update_baseline": parse_bool(
            env_value("BENCHMARK_FORCE_UPDATE_BASELINE"), "force_update_baseline"
        ),
        "cancelled": parse_bool(env_value("BENCHMARK_CANCELLED"), "cancelled"),
    }
    write_json(output_path, context)
    return context


def load_policy_baseline(policy_path: Path) -> dict[str, Any]:
    """读取查询所需的基线名称。"""
    policy: dict[str, Any] = read_json(policy_path)
    baseline: Any = policy.get("baseline")
    if not isinstance(baseline, dict):
        raise ValueError("策略缺少 baseline 对象")
    name: Any = baseline.get("name")
    if not isinstance(name, str) or not name:
        raise ValueError("策略 baseline.name 必须为非空字符串")
    return baseline


def gh_api(arguments: Sequence[str]) -> str:
    """执行 gh api 并返回标准输出。"""
    command: list[str] = ["gh", "api", *arguments]
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command, check=False, capture_output=True, text=True, encoding="utf-8"
    )
    if result.returncode != 0:
        detail: str = result.stderr.strip() or result.stdout.strip() or f"退出码 {result.returncode}"
        raise RuntimeError(f"{' '.join(command)} 失败：{detail}")
    return result.stdout


def artifact_pages(value: Any) -> list[dict[str, Any]]:
    """将 --paginate --slurp 输出归一为 API 页列表。"""
    if not isinstance(value, list):
        raise ValueError("gh api --paginate --slurp 输出必须是 JSON 数组")
    pages: list[dict[str, Any]] = []
    for page in value:
        if isinstance(page, dict):
            pages.append(page)
        elif isinstance(page, list):
            if not all(isinstance(item, dict) for item in page):
                raise ValueError("gh api 分页结果包含非对象页面")
            pages.extend(page)
        else:
            raise ValueError("gh api 分页结果包含非对象页面")
    return pages


def query_baselines(policy_path: Path, context_path: Path, output_path: Path) -> dict[str, Any]:
    """完整查询基线 artifact 与每个所属 run 的元数据。"""
    context: dict[str, Any] = read_json(context_path)
    repository: Any = context.get("repository")
    if not isinstance(repository, str) or not repository:
        raise ValueError("运行上下文缺少 repository")

    error_messages: list[str] = []
    artifacts: list[dict[str, Any]] = []
    runs: dict[str, dict[str, Any]] = {}
    baseline: dict[str, Any] = load_policy_baseline(policy_path)
    artifact_name: str = str(baseline["name"])
    endpoint: str = (
        f"repos/{repository}/actions/artifacts?name={quote(artifact_name, safe='')}"
        f"&per_page={ARTIFACT_PAGE_SIZE}"
    )

    try:
        pages_value: Any = json.loads(gh_api(["--paginate", "--slurp", endpoint]))
        pages: list[dict[str, Any]] = artifact_pages(pages_value)
        for page in pages:
            page_artifacts: Any = page.get("artifacts")
            if not isinstance(page_artifacts, list):
                raise ValueError("artifact API 页面缺少 artifacts 数组")
            if not all(isinstance(item, dict) for item in page_artifacts):
                raise ValueError("artifact API 页面包含非对象记录")
            artifacts.extend(page_artifacts)
    except (json.JSONDecodeError, OSError, RuntimeError, ValueError) as error:
        error_messages.append(f"artifact 查询失败：{error}")

    run_ids: list[str] = []
    for artifact in artifacts:
        workflow_run: Any = artifact.get("workflow_run")
        if not isinstance(workflow_run, dict):
            error_messages.append(f"artifact {artifact.get('id', '<unknown>')} 缺少 workflow_run 元数据")
            continue
        run_id: Any = workflow_run.get("id")
        if isinstance(run_id, bool) or not isinstance(run_id, (int, str)) or not str(run_id).isdecimal():
            error_messages.append(f"artifact {artifact.get('id', '<unknown>')} 的 workflow_run.id 无效")
            continue
        normalized_id: str = str(run_id)
        if normalized_id not in run_ids:
            run_ids.append(normalized_id)

    for run_id in run_ids:
        try:
            run_value: Any = json.loads(gh_api([f"repos/{repository}/actions/runs/{run_id}"]))
            if not isinstance(run_value, dict):
                raise ValueError("run API 响应根节点必须为对象")
            runs[run_id] = run_value
        except (json.JSONDecodeError, OSError, RuntimeError, ValueError) as error:
            error_messages.append(f"run {run_id} 元数据查询失败：{error}")

    result: dict[str, Any] = {
        "success": not error_messages,
        "artifacts": artifacts,
        "runs": runs,
        "error": "; ".join(error_messages),
    }
    write_json(output_path, result)
    return result


def write_fallback_inputs(context_path: Path, query_path: Path) -> None:
    """基线计划产物不可用时，保留明确的查询失败事实供门禁求值。"""
    create_context(context_path)
    write_json(query_path, {
        "success": False,
        "artifacts": [],
        "runs": {},
        "error": "benchmark-plan artifact 下载失败，无法读取原始基线查询结果",
    })


def write_plan_outputs(plan_path: Path, github_output_path: Path) -> dict[str, str]:
    """写出下游 runner 所需的简洁计划字段。"""
    plan: dict[str, Any] = read_json(plan_path)
    context: Any = plan.get("context")
    candidate_commit: Any = context.get("candidate_commit") if isinstance(context, dict) else None
    values: dict[str, str] = {
        "candidate_commit": str(candidate_commit or ""),
    }
    github_output(github_output_path, values)
    return values


def read_step_outcomes(value: Any) -> dict[str, dict[str, Any]]:
    """读取 Actions steps JSON，保留 outcome 与 conclusion。"""
    if not isinstance(value, dict):
        raise ValueError("steps JSON 根节点必须为对象")
    outcomes: dict[str, dict[str, Any]] = {}
    for name, step in value.items():
        if isinstance(step, dict):
            outcomes[str(name)] = step
    return outcomes


def step_outcome(steps: Mapping[str, Mapping[str, Any]], step_id: str) -> str:
    """取得真实 outcome；缺少的平台步骤视为 skipped。"""
    step: Mapping[str, Any] | None = steps.get(step_id)
    outcome: Any = step.get("outcome") if step is not None else None
    return outcome if isinstance(outcome, str) and outcome else "skipped"


def group_step_ids(group: str, plan: Mapping[str, Any]) -> list[str]:
    """从计划读取门禁必需步骤。"""
    required_steps: Any = plan.get("required_steps")
    if not isinstance(required_steps, dict):
        raise ValueError("计划缺少 required_steps")
    required: Any = required_steps.get(group)
    if not isinstance(required, list) or any(not isinstance(item, str) for item in required):
        raise ValueError(f"计划缺少 {group} 的 required_steps 列表")
    return required


def collect_group(
    plan_path: Path, group: str, groups_directory: Path, step_json: str,
) -> dict[str, Any]:
    """依据计划与平台 outcome 生成组级规范产物。"""
    if group not in GROUP_NAMES:
        raise ValueError(f"未知比较组：{group}")
    plan: dict[str, Any] = read_json(plan_path)
    steps: dict[str, dict[str, Any]] = read_step_outcomes(json.loads(step_json))
    group_directory: Path = groups_directory / group
    environment_path: Path = group_directory / "environment.json"
    expected_path: Path = group_directory / "expected.json"
    environment: dict[str, Any] = read_json(environment_path) if environment_path.exists() else {}
    expected: dict[str, Any] = read_json(expected_path) if expected_path.exists() else {}
    candidate_names: Any = expected.get("candidate", [])
    baseline_names: Any = expected.get("baseline", [])
    if not isinstance(candidate_names, list) or not all(isinstance(name, str) for name in candidate_names):
        raise ValueError(f"{group} candidate expected 必须为字符串数组")
    if not isinstance(baseline_names, list) or not all(isinstance(name, str) for name in baseline_names):
        raise ValueError(f"{group} baseline expected 必须为字符串数组")

    actual_steps: dict[str, str] = {}
    sampling_measure_outcome: str = step_outcome(steps, "measure_sampling_rounds")
    for step_id in group_step_ids(group, plan):
        if group in SAMPLING_GROUPS and step_id in ("measure_candidate", "measure_baseline"):
            actual_steps[step_id] = sampling_measure_outcome
        else:
            actual_steps[step_id] = step_outcome(steps, step_id)

    context: Any = plan.get("context")
    plan_baseline: Any = plan.get("baseline")
    candidate_commit: Any = environment.get("candidate_commit")
    if not isinstance(candidate_commit, str) or not candidate_commit:
        candidate_commit = context.get("candidate_commit") if isinstance(context, dict) else None
    if group in SAMPLING_GROUPS:
        baseline_commit: Any = environment.get("baseline_commit") or plan.get("sampling_commit")
    else:
        baseline_commit = environment.get("baseline_commit") or (
            plan_baseline.get("commit") if isinstance(plan_baseline, dict) else None
        )

    run_id: Any = context.get("run_id") if isinstance(context, dict) else None
    run_attempt: Any = context.get("run_attempt") if isinstance(context, dict) else None
    result: dict[str, Any] = {
        "format_version": 1,
        "group": group,
        "run_id": run_id,
        "run_attempt": run_attempt,
        "candidate_commit": candidate_commit,
        "baseline_commit": baseline_commit,
        "steps": actual_steps,
        "expected": {"candidate": candidate_names, "baseline": baseline_names},
        "environment": environment,
    }
    write_json(group_directory / "group.json", result)
    return result


def capture_execution(
    plan_job: str,
    plan_download: str,
    needs_json: str,
    steps_json: str,
    output_path: Path,
) -> dict[str, Any]:
    """記録计划、测量 job 和各组 artifact 下载的真实状态。"""
    needs_value: Any = json.loads(needs_json)
    download_steps: dict[str, dict[str, Any]] = read_step_outcomes(json.loads(steps_json))
    if not isinstance(needs_value, dict):
        raise ValueError("needs JSON 根节点必须为对象")

    def job_result(job_name: str) -> str:
        job: Any = needs_value.get(job_name)
        if not isinstance(job, dict):
            return "skipped"
        result: Any = job.get("result")
        return result if isinstance(result, str) else "skipped"

    jobs: dict[str, str] = {
        "general": job_result("measure_general"),
        "sampling_cpp17": job_result("measure_sampling_cpp17"),
        "sampling_cpp23": job_result("measure_sampling_cpp23"),
    }
    downloads: dict[str, str] = {
        group: step_outcome(download_steps, f"download_{group}") for group in GROUP_NAMES
    }
    errors: list[dict[str, str]] = []
    for group, state in downloads.items():
        if state != "success":
            errors.append({"status": state, "message": f"{group} artifact 下载状态为 {state}"})
    if plan_download != "success":
        download_status: str = plan_download or "skipped"
        errors.append({"status": download_status, "message": f"plan artifact 下载状态为 {download_status}"})
    result: dict[str, Any] = {
        "plan_job": plan_job,
        "jobs": jobs,
        "downloads": downloads,
        "errors": errors,
    }
    write_json(output_path, result)
    return result


def prepare_baseline(groups_directory: Path, report_path: Path, summary_path: Path, output: Path) -> None:
    """准备唯一发布的通用基线 artifact 内容。"""
    output.mkdir(parents=True, exist_ok=True)
    copy2(groups_directory / "general" / "candidate.json", output / "result.json")
    copy2(report_path, output / "benchmark-gate.json")
    copy2(summary_path, output / "benchmark-summary.md")


def finish(report_path: Path, report_ready: str, exit_code: str) -> int:
    """验证最终报告及平台输出，并返回门禁退出状态。"""
    try:
        if report_ready != "true":
            raise ValueError("report_ready 缺失或不是 true")
        report: dict[str, Any] = read_json(report_path)
        if report.get("report_ready") is not True:
            raise ValueError("报告缺少 report_ready=true")
        report_exit_code: Any = report.get("exit_code")
        if isinstance(report_exit_code, bool) or not isinstance(report_exit_code, int):
            raise ValueError("报告缺少整数 exit_code")
        if not exit_code.isdecimal():
            raise ValueError("Actions exit_code 输出缺失或不是十进制整数")
        action_exit_code: int = int(exit_code)
        if action_exit_code not in VALID_REPORT_EXIT_CODES or action_exit_code != report_exit_code:
            raise ValueError("Actions exit_code 与门禁报告不一致")
        return action_exit_code
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"门禁报告无效：{error}", file=sys.stderr)
        return 2


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    context_parser: argparse.ArgumentParser = commands.add_parser("context")
    context_parser.add_argument("--output", type=Path, required=True)

    query_parser: argparse.ArgumentParser = commands.add_parser("query")
    query_parser.add_argument("--policy", type=Path, required=True)
    query_parser.add_argument("--context", type=Path, required=True)
    query_parser.add_argument("--output", type=Path, required=True)

    fallback_parser: argparse.ArgumentParser = commands.add_parser("fallback-inputs")
    fallback_parser.add_argument("--context-output", type=Path, required=True)
    fallback_parser.add_argument("--query-output", type=Path, required=True)

    outputs_parser: argparse.ArgumentParser = commands.add_parser("plan-outputs")
    outputs_parser.add_argument("--plan", type=Path, required=True)
    outputs_parser.add_argument("--github-output", type=Path, required=True)

    group_parser: argparse.ArgumentParser = commands.add_parser("collect-group")
    group_parser.add_argument("--plan", type=Path, required=True)
    group_parser.add_argument("--group", choices=GROUP_NAMES, required=True)
    group_parser.add_argument("--groups-directory", type=Path, required=True)
    group_parser.add_argument("--steps-json", required=True)

    execution_parser: argparse.ArgumentParser = commands.add_parser("execution")
    execution_parser.add_argument("--plan-job", required=True)
    execution_parser.add_argument("--plan-download", required=True)
    execution_parser.add_argument("--needs-json", required=True)
    execution_parser.add_argument("--steps-json", required=True)
    execution_parser.add_argument("--output", type=Path, required=True)

    baseline_parser: argparse.ArgumentParser = commands.add_parser("prepare-baseline")
    baseline_parser.add_argument("--groups-directory", type=Path, required=True)
    baseline_parser.add_argument("--report", type=Path, required=True)
    baseline_parser.add_argument("--summary", type=Path, required=True)
    baseline_parser.add_argument("--output-directory", type=Path, required=True)

    finish_parser: argparse.ArgumentParser = commands.add_parser("finish")
    finish_parser.add_argument("--report", type=Path, required=True)
    finish_parser.add_argument("--report-ready", required=True)
    finish_parser.add_argument("--exit-code", required=True)

    arguments: argparse.Namespace = parser.parse_args()
    try:
        if arguments.command == "context":
            create_context(arguments.output)
            return 0
        if arguments.command == "query":
            result: dict[str, Any] = query_baselines(arguments.policy, arguments.context, arguments.output)
            if not result["success"]:
                print(result["error"], file=sys.stderr)
            return 0
        if arguments.command == "fallback-inputs":
            write_fallback_inputs(arguments.context_output, arguments.query_output)
            return 0
        if arguments.command == "plan-outputs":
            write_plan_outputs(arguments.plan, arguments.github_output)
            return 0
        if arguments.command == "collect-group":
            collect_group(arguments.plan, arguments.group, arguments.groups_directory, arguments.steps_json)
            return 0
        if arguments.command == "execution":
            capture_execution(
                arguments.plan_job,
                arguments.plan_download,
                arguments.needs_json,
                arguments.steps_json,
                arguments.output,
            )
            return 0
        if arguments.command == "prepare-baseline":
            prepare_baseline(arguments.groups_directory, arguments.report, arguments.summary, arguments.output_directory)
            return 0
        if arguments.command == "finish":
            return finish(arguments.report, arguments.report_ready, arguments.exit_code)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"Actions 适配失败：{error}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
