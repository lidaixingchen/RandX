#!/usr/bin/env python3
"""核对 PractRand 实际报告与冻结计划并生成 workflow 汇总."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from run_practrand import atomic_write_json

IDENTITY_FIELDS: tuple[str, ...] = (
    "engine",
    "randx_commit",
    "practrand_commit",
    "profile",
    "input_width_bits",
    "seed_strategy",
    "seed_value",
    "checkpoint_min_bytes",
    "target_bytes",
    "timeout_seconds",
    "test_parameters",
)

OVERALL_EXIT_CODES: dict[str, int] = {
    "pass": 0,
    "statistical_failure": 1,
    "environment_error": 2,
    "inconclusive": 3,
}


def read_plan(plan_path: Path | str) -> dict[str, Any]:
    """读取预期计划对象."""
    with Path(plan_path).open("r", encoding="utf-8") as plan_file:
        plan: Any = json.load(plan_file)
    if not isinstance(plan, dict):
        raise ValueError("冻结计划顶层必须为对象")
    items: Any = plan.get("items")
    if not isinstance(items, list):
        raise ValueError("冻结计划必须包含 items 数组")
    if any(not isinstance(item, dict) for item in items):
        raise ValueError("冻结计划中的每个项目必须为对象")
    return plan


def load_report_files(results_dir: Path | str) -> tuple[list[dict[str, Any]], list[str]]:
    """递归读取上传的项目数组，保留损坏文件的位置."""
    root: Path = Path(results_dir)
    reports: list[dict[str, Any]] = []
    errors: list[str] = []
    if not root.exists():
        return reports, [f"报告目录不存在: {root}"]
    for report_path in sorted(root.rglob("*.json")):
        try:
            with report_path.open("r", encoding="utf-8") as report_file:
                report: Any = json.load(report_file)
        except (OSError, json.JSONDecodeError) as error:
            errors.append(f"报告 JSON 无法读取 {report_path}: {error}")
            continue
        if isinstance(report, list):
            for project in report:
                if isinstance(project, dict):
                    project_copy: dict[str, Any] = dict(project)
                    project_copy.setdefault("report_file", str(report_path))
                    reports.append(project_copy)
                else:
                    errors.append(f"报告项目不是对象: {report_path}")
        else:
            errors.append(f"报告顶层必须为项目数组: {report_path}")
    return reports, errors


def compare_identity(expected: dict[str, Any], actual: dict[str, Any]) -> list[str]:
    """逐字段比较计划身份，长度已按字节数冻结."""
    mismatches: list[str] = []
    for field in IDENTITY_FIELDS:
        if field not in expected:
            mismatches.append(f"计划缺少必需字段 {field}")
        elif field not in actual:
            mismatches.append(f"实际报告缺少必需字段 {field}")
        elif actual[field] != expected[field]:
            mismatches.append(f"{field} 不匹配: 计划={expected[field]!r} 报告={actual[field]!r}")
    return mismatches


def classify_project(expected: dict[str, Any], actual: dict[str, Any] | None, matrix_result: str) -> dict[str, Any]:
    """分类单个预期项目，同时检查身份和完整目标证据."""
    engine: str = str(expected.get("engine", "unknown"))
    if actual is None:
        missing_status: str = "inconclusive" if matrix_result in ("cancelled", "skipped") else "environment_error"
        return {
            "engine": engine,
            "status": missing_status,
            "reason": "矩阵任务被取消，未取得项目报告" if missing_status == "inconclusive" else "预期项目缺少结构化报告",
            "missing_report": True,
            "evidence": [],
        }

    mismatches: list[str] = compare_identity(expected, actual)
    if mismatches:
        return {
            "engine": engine,
            "status": "environment_error",
            "reason": "；".join(mismatches),
            "identity_mismatches": mismatches,
            "missing_report": False,
            "evidence": [actual.get("report_file", "")],
        }

    actual_status: str = str(actual.get("status", ""))
    phase: str = str(actual.get("phase", ""))
    execution_status: str = str(actual.get("execution_status", ""))
    statistical_status: str = str(actual.get("statistical_status", ""))
    target_bytes: int = int(expected["target_bytes"])
    tested_bytes: int = int(actual.get("reported_tested_bytes", 0))
    test_count: int = int(actual.get("test_count", 0))

    if actual_status == "statistical_failure" or statistical_status == "failure":
        result_status: str = "statistical_failure"
    elif execution_status in ("failed", "unknown") or actual_status == "environment_error":
        result_status = "environment_error"
    elif phase != "final" or execution_status in ("timeout", "cancelled"):
        result_status = "inconclusive"
    elif execution_status != "ok":
        result_status = "environment_error"
    elif actual_status != "pass" or statistical_status != "pass":
        result_status = "inconclusive"
    elif tested_bytes < target_bytes or test_count <= 0:
        result_status = "inconclusive"
    else:
        result_status = "pass"

    reason: str = str(actual.get("reason", ""))
    if result_status == "inconclusive" and not reason:
        reason = f"完整测试证据不足: {tested_bytes}/{target_bytes} 字节，{test_count} 项检验"
    return {
        "engine": engine,
        "status": result_status,
        "reason": reason,
        "reported_tested_bytes": tested_bytes,
        "target_bytes": target_bytes,
        "test_count": test_count,
        "run_suspicious_count": int(actual.get("run_suspicious_count", 0)),
        "suspicious_markers": actual.get("suspicious_markers", []),
        "log_file": actual.get("log_file", ""),
        "report_file": actual.get("report_file", ""),
        "missing_report": False,
        "evidence": [actual.get("log_file", ""), actual.get("report_file", "")],
    }


def aggregate_status(projects: list[dict[str, Any]], global_errors: list[str]) -> str:
    """按既有统计失败、环境异常、证据不足优先级归并结果."""
    statuses: list[str] = [str(project["status"]) for project in projects]
    if "statistical_failure" in statuses:
        return "statistical_failure"
    if global_errors or not statuses:
        return "environment_error"
    if "environment_error" in statuses:
        return "environment_error"
    if "inconclusive" in statuses:
        return "inconclusive"
    return "pass" if all(status == "pass" for status in statuses) else "environment_error"


def render_summary(summary: dict[str, Any]) -> str:
    """生成人类可读的 GitHub Actions Summary."""
    lines: list[str] = [
        "# PractRand 计划汇总",
        "",
        f"- 总体状态：`{summary['status']}`",
        f"- Profile：`{summary.get('profile', 'unknown')}` ({summary.get('profile_acceptance_status', 'unknown')})",
        f"- 预期项目：{summary['expected_count']}，实际报告：{summary['actual_count']}",
        "",
        "| 引擎 | 状态 | 已测字节 / 目标字节 | 检验数 | 可疑标记 | 说明 |",
        "| --- | --- | ---: | ---: | ---: | --- |",
    ]
    for project in summary["projects"]:
        tested: str = str(project.get("reported_tested_bytes", "—"))
        target: str = str(project.get("target_bytes", "—"))
        count: str = str(project.get("test_count", "—"))
        suspicious: str = str(project.get("run_suspicious_count", "—"))
        reason: str = str(project.get("reason", "")).replace("|", "\\|").replace("\n", " ")
        lines.append(
            f"| {project['engine']} | {project['status']} | {tested} / {target} | {count} | {suspicious} | {reason} |"
        )
    if summary["errors"]:
        lines.extend(("", "## 汇总错误", ""))
        lines.extend(f"- {error}" for error in summary["errors"])
    markers: list[dict[str, Any]] = [
        {**marker, "engine": project["engine"]}
        for project in summary["projects"]
        for marker in project.get("suspicious_markers", [])
        if isinstance(marker, dict)
    ]
    if markers:
        lines.extend(("", "## 可疑标记证据", ""))
        lines.extend(
            f"- {marker['engine']} / {marker.get('test_name', 'unknown')}：检查点 {marker.get('checkpoint_bytes', 0)} 字节，"
            f"{marker.get('log_file', '无日志位置')}:{marker.get('line_number', '?')}"
            for marker in markers
        )
    return "\n".join(lines) + "\n"


def summarize(
    plan_path: Path | str,
    results_dir: Path | str,
    plan_result: str = "success",
    matrix_result: str = "success",
) -> dict[str, Any]:
    """生成冻结计划与上传报告的完整对照结果."""
    errors: list[str] = []
    try:
        plan: dict[str, Any] = read_plan(plan_path)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        plan = {"items": []}
        errors.append(f"计划读取失败: {error}")

    expected: list[dict[str, Any]] = list(plan.get("items", []))
    if plan_result != "success":
        errors.append(f"计划任务状态为 {plan_result}")
    if not expected:
        errors.append("预期项目清单为空")

    actual_reports, report_errors = load_report_files(results_dir)
    errors.extend(report_errors)
    reports_by_engine: dict[str, list[dict[str, Any]]] = {}
    for actual in actual_reports:
        engine: str = str(actual.get("engine", ""))
        reports_by_engine.setdefault(engine, []).append(actual)

    projects: list[dict[str, Any]] = []
    expected_engines: set[str] = set()
    for item in expected:
        engine = str(item.get("engine", "unknown"))
        if engine in expected_engines:
            errors.append(f"计划引擎重复: {engine}")
            projects.append(classify_project(item, None, matrix_result))
            continue
        expected_engines.add(engine)
        actual_items: list[dict[str, Any]] = reports_by_engine.get(engine, [])
        if len(actual_items) > 1:
            errors.append(f"引擎 {engine} 有多个实际报告")
            projects.append(
                {
                    "engine": engine,
                    "status": "environment_error",
                    "reason": "同一预期项目存在多个实际报告",
                    "missing_report": False,
                    "evidence": [str(report.get("report_file", "")) for report in actual_items],
                }
            )
        else:
            projects.append(classify_project(item, actual_items[0] if actual_items else None, matrix_result))

    unexpected_engines: list[str] = sorted(set(reports_by_engine) - expected_engines)
    if unexpected_engines:
        errors.append(f"报告包含计划外引擎: {', '.join(unexpected_engines)}")
    if matrix_result == "failure" and all(project["status"] == "pass" for project in projects):
        errors.append("矩阵任务报告失败，但所有项目报告均为通过")

    summary: dict[str, Any] = {
        "schema_version": "1.0",
        "status": "environment_error",
        "profile": plan.get("profile", "unknown"),
        "profile_acceptance_status": plan.get("profile_acceptance_status", "unknown"),
        "plan_result": plan_result,
        "matrix_result": matrix_result,
        "expected_count": len(expected),
        "actual_count": len(actual_reports),
        "projects": projects,
        "errors": errors,
    }
    summary["status"] = aggregate_status(projects, errors)
    return summary


def parse_arguments(arguments: list[str] | None = None) -> argparse.Namespace:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, help="计划 job 生成的冻结清单")
    parser.add_argument("--results-dir", required=True, help="各引擎上传报告的目录")
    parser.add_argument("--plan-result", default="success", help="计划 job 的 Actions 终态")
    parser.add_argument("--matrix-result", default="success", help="矩阵 job 的 Actions 终态")
    parser.add_argument("--output-json", required=True, help="汇总 JSON 输出路径")
    parser.add_argument("--github-summary", help="GITHUB_STEP_SUMMARY 文件路径")
    parser.add_argument("--github-output", help="GITHUB_OUTPUT 文件路径")
    return parser.parse_args(arguments)


def main(arguments: list[str] | None = None) -> int:
    args: argparse.Namespace = parse_arguments(arguments)
    try:
        summary: dict[str, Any] = summarize(
            args.plan,
            args.results_dir,
            plan_result=args.plan_result,
            matrix_result=args.matrix_result,
        )
        atomic_write_json(args.output_json, summary)
        rendered: str = render_summary(summary)
        if args.github_summary:
            with Path(args.github_summary).open("a", encoding="utf-8", newline="\n") as summary_file:
                summary_file.write(rendered)
        if args.github_output:
            with Path(args.github_output).open("a", encoding="utf-8", newline="\n") as output_file:
                output_file.write(f"status={summary['status']}\n")
        print(rendered, end="")
        return OVERALL_EXIT_CODES[summary["status"]]
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(f"汇总写入失败: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
