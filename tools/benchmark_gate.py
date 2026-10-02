#!/usr/bin/env python3
"""集中规划并求值性能门禁。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import sys
from dataclasses import asdict, dataclass
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


FORMAT_VERSION: int = 1
GROUP_NAMES: tuple[str, ...] = ("general", "sampling_cpp17", "sampling_cpp23")
SUCCESS_OUTCOME: str = "success"
CANCELLED_PLATFORM_STATE: str = "cancelled"
ISSUE_EVENTS: frozenset[str] = frozenset(("push", "pull_request", "workflow_dispatch"))
DEFAULT_SUMMARY_NAME: str = "benchmark-summary.md"


class GateStatus(str, Enum):
    CONTEXT_ERROR = "CONTEXT_ERROR"
    BASELINE_ERROR = "BASELINE_ERROR"
    DATA_ERROR = "DATA_ERROR"
    EXECUTION_ERROR = "EXECUTION_ERROR"
    CANCELLED = "CANCELLED"


class PlanMode(str, Enum):
    COMPARE = "COMPARE"
    REFRESH = "REFRESH"
    ERROR = "ERROR"
    CANCELLED = "CANCELLED"


class GroupStatus(str, Enum):
    PASS = "PASS"
    REGRESSION = "REGRESSION"
    DATA_ERROR = "DATA_ERROR"
    EXECUTION_ERROR = "EXECUTION_ERROR"
    SKIPPED = "SKIPPED"


@dataclass(frozen=True)
class ErrorRecord:
    status: GateStatus
    message: str
    group: str | None = None


@dataclass(frozen=True)
class GroupPolicy:
    name: str
    kind: str
    standard: int
    target: str
    repetitions: int
    min_time: str
    tolerance: float
    confirmation_repetitions: int | None = None
    confirmation_min_time: str | None = None


@dataclass(frozen=True)
class BaselinePolicy:
    name: str
    branch: str
    workflow: str
    initial_sampling_commit: str
    publish_events: tuple[str, ...]


@dataclass(frozen=True)
class GatePolicy:
    format_version: int
    baseline: BaselinePolicy
    groups: Mapping[str, GroupPolicy]


@dataclass(frozen=True)
class BaselineCandidate:
    artifact_id: int
    run_id: str
    commit: str
    created_at: datetime


class GateInputError(ValueError):
    """策略、测量产物或 API 记录违反输入契约。"""


class GateExecutionError(RuntimeError):
    """必需的测量产物不可用。"""


def _is_mapping(value: Any) -> bool:
    return isinstance(value, Mapping)


def _nonempty_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def _integer(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _identifier(value: Any) -> bool:
    return _integer(value) or _nonempty_string(value)


def _parse_policy(value: Any) -> GatePolicy:
    if not _is_mapping(value):
        raise GateInputError("policy 必须是 JSON 对象")
    if not _integer(value.get("format_version")) or value.get("format_version") != FORMAT_VERSION:
        raise GateInputError(f"policy.format_version 必须为 {FORMAT_VERSION}")
    baseline_data: Any = value.get("baseline")
    if not _is_mapping(baseline_data):
        raise GateInputError("policy.baseline 必须是 JSON 对象")
    baseline_strings: tuple[str, ...] = (
        "name", "branch", "workflow", "initial_sampling_commit",
    )
    if any(not _nonempty_string(baseline_data.get(key)) for key in baseline_strings):
        raise GateInputError("policy.baseline 缺少必需字符串配置")
    publish_events_value: Any = baseline_data.get("publish_events")
    if (
        not isinstance(publish_events_value, list)
        or not publish_events_value
        or any(not _nonempty_string(event) for event in publish_events_value)
    ):
        raise GateInputError("policy.baseline.publish_events 必须是非空字符串列表")

    groups_data: Any = value.get("groups")
    if not _is_mapping(groups_data) or set(groups_data) != set(GROUP_NAMES):
        raise GateInputError(f"policy.groups 必须恰好包含：{', '.join(GROUP_NAMES)}")
    groups: dict[str, GroupPolicy] = {}
    for name in GROUP_NAMES:
        group_data: Any = groups_data[name]
        if not _is_mapping(group_data):
            raise GateInputError(f"policy.groups.{name} 必须是 JSON 对象")
        kind: Any = group_data.get("kind")
        if kind != ("general" if name == "general" else "sampling"):
            raise GateInputError(f"policy.groups.{name}.kind 配置无效")
        standard: Any = group_data.get("standard")
        if not _integer(standard) or standard < 0:
            raise GateInputError(f"policy.groups.{name}.standard 配置无效")
        if name == "sampling_cpp17" and standard != 17:
            raise GateInputError("sampling_cpp17.standard 必须为 17")
        if name == "sampling_cpp23" and standard != 23:
            raise GateInputError("sampling_cpp23.standard 必须为 23")
        target: Any = group_data.get("target")
        repetitions: Any = group_data.get("repetitions")
        min_time: Any = group_data.get("min_time")
        tolerance: Any = group_data.get("tolerance")
        if not _nonempty_string(target) or not _integer(repetitions) or repetitions <= 0:
            raise GateInputError(f"policy.groups.{name} 的 target 或 repetitions 配置无效")
        if not _valid_min_time(min_time):
            raise GateInputError(f"policy.groups.{name}.min_time 配置无效")
        if not isinstance(tolerance, (int, float)) or isinstance(tolerance, bool):
            raise GateInputError(f"policy.groups.{name}.tolerance 配置无效")
        if not math.isfinite(float(tolerance)) or tolerance < 0:
            raise GateInputError(f"policy.groups.{name}.tolerance 配置无效")
        confirmation_repetitions: int | None = None
        confirmation_min_time: str | None = None
        if kind == "sampling":
            confirmation_value: Any = group_data.get("confirmation_repetitions")
            confirmation_time: Any = group_data.get("confirmation_min_time")
            if not _integer(confirmation_value) or confirmation_value <= 0:
                raise GateInputError(f"policy.groups.{name}.confirmation_repetitions 配置无效")
            if confirmation_value % 2 != 0:
                raise GateInputError(f"policy.groups.{name}.confirmation_repetitions 必须为偶数")
            if not _valid_min_time(confirmation_time):
                raise GateInputError(f"policy.groups.{name}.confirmation_min_time 配置无效")
            confirmation_repetitions = confirmation_value
            confirmation_min_time = confirmation_time
        groups[name] = GroupPolicy(
            name=name,
            kind=kind,
            standard=standard,
            target=target,
            repetitions=repetitions,
            min_time=min_time,
            tolerance=float(tolerance),
            confirmation_repetitions=confirmation_repetitions,
            confirmation_min_time=confirmation_min_time,
        )
    return GatePolicy(
        format_version=FORMAT_VERSION,
        baseline=BaselinePolicy(
            name=baseline_data["name"],
            branch=baseline_data["branch"],
            workflow=baseline_data["workflow"],
            initial_sampling_commit=baseline_data["initial_sampling_commit"],
            publish_events=tuple(publish_events_value),
        ),
        groups=groups,
    )


def _valid_min_time(value: Any) -> bool:
    if not _nonempty_string(value) or not value.endswith("s"):
        return False
    try:
        seconds: float = float(value[:-1])
    except ValueError:
        return False
    return math.isfinite(seconds) and seconds > 0


def _context_error(context: Any) -> str | None:
    if not _is_mapping(context):
        return "context 必须是 JSON 对象"
    string_fields: tuple[str, ...] = (
        "repository", "event", "ref", "candidate_commit", "head_commit",
    )
    if any(not _nonempty_string(context.get(key)) for key in string_fields):
        return "context 缺少必需字符串字段"
    if not _integer(context.get("repository_id")):
        return "context.repository_id 必须是整数"
    if not _identifier(context.get("run_id")):
        return "context.run_id 必须有值"
    if not _identifier(context.get("run_attempt")):
        return "context.run_attempt 必须有值"
    boolean_fields: tuple[str, ...] = ("fork_pr", "force_update_baseline", "cancelled")
    if any(not isinstance(context.get(key), bool) for key in boolean_fields):
        return "context 的 fork_pr、force_update_baseline、cancelled 必须为布尔值"
    return None


def _dispatch_refresh_allowed(policy: GatePolicy, context: Mapping[str, Any]) -> bool:
    expected_ref: str = f"refs/heads/{policy.baseline.branch}"
    return (
        context.get("event") == "workflow_dispatch"
        and context.get("ref") == expected_ref
        and context.get("force_update_baseline") is True
    )


def _target_branch(policy: GatePolicy, context: Mapping[str, Any]) -> bool:
    return context.get("ref") == f"refs/heads/{policy.baseline.branch}"


def _parse_timestamp(value: Any, description: str) -> datetime:
    if not _nonempty_string(value):
        raise GateInputError(f"{description} 缺少 created_at")
    normalized: str = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        result: datetime = datetime.fromisoformat(normalized)
    except ValueError as error:
        raise GateInputError(f"{description}.created_at 不是有效 ISO 时间") from error
    if result.tzinfo is None:
        raise GateInputError(f"{description}.created_at 必须包含时区")
    return result


def _run_path(run: Mapping[str, Any]) -> str | None:
    path: Any = run.get("path")
    if not _nonempty_string(path):
        return None
    return path.split("@", maxsplit=1)[0]


def _record_incomplete_baseline(
    records: list[tuple[datetime | None, int | None, ErrorRecord]],
    artifact: Mapping[str, Any],
    message: str,
) -> None:
    timestamp: datetime | None = None
    artifact_id: int | None = None
    if _integer(artifact.get("id")):
        artifact_id = artifact["id"]
    try:
        timestamp = _parse_timestamp(artifact.get("created_at"), f"artifact {artifact.get('id', '<unknown>')}")
    except GateInputError:
        timestamp = None
    records.append((timestamp, artifact_id, ErrorRecord(GateStatus.DATA_ERROR, message)))


def _select_baseline(
    policy: GatePolicy,
    context: Mapping[str, Any],
    query: Any,
) -> tuple[BaselineCandidate | None, list[ErrorRecord], bool]:
    if not _is_mapping(query):
        return None, [ErrorRecord(GateStatus.DATA_ERROR, "query 必须是 JSON 对象")], False
    if not isinstance(query.get("success"), bool):
        return None, [ErrorRecord(GateStatus.DATA_ERROR, "query.success 必须为布尔值")], False
    if query["success"] is False:
        detail: Any = query.get("error")
        message: str = f"基线查询失败：{detail}" if _nonempty_string(detail) else "基线查询失败"
        return None, [ErrorRecord(GateStatus.EXECUTION_ERROR, message)], False

    artifacts: Any = query.get("artifacts")
    runs: Any = query.get("runs")
    if not isinstance(artifacts, list) or not _is_mapping(runs):
        return None, [ErrorRecord(GateStatus.DATA_ERROR, "query 缺少完整的 artifacts 或 runs 元数据")], False

    matching_named_artifact: bool = False
    incomplete_records: list[tuple[datetime | None, int | None, ErrorRecord]] = []
    eligible: list[BaselineCandidate] = []
    allowed_events: set[str] = {"push", "workflow_dispatch"}
    for artifact_index, artifact_value in enumerate(artifacts):
        if not _is_mapping(artifact_value):
            return None, [ErrorRecord(GateStatus.DATA_ERROR, f"artifacts[{artifact_index}] 必须是 JSON 对象")], False
        artifact: Mapping[str, Any] = artifact_value
        if artifact.get("name") != policy.baseline.name:
            continue
        matching_named_artifact = True
        expired: Any = artifact.get("expired")
        if not isinstance(expired, bool):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"同名 artifact {artifact.get('id', '<unknown>')} 缺少有效 expired 元数据",
            )
            continue
        if expired:
            continue
        artifact_run: Any = artifact.get("workflow_run")
        if not _is_mapping(artifact_run) or not _identifier(artifact_run.get("id")):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"同名 artifact {artifact.get('id', '<unknown>')} 缺少所属 workflow_run.id",
            )
            continue
        run_id: str = str(artifact_run["id"])
        if run_id == str(context["run_id"]):
            continue
        run_value: Any = runs.get(run_id)
        if not _is_mapping(run_value):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"同名 artifact {artifact.get('id', '<unknown>')} 缺少 run {run_id} 的完整 API 元数据",
            )
            continue
        run: Mapping[str, Any] = run_value
        if str(run.get("id", "")) != run_id:
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"artifact {artifact.get('id', '<unknown>')} 与 workflow run ID 不一致",
            )
            continue
        path: str | None = _run_path(run)
        if path is None:
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"同名 artifact 的 run {run_id} 缺少 workflow path 元数据",
            )
            continue
        if path != policy.baseline.workflow:
            continue
        event: Any = run.get("event")
        if not _nonempty_string(event):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"符合来源的 run {run_id} 缺少 event 元数据",
            )
            continue
        if event not in allowed_events:
            continue
        repository_value: Any = run.get("repository")
        head_repository_value: Any = run.get("head_repository")
        if not _is_mapping(repository_value) or not _integer(repository_value.get("id")):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"符合来源的 run {run_id} 缺少 repository.id 元数据",
            )
            continue
        if not _is_mapping(head_repository_value) or not _integer(head_repository_value.get("id")):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"符合来源的 run {run_id} 缺少 head_repository.id 元数据",
            )
            continue
        if repository_value["id"] != context["repository_id"]:
            continue
        if head_repository_value["id"] != context["repository_id"]:
            continue
        branch: Any = run.get("head_branch")
        if not _nonempty_string(branch):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"符合来源的 run {run_id} 缺少 head_branch 元数据",
            )
            continue
        if branch != policy.baseline.branch:
            continue
        status: Any = run.get("status")
        conclusion: Any = run.get("conclusion")
        if status != "completed" or conclusion != SUCCESS_OUTCOME:
            continue

        artifact_id: Any = artifact.get("id")
        try:
            created_at: datetime = _parse_timestamp(artifact.get("created_at"), f"artifact {artifact_id}")
        except GateInputError as error:
            _record_incomplete_baseline(incomplete_records, artifact, str(error))
            continue
        commit: Any = run.get("head_sha")
        if not _nonempty_string(commit):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"符合来源的 run {run_id} 缺少 head_sha 基线提交",
            )
            continue
        if not _integer(artifact_id):
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"符合来源的 run {run_id} 对应 artifact 缺少整数 id",
            )
            continue
        artifact_run_repo_id: Any = artifact_run.get("repository_id")
        artifact_run_head_id: Any = artifact_run.get("head_repository_id")
        if artifact_run_repo_id is not None and artifact_run_repo_id != repository_value["id"]:
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"artifact {artifact_id} 的 workflow_run.repository_id 与所属 run 不一致",
            )
            continue
        if artifact_run_head_id is not None and artifact_run_head_id != head_repository_value["id"]:
            _record_incomplete_baseline(
                incomplete_records,
                artifact,
                f"artifact {artifact_id} 的 workflow_run.head_repository_id 与所属 run 不一致",
            )
            continue
        eligible.append(BaselineCandidate(artifact_id, run_id, commit, created_at))

    selected: BaselineCandidate | None = max(
        eligible,
        key=lambda candidate: (candidate.created_at, candidate.artifact_id),
        default=None,
    )
    if incomplete_records:
        blocking_records: list[ErrorRecord] = []
        for timestamp, artifact_id, error in incomplete_records:
            if selected is None or timestamp is None:
                blocking_records.append(error)
            elif timestamp > selected.created_at:
                blocking_records.append(error)
            elif timestamp == selected.created_at and (artifact_id is None or artifact_id >= selected.artifact_id):
                blocking_records.append(error)
        if blocking_records:
            return None, blocking_records, matching_named_artifact
    return selected, [], matching_named_artifact


def _required_steps(policy: GatePolicy, mode: PlanMode) -> dict[str, list[str]]:
    candidate_steps: list[str] = [
        "checkout_candidate", "build_candidate", "enumerate_candidate", "measure_candidate",
    ]
    baseline_steps: list[str] = [
        "checkout_baseline", "build_baseline", "enumerate_baseline", "measure_baseline",
    ]
    required: dict[str, list[str]] = {}
    for name in GROUP_NAMES:
        group_policy: GroupPolicy = policy.groups[name]
        if name == "general" and mode == PlanMode.REFRESH:
            required[name] = list(candidate_steps)
        elif group_policy.kind == "general":
            required[name] = [*candidate_steps, *baseline_steps]
        else:
            required[name] = [*candidate_steps, *baseline_steps, "aggregate", "confirm"]
    return required


def _serialized_policy(policy: GatePolicy) -> dict[str, Any]:
    value: dict[str, Any] = {
        "format_version": policy.format_version,
        "baseline": {
            **asdict(policy.baseline),
            "publish_events": list(policy.baseline.publish_events),
        },
        "groups": {name: asdict(group) for name, group in policy.groups.items()},
    }
    return value


def _error_plan(
    policy_value: Any,
    context_value: Any,
    errors: Sequence[ErrorRecord],
    mode: PlanMode = PlanMode.ERROR,
) -> dict[str, Any]:
    return {
        "format_version": FORMAT_VERSION,
        "valid": False,
        "mode": mode.value,
        "policy": copy.deepcopy(policy_value) if _is_mapping(policy_value) else {},
        "context": copy.deepcopy(context_value) if _is_mapping(context_value) else {},
        "baseline": None,
        "sampling_commit": None,
        "groups": list(GROUP_NAMES),
        "required_steps": {name: [] for name in GROUP_NAMES},
        "errors": [_error_to_dict(error) for error in errors],
    }


def _error_to_dict(error: ErrorRecord) -> dict[str, Any]:
    record: dict[str, Any] = {"status": error.status.value, "message": error.message}
    if error.group is not None:
        record["group"] = error.group
    return record


def plan_run(policy: dict[str, Any], context: dict[str, Any], query: dict[str, Any]) -> dict[str, Any]:
    """依据策略、运行上下文和完整 API 记录生成确定性计划。"""
    try:
        parsed_policy: GatePolicy = _parse_policy(policy)
    except GateInputError as error:
        return _error_plan(policy, context, [ErrorRecord(GateStatus.DATA_ERROR, str(error))])
    context_problem: str | None = _context_error(context)
    if context_problem is not None:
        return _error_plan(
            _serialized_policy(parsed_policy), context,
            [ErrorRecord(GateStatus.CONTEXT_ERROR, context_problem)],
        )
    if context["cancelled"]:
        return _error_plan(
            _serialized_policy(parsed_policy), context,
            [ErrorRecord(GateStatus.CANCELLED, "运行已取消")],
            PlanMode.CANCELLED,
        )
    force_requested: bool = context["force_update_baseline"]
    refresh_allowed: bool = _dispatch_refresh_allowed(parsed_policy, context)
    if force_requested and not refresh_allowed:
        return _error_plan(
            _serialized_policy(parsed_policy), context,
            [ErrorRecord(
                GateStatus.CONTEXT_ERROR,
                "强制更新仅允许目标分支上的 workflow_dispatch",
            )],
        )

    baseline, baseline_errors, matching_record = _select_baseline(parsed_policy, context, query)
    if baseline_errors:
        return _error_plan(_serialized_policy(parsed_policy), context, baseline_errors)
    if baseline is None and not refresh_allowed:
        reason: str = (
            "没有可用的成功基线；目标分支需显式 workflow_dispatch 并启用强制更新"
            if matching_record
            else "基线列表为空；需在目标分支显式 workflow_dispatch 并启用强制更新"
        )
        return _error_plan(
            _serialized_policy(parsed_policy), context,
            [ErrorRecord(GateStatus.BASELINE_ERROR, reason)],
        )

    mode: PlanMode = PlanMode.REFRESH if refresh_allowed else PlanMode.COMPARE
    baseline_record: dict[str, Any] | None = None
    if baseline is not None:
        baseline_record = {
            "artifact_id": baseline.artifact_id,
            "run_id": baseline.run_id,
            "commit": baseline.commit,
        }
    sampling_commit: str = (
        baseline.commit if baseline is not None else parsed_policy.baseline.initial_sampling_commit
    )
    return {
        "format_version": FORMAT_VERSION,
        "valid": True,
        "mode": mode.value,
        "policy": _serialized_policy(parsed_policy),
        "context": copy.deepcopy(context),
        "baseline": baseline_record,
        "sampling_commit": sampling_commit,
        "groups": list(GROUP_NAMES),
        "required_steps": _required_steps(parsed_policy, mode),
        "errors": [],
    }


def _comparison_api() -> tuple[Any, Any, Any, Any]:
    from compare_benchmark import BenchmarkDataError, benchmark_name, compare_results, parse_results

    return BenchmarkDataError, benchmark_name, compare_results, parse_results


def _merge_api() -> tuple[Any, Any, Any]:
    from confirm_sampling_benchmark import replace_measurements, select_confirmation_cases
    from merge_benchmark_repetitions import merge_results

    return replace_measurements, select_confirmation_cases, merge_results


def _measurement_entries(data: Any, source: str, aggregate_only: bool) -> list[dict[str, Any]]:
    if not _is_mapping(data) or not isinstance(data.get("benchmarks"), list):
        raise GateInputError(f"{source} 缺少 benchmarks 列表")
    entries: list[dict[str, Any]] = []
    for index, value in enumerate(data["benchmarks"]):
        if not _is_mapping(value):
            raise GateInputError(f"{source}.benchmarks[{index}] 不是对象")
        entry: dict[str, Any] = dict(value)
        if entry.get("error_occurred") or entry.get("error_message"):
            raise GateInputError(f"{source} 中测量项执行失败：{entry.get('name', '<unknown>')}")
        if aggregate_only:
            if entry.get("aggregate_name") == "median":
                entries.append(entry)
        elif entry.get("run_type", "iteration") == "iteration":
            entries.append(entry)
    return entries


def _entry_names(data: Any, source: str, aggregate_only: bool) -> list[str]:
    _, benchmark_name, _, _ = _comparison_api()
    entries: list[dict[str, Any]] = _measurement_entries(data, source, aggregate_only)
    names: list[str] = []
    for entry in entries:
        name: Any = benchmark_name(entry)
        if not _nonempty_string(name):
            raise GateInputError(f"{source} 含无效 benchmark 名称")
        names.append(name)
    duplicate_names: list[str] = sorted({name for name in names if names.count(name) > 1})
    if duplicate_names:
        raise GateInputError(f"{source} 含重复 benchmark 名称：{', '.join(duplicate_names)}")
    if not names:
        raise GateInputError(f"{source} 不含有效测量项")
    return names


def _expected_names(value: Any, source: str) -> list[str]:
    if not isinstance(value, list) or any(not _nonempty_string(name) for name in value):
        raise GateInputError(f"{source} 必须是字符串列表")
    names: list[str] = [name.strip() for name in value]
    if not names:
        raise GateInputError(f"{source} 不能为空")
    duplicate_names: list[str] = sorted({name for name in names if names.count(name) > 1})
    if duplicate_names:
        raise GateInputError(f"{source} 含重复名称：{', '.join(duplicate_names)}")
    return names


def _assert_exact_names(actual: Sequence[str], expected: Sequence[str], source: str) -> None:
    actual_set: set[str] = set(actual)
    expected_set: set[str] = set(expected)
    if actual_set != expected_set:
        missing: list[str] = sorted(expected_set - actual_set)
        unexpected: list[str] = sorted(actual_set - expected_set)
        details: list[str] = []
        if missing:
            details.append(f"缺少 {', '.join(missing)}")
        if unexpected:
            details.append(f"多出 {', '.join(unexpected)}")
        raise GateInputError(f"{source} 与预期清单不一致：{'；'.join(details)}")


def _parsed_medians(data: Any, source: str) -> dict[str, dict[str, Any]]:
    _, benchmark_name, _, parse_results = _comparison_api()
    _entry_names(data, source, aggregate_only=True)
    result: Any = parse_results(data, source)
    if not isinstance(result, dict) or not result:
        raise GateInputError(f"{source} 未解析出有效 median 测量项")
    normalized: dict[str, dict[str, Any]] = {}
    for entry in result.values():
        if not _is_mapping(entry):
            raise GateInputError(f"{source} median 映射包含非对象记录")
        name: Any = benchmark_name(entry)
        if not _nonempty_string(name) or name in normalized:
            raise GateInputError(f"{source} 含重复或无效 median 名称：{name}")
        normalized[name] = dict(entry)
    return normalized


def _assert_measurement_identity(
    expected: Mapping[str, dict[str, Any]],
    actual: Mapping[str, dict[str, Any]],
    source: str,
) -> None:
    for name in expected:
        expected_entry: Mapping[str, Any] = expected[name]
        actual_entry: Mapping[str, Any] | None = actual.get(name)
        if actual_entry is None:
            raise GateInputError(f"{source} 缺少测量项 {name}")
        for field in ("cpu_time", "real_time", "time_unit"):
            if field in expected_entry and expected_entry.get(field) != actual_entry.get(field):
                raise GateInputError(f"{source} 测量项 {name} 的 {field} 与原始轮次不一致")


def _comparison_dict(current: Mapping[str, dict[str, Any]], baseline: Mapping[str, dict[str, Any]], tolerance: float) -> dict[str, Any]:
    _, _, compare_results, _ = _comparison_api()
    comparison: Any = compare_results(current, baseline, tolerance)
    items: list[dict[str, Any]] = []
    for item in comparison.items:
        if _is_mapping(item):
            source: Mapping[str, Any] = item
            fields: dict[str, Any] = {
                "name": source.get("name"),
                "baseline_ms": source.get("baseline_ms"),
                "current_ms": source.get("current_ms"),
                "change": source.get("change"),
                "regression": source.get("regression"),
            }
        else:
            fields = {
                "name": item.name,
                "baseline_ms": item.baseline_ms,
                "current_ms": item.current_ms,
                "change": item.change,
                "regression": item.regression,
            }
        items.append(fields)
    return {
        "items": items,
        "regressions": list(comparison.regressions),
        "new_items": list(comparison.new_items),
        "missing_items": list(comparison.missing_items),
    }


def _stringify_expected(expected: Mapping[str, Any], key: str, group: str) -> list[str]:
    if key not in expected:
        raise GateInputError(f"{group}.expected 缺少 {key}")
    return _expected_names(expected[key], f"{group}.expected.{key}")


def _group_input(
    group_name: str,
    group_value: Any,
    plan: Mapping[str, Any],
    policy: GatePolicy,
) -> tuple[Mapping[str, Any] | None, Mapping[str, Any], list[ErrorRecord]]:
    if not _is_mapping(group_value):
        return None, {}, [ErrorRecord(GateStatus.EXECUTION_ERROR, "组文件未能加载", group_name)]
    metadata: Any = group_value.get("metadata")
    files: Any = group_value.get("files")
    if not _is_mapping(metadata):
        return None, files if _is_mapping(files) else {}, [
            *_group_load_errors(group_value, group_name),
            ErrorRecord(GateStatus.EXECUTION_ERROR, "group.json 缺失或无法读取", group_name),
        ]
    if not _is_mapping(files):
        return None, {}, [
            *_group_load_errors(group_value, group_name),
            ErrorRecord(GateStatus.EXECUTION_ERROR, "组结果文件未能加载", group_name),
        ]
    return metadata, files, _group_load_errors(group_value, group_name)


def _group_load_errors(group_value: Mapping[str, Any], group_name: str) -> list[ErrorRecord]:
    raw: Any = group_value.get("load_errors", [])
    if not isinstance(raw, list):
        return [ErrorRecord(GateStatus.DATA_ERROR, "load_errors 必须是列表", group_name)]
    result: list[ErrorRecord] = []
    for value in raw:
        if not _is_mapping(value):
            result.append(ErrorRecord(GateStatus.DATA_ERROR, str(value), group_name))
            continue
        try:
            status: GateStatus = GateStatus(value.get("status"))
        except (ValueError, TypeError):
            status = GateStatus.DATA_ERROR
        result.append(ErrorRecord(status, str(value.get("message", "组文件加载失败")), group_name))
    return result


def _validate_group_metadata(
    group_name: str,
    metadata: Mapping[str, Any],
    plan: Mapping[str, Any],
    required: Sequence[str],
) -> list[ErrorRecord]:
    context: Mapping[str, Any] = plan.get("context", {})
    errors: list[ErrorRecord] = []
    if not _integer(metadata.get("format_version")) or metadata.get("format_version") != FORMAT_VERSION:
        errors.append(ErrorRecord(GateStatus.DATA_ERROR, "group.json.format_version 不匹配", group_name))
    if metadata.get("group") != group_name:
        errors.append(ErrorRecord(GateStatus.DATA_ERROR, "group.json.group 与计划不匹配", group_name))
    for field in ("run_id", "run_attempt"):
        if str(metadata.get(field, "")) != str(context.get(field, "")):
            errors.append(ErrorRecord(GateStatus.DATA_ERROR, f"group.json.{field} 与当前 run 不匹配", group_name))
    if metadata.get("candidate_commit") != context.get("candidate_commit"):
        errors.append(ErrorRecord(GateStatus.DATA_ERROR, "group.json.candidate_commit 与计划不匹配", group_name))
    expected_baseline_commit: Any = (
        plan.get("baseline", {}).get("commit")
        if group_name == "general" and plan.get("mode") == PlanMode.COMPARE.value
        else plan.get("sampling_commit")
    )
    if group_name == "general" and plan.get("mode") == PlanMode.REFRESH.value:
        if metadata.get("baseline_commit") not in (None, ""):
            errors.append(ErrorRecord(GateStatus.DATA_ERROR, "REFRESH general 不应声明历史 baseline_commit", group_name))
    elif metadata.get("baseline_commit") != expected_baseline_commit:
        errors.append(ErrorRecord(GateStatus.DATA_ERROR, "group.json.baseline_commit 与计划不匹配", group_name))
    steps: Any = metadata.get("steps")
    if not _is_mapping(steps):
        errors.append(ErrorRecord(GateStatus.DATA_ERROR, "group.json.steps 必须是对象", group_name))
    else:
        for step in required:
            if steps.get(step) != SUCCESS_OUTCOME:
                errors.append(ErrorRecord(
                    GateStatus.EXECUTION_ERROR,
                    f"必需步骤 {step} 的 outcome 为 {steps.get(step, '<missing>')}",
                    group_name,
                ))
    if not _is_mapping(metadata.get("expected")):
        errors.append(ErrorRecord(GateStatus.DATA_ERROR, "group.json.expected 必须是对象", group_name))
    if not _is_mapping(metadata.get("environment")):
        errors.append(ErrorRecord(GateStatus.DATA_ERROR, "group.json.environment 必须是对象", group_name))
    return errors


def _file(files: Mapping[str, Any], key: str, group_name: str) -> Any:
    if key not in files:
        raise GateExecutionError(f"{group_name} 缺少必需结果文件 {key}")
    return files[key]


def _evaluate_general(
    group_name: str,
    metadata: Mapping[str, Any],
    files: Mapping[str, Any],
    plan: Mapping[str, Any],
    group_policy: GroupPolicy,
) -> tuple[dict[str, Any], list[ErrorRecord]]:
    mode: str = str(plan.get("mode"))
    expected: Mapping[str, Any] = metadata.get("expected", {})
    errors: list[ErrorRecord] = []
    candidate_expected: list[str] = _stringify_expected(expected, "candidate", group_name)
    candidate_data: Any = _file(files, "candidate.json", group_name)
    candidate_names: list[str] = _entry_names(candidate_data, f"{group_name} candidate.json", True)
    _assert_exact_names(candidate_names, candidate_expected, f"{group_name} candidate.json")
    candidate: dict[str, dict[str, Any]] = _parsed_medians(candidate_data, f"{group_name} candidate.json")
    for name, entry in candidate.items():
        if entry.get("repetitions") != group_policy.repetitions:
            raise GateInputError(f"{group_name} candidate.json 的 {name} repetitions 与策略不一致")
    if mode == PlanMode.REFRESH.value:
        if not isinstance(expected.get("baseline"), list) or expected.get("baseline") != []:
            raise GateInputError(f"{group_name} REFRESH 的 expected.baseline 必须为空列表")
        detail: dict[str, Any] = {
            "status": GroupStatus.PASS.value,
            "comparison_status": "SKIPPED",
            "waived": False,
            "items": [],
            "regressions": [],
            "new_items": [],
            "missing_items": [],
            "expected": {"candidate": candidate_expected, "baseline": []},
        }
        return detail, errors
    baseline_expected: list[str] = _stringify_expected(expected, "baseline", group_name)
    missing_candidate: list[str] = sorted(set(baseline_expected) - set(candidate_expected))
    if missing_candidate:
        raise GateInputError(f"{group_name} candidate 预期清单缺少基线项：{', '.join(missing_candidate)}")
    baseline_data: Any = _file(files, "baseline.json", group_name)
    baseline_names: list[str] = _entry_names(baseline_data, f"{group_name} baseline.json", True)
    _assert_exact_names(baseline_names, baseline_expected, f"{group_name} baseline.json")
    baseline: dict[str, dict[str, Any]] = _parsed_medians(baseline_data, f"{group_name} baseline.json")
    for name, entry in baseline.items():
        if entry.get("repetitions") != group_policy.repetitions:
            raise GateInputError(f"{group_name} baseline.json 的 {name} repetitions 与策略不一致")
    comparison: dict[str, Any] = _comparison_dict(candidate, baseline, group_policy.tolerance)
    detail = {
        "status": GroupStatus.REGRESSION.value if comparison["regressions"] else GroupStatus.PASS.value,
        "comparison_status": "COMPARED",
        "waived": False,
        **comparison,
        "expected": {"candidate": candidate_expected, "baseline": baseline_expected},
    }
    return detail, errors


def _validate_confirmation_metadata(
    group_name: str,
    metadata: Mapping[str, Any],
    environment: Mapping[str, Any],
    group_policy: GroupPolicy,
    selected_cases: Sequence[str],
) -> None:
    if metadata.get("complete") is not True:
        raise GateInputError(f"{group_name} confirmation 未标记 complete")
    if metadata.get("cases") != list(selected_cases):
        raise GateInputError(f"{group_name} confirmation.cases 与初测选择不一致")
    if metadata.get("repetitions") != group_policy.confirmation_repetitions:
        raise GateInputError(f"{group_name} confirmation.repetitions 与策略不一致")
    if metadata.get("min_time") != group_policy.confirmation_min_time:
        raise GateInputError(f"{group_name} confirmation.min_time 与策略不一致")
    if metadata.get("tolerance") != group_policy.tolerance:
        raise GateInputError(f"{group_name} confirmation.tolerance 与策略不一致")
    if metadata.get("cpu") != environment.get("cpu"):
        raise GateInputError(f"{group_name} confirmation.cpu 与测量环境不一致")
    runs: Any = metadata.get("runs")
    if not isinstance(runs, list):
        raise GateInputError(f"{group_name} confirmation.runs 必须是列表")
    expected_run_count: int = int(group_policy.confirmation_repetitions or 0) * (2 if selected_cases else 0)
    if len(runs) != expected_run_count:
        raise GateInputError(f"{group_name} confirmation.runs 数量与选中项状态不一致")
    observed: set[tuple[str, int]] = set()
    for run in runs:
        if not _is_mapping(run):
            raise GateInputError(f"{group_name} confirmation.runs 含非对象记录")
        variant: Any = run.get("variant")
        round_value: Any = run.get("round")
        returncode: Any = run.get("returncode")
        if variant not in ("baseline", "candidate") or not _integer(round_value):
            raise GateInputError(f"{group_name} confirmation.runs 含无效 variant 或 round")
        if not _integer(returncode) or returncode != 0:
            raise GateInputError(f"{group_name} confirmation {variant} 第 {round_value} 轮未成功")
        key: tuple[str, int] = (variant, round_value)
        if key in observed:
            raise GateInputError(f"{group_name} confirmation.runs 含重复轮次 {variant}-{round_value}")
        observed.add(key)
    expected_runs: set[tuple[str, int]] = {
        (variant, round_number)
        for variant in ("baseline", "candidate")
        for round_number in range(1, int(group_policy.confirmation_repetitions or 0) + 1)
    } if selected_cases else set()
    if observed != expected_runs:
        raise GateInputError(f"{group_name} confirmation.runs 未覆盖全部双边轮次")


def _evaluate_sampling(
    group_name: str,
    metadata: Mapping[str, Any],
    files: Mapping[str, Any],
    group_policy: GroupPolicy,
) -> tuple[dict[str, Any], list[ErrorRecord]]:
    expected: Mapping[str, Any] = metadata.get("expected", {})
    candidate_expected: list[str] = _stringify_expected(expected, "candidate", group_name)
    baseline_expected: list[str] = _stringify_expected(expected, "baseline", group_name)
    if set(candidate_expected) != set(baseline_expected):
        raise GateInputError(f"{group_name} 双边 expected 清单必须相同")
    expected_names: list[str] = candidate_expected
    initial_data: dict[str, Any] = {
        "candidate": _file(files, "candidate-initial.json", group_name),
        "baseline": _file(files, "baseline-initial.json", group_name),
    }
    initial: dict[str, dict[str, dict[str, Any]]] = {}
    round_data: dict[str, list[dict[str, Any]]] = {"candidate": [], "baseline": []}
    for variant in ("candidate", "baseline"):
        result: Any = initial_data[variant]
        initial_names: list[str] = _entry_names(result, f"{group_name} {variant}-initial.json", True)
        _assert_exact_names(initial_names, expected_names, f"{group_name} {variant}-initial.json")
        initial[variant] = _parsed_medians(result, f"{group_name} {variant}-initial.json")
        for round_number in range(1, group_policy.repetitions + 1):
            round_key: str = f"rounds/{variant}-{round_number}.json"
            round_result: Any = _file(files, round_key, group_name)
            round_names: list[str] = _entry_names(round_result, f"{group_name} {round_key}", False)
            _assert_exact_names(round_names, expected_names, f"{group_name} {round_key}")
            round_data[variant].append(round_result)
        replacement_function, _, merge_results = _merge_api()
        raw_aggregate: dict[str, Any] = merge_results(round_data[variant])
        recomputed: dict[str, dict[str, Any]] = _parsed_medians(raw_aggregate, f"{group_name} merged {variant} rounds")
        _assert_measurement_identity(recomputed, initial[variant], f"{group_name} {variant}-initial.json")
        for name, entry in initial[variant].items():
            if entry.get("repetitions") != group_policy.repetitions:
                raise GateInputError(f"{group_name} {variant}-initial.json 的 {name} repetitions 与策略不一致")

    confirmation_metadata: Any = _file(files, "confirmation/metadata.json", group_name)
    if not _is_mapping(confirmation_metadata):
        raise GateInputError(f"{group_name} confirmation/metadata.json 必须是对象")
    _, select_confirmation_cases, _ = _merge_api()
    selected_cases: list[str] = select_confirmation_cases(
        initial["baseline"], initial["candidate"], group_policy.tolerance,
    )
    _validate_confirmation_metadata(
        group_name,
        confirmation_metadata,
        metadata.get("environment", {}),
        group_policy,
        selected_cases,
    )

    confirmation_rounds: dict[str, list[dict[str, Any]]] = {"candidate": [], "baseline": []}
    if selected_cases:
        for variant in ("candidate", "baseline"):
            for round_number in range(1, int(group_policy.confirmation_repetitions or 0) + 1):
                round_key = f"confirmation/rounds/{variant}-{round_number}.json"
                result = _file(files, round_key, group_name)
                round_names = _entry_names(result, f"{group_name} {round_key}", False)
                _assert_exact_names(round_names, selected_cases, f"{group_name} {round_key}")
                confirmation_rounds[variant].append(result)

    _, _, merge_results = _merge_api()
    final_data: dict[str, Any] = {
        "candidate": _file(files, "confirmation/candidate.json", group_name),
        "baseline": _file(files, "confirmation/baseline.json", group_name),
    }
    final_maps: dict[str, dict[str, dict[str, Any]]] = {}
    for variant in ("candidate", "baseline"):
        final_names: list[str] = _entry_names(final_data[variant], f"{group_name} confirmation/{variant}.json", True)
        _assert_exact_names(final_names, expected_names, f"{group_name} confirmation/{variant}.json")
        final_maps[variant] = _parsed_medians(final_data[variant], f"{group_name} confirmation/{variant}.json")
        if selected_cases:
            confirmation_aggregate: dict[str, Any] = merge_results(confirmation_rounds[variant])
            initial_with_confirmation: dict[str, Any] = replacement_function(
                initial_data[variant], confirmation_aggregate, selected_cases,
            )
        else:
            initial_with_confirmation = initial_data[variant]
        expected_final: dict[str, dict[str, Any]] = _parsed_medians(
            initial_with_confirmation,
            f"{group_name} expected confirmation/{variant}.json",
        )
        _assert_measurement_identity(
            expected_final,
            final_maps[variant],
            f"{group_name} confirmation/{variant}.json",
        )

    comparison: dict[str, Any] = _comparison_dict(
        final_maps["candidate"], final_maps["baseline"], group_policy.tolerance,
    )
    detail: dict[str, Any] = {
        "status": GroupStatus.REGRESSION.value if comparison["regressions"] else GroupStatus.PASS.value,
        "comparison_status": "CONFIRMED",
        "waived": False,
        "expected": {"candidate": candidate_expected, "baseline": baseline_expected},
        "confirmation": {
            "cases": selected_cases,
            "repetitions": group_policy.confirmation_repetitions,
            "min_time": group_policy.confirmation_min_time,
            "tolerance": group_policy.tolerance,
            "cpu": confirmation_metadata.get("cpu"),
        },
        **comparison,
    }
    return detail, []


def _status_from_errors(errors: Sequence[ErrorRecord]) -> GroupStatus:
    if any(error.status == GateStatus.DATA_ERROR for error in errors):
        return GroupStatus.DATA_ERROR
    if errors:
        return GroupStatus.EXECUTION_ERROR
    return GroupStatus.PASS


def _execution_errors(execution: Any, groups: Sequence[str]) -> tuple[list[ErrorRecord], dict[str, list[ErrorRecord]]]:
    global_errors: list[ErrorRecord] = []
    group_errors: dict[str, list[ErrorRecord]] = {name: [] for name in groups}
    if not _is_mapping(execution):
        return [ErrorRecord(GateStatus.EXECUTION_ERROR, "execution 必须是 JSON 对象")], group_errors
    plan_job_state: Any = execution.get("plan_job", "<missing>")
    if plan_job_state != SUCCESS_OUTCOME:
        plan_status: GateStatus = (
            GateStatus.CANCELLED if plan_job_state == CANCELLED_PLATFORM_STATE else GateStatus.EXECUTION_ERROR
        )
        global_errors.append(ErrorRecord(
            plan_status,
            f"plan_job 状态为 {plan_job_state}",
        ))
    jobs: Any = execution.get("jobs")
    downloads: Any = execution.get("downloads")
    if not _is_mapping(jobs):
        global_errors.append(ErrorRecord(GateStatus.EXECUTION_ERROR, "execution.jobs 必须是对象"))
        jobs = {}
    if not _is_mapping(downloads):
        global_errors.append(ErrorRecord(GateStatus.EXECUTION_ERROR, "execution.downloads 必须是对象"))
        downloads = {}
    for name in groups:
        job_state: Any = jobs.get(name, "<missing>")
        if job_state != SUCCESS_OUTCOME:
            job_status: GateStatus = (
                GateStatus.CANCELLED if job_state == CANCELLED_PLATFORM_STATE else GateStatus.EXECUTION_ERROR
            )
            group_errors[name].append(ErrorRecord(
                job_status,
                f"测量 job 状态为 {job_state}",
                name,
            ))
        download_state: Any = downloads.get(name, "<missing>")
        if download_state != SUCCESS_OUTCOME:
            download_status: GateStatus = (
                GateStatus.CANCELLED if download_state == CANCELLED_PLATFORM_STATE else GateStatus.EXECUTION_ERROR
            )
            group_errors[name].append(ErrorRecord(
                download_status,
                f"结果产物下载状态为 {download_state}",
                name,
            ))
    raw_errors: Any = execution.get("errors", [])
    if not isinstance(raw_errors, list):
        global_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "execution.errors 必须是列表"))
    else:
        for raw_error in raw_errors:
            if _is_mapping(raw_error):
                try:
                    status = GateStatus(raw_error.get("status"))
                except (ValueError, TypeError):
                    status = GateStatus.EXECUTION_ERROR
                group_value: Any = raw_error.get("group")
                error = ErrorRecord(status, str(raw_error.get("message", "execution error")), group_value)
            else:
                error = ErrorRecord(GateStatus.EXECUTION_ERROR, str(raw_error))
            if isinstance(error.group, str) and error.group in group_errors:
                group_errors[str(error.group)].append(error)
            else:
                global_errors.append(error)
    return global_errors, group_errors


def _summary(report: Mapping[str, Any]) -> str:
    lines: list[str] = [
        "# 性能门禁报告",
        "",
        f"- 模式：`{report.get('mode', 'ERROR')}`",
        f"- 结论：`{report.get('status', 'ERROR')}`",
        f"- 门禁退出码：`{report.get('exit_code', 2)}`",
        "",
    ]
    for group_name, group in report.get("groups", {}).items():
        lines.extend((f"## {group_name}", "", f"- 状态：`{group.get('status', 'SKIPPED')}`"))
        if group.get("waived"):
            lines.append("- 刷新模式已记录并豁免该组回归。")
        items: list[Mapping[str, Any]] = group.get("items", [])
        for item in items:
            lines.append(
                f"- `{item.get('name')}`：{float(item.get('change', 0.0)):+.1%}，"
                f"baseline {float(item.get('baseline_ms', 0.0)):.4f} ms，"
                f"candidate {float(item.get('current_ms', 0.0)):.4f} ms。"
            )
        for error in group.get("errors", []):
            lines.append(f"- `{error.get('status')}`：{error.get('message')}")
        lines.append("")
    for error in report.get("errors", []):
        lines.append(f"- `{error.get('status')}`：{error.get('message')}")
    lines.append("")
    return "\n".join(lines)


def _report_error(error: ErrorRecord) -> dict[str, Any]:
    return _error_to_dict(error)


def evaluate_run(
    plan: dict[str, Any],
    execution: dict[str, Any],
    groups: dict[str, Any],
) -> dict[str, Any]:
    """逐组求值，保留各组独立有效的测量证据。"""
    plan_errors: list[ErrorRecord] = []
    if not _is_mapping(plan):
        plan = {}
        plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "plan 必须是 JSON 对象"))
    plan_policy: Any = plan.get("policy", {})
    try:
        parsed_policy: GatePolicy | None = _parse_policy(plan_policy)
    except GateInputError as error:
        parsed_policy = None
        plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, f"plan.policy 无效：{error}"))
    if not _integer(plan.get("format_version")) or plan.get("format_version") != FORMAT_VERSION:
        plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "plan.format_version 不匹配"))
    required_steps_by_group: dict[str, list[str]] = {name: [] for name in GROUP_NAMES}
    mode_value: Any = plan.get("mode")
    parsed_mode: PlanMode | None = None
    try:
        parsed_mode = PlanMode(mode_value)
    except (ValueError, TypeError):
        plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "plan.mode 无效"))
    context_value: Any = plan.get("context")
    context: Mapping[str, Any] = context_value if _is_mapping(context_value) else {}
    if parsed_policy is not None and parsed_mode in (PlanMode.COMPARE, PlanMode.REFRESH):
        context_problem: str | None = _context_error(context)
        if context_problem is not None:
            plan_errors.append(ErrorRecord(GateStatus.CONTEXT_ERROR, context_problem))
        elif context.get("cancelled") is True:
            plan_errors.append(ErrorRecord(GateStatus.CANCELLED, "运行已取消"))
        elif parsed_mode == PlanMode.REFRESH and not _dispatch_refresh_allowed(parsed_policy, context):
            plan_errors.append(ErrorRecord(GateStatus.CONTEXT_ERROR, "REFRESH 计划不符合目标分支手动刷新条件"))
        elif parsed_mode == PlanMode.COMPARE and context.get("force_update_baseline") is True:
            plan_errors.append(ErrorRecord(GateStatus.CONTEXT_ERROR, "强制更新请求不能使用 COMPARE 计划"))
        baseline_value: Any = plan.get("baseline")
        expected_sampling_commit: str = parsed_policy.baseline.initial_sampling_commit
        if _is_mapping(baseline_value):
            artifact_id: Any = baseline_value.get("artifact_id")
            run_id: Any = baseline_value.get("run_id")
            commit: Any = baseline_value.get("commit")
            if not _integer(artifact_id) or not _identifier(run_id) or not _nonempty_string(commit):
                plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "plan.baseline 元数据不完整"))
            elif parsed_mode == PlanMode.COMPARE:
                expected_sampling_commit = commit
            else:
                expected_sampling_commit = commit
        elif parsed_mode == PlanMode.COMPARE:
            plan_errors.append(ErrorRecord(GateStatus.BASELINE_ERROR, "COMPARE 计划缺少基线记录"))
        if plan.get("sampling_commit") != expected_sampling_commit:
            plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "plan.sampling_commit 与基线策略不一致"))
        required_steps_by_group = _required_steps(parsed_policy, parsed_mode)
        supplied_steps: Any = plan.get("required_steps")
        if supplied_steps != required_steps_by_group:
            plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "plan.required_steps 与集中策略不一致"))
        if plan.get("groups") != list(GROUP_NAMES):
            plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "plan.groups 与集中策略不一致"))
    elif parsed_mode in (PlanMode.ERROR, PlanMode.CANCELLED) and plan.get("valid") is True:
        plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "错误计划不能标记 valid=true"))
    if plan.get("valid") is not True:
        raw_plan_errors: Any = plan.get("errors", [])
        if isinstance(raw_plan_errors, list):
            for value in raw_plan_errors:
                if _is_mapping(value):
                    try:
                        status = GateStatus(value.get("status"))
                    except ValueError:
                        status = GateStatus.DATA_ERROR
                    plan_errors.append(ErrorRecord(status, str(value.get("message", "计划无效")), value.get("group")))
                else:
                    plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, str(value)))
        if not plan_errors:
            plan_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "计划无效"))
    group_names: list[str] = [
        name for name in GROUP_NAMES
        if parsed_policy is not None and name in parsed_policy.groups
    ]
    if not group_names:
        group_names = list(GROUP_NAMES)
    global_exec_errors, per_group_exec_errors = _execution_errors(execution, group_names)
    errors: list[ErrorRecord] = [*plan_errors, *global_exec_errors]
    cancelled: bool = context.get("cancelled") is True
    if cancelled:
        errors.append(ErrorRecord(GateStatus.CANCELLED, "运行已取消"))

    group_reports: dict[str, Any] = {}
    waived_regressions: list[dict[str, Any]] = []
    for group_name in group_names:
        group_report: dict[str, Any] = {
            "status": GroupStatus.SKIPPED.value,
            "comparison_status": "NOT_RUN",
            "waived": False,
            "steps": {},
            "items": [],
            "regressions": [],
            "new_items": [],
            "missing_items": [],
            "errors": [],
        }
        execution_jobs: Any = execution.get("jobs", {}) if _is_mapping(execution) else {}
        execution_downloads: Any = execution.get("downloads", {}) if _is_mapping(execution) else {}
        group_report["job"] = execution_jobs.get(group_name) if _is_mapping(execution_jobs) else None
        group_report["download"] = execution_downloads.get(group_name) if _is_mapping(execution_downloads) else None
        group_errors: list[ErrorRecord] = list(per_group_exec_errors.get(group_name, []))
        if plan.get("valid") is True and parsed_policy is not None and not cancelled:
            required_steps: Any = required_steps_by_group.get(group_name, [])
            if not isinstance(required_steps, list) or any(not _nonempty_string(step) for step in required_steps):
                group_errors.append(ErrorRecord(GateStatus.DATA_ERROR, "plan.required_steps 无效", group_name))
                required_steps = []
            metadata, files, load_errors = _group_input(group_name, groups.get(group_name), plan, parsed_policy)
            group_errors.extend(load_errors)
            if metadata is not None:
                steps_value: Any = metadata.get("steps")
                if _is_mapping(steps_value):
                    group_report["steps"] = copy.deepcopy(steps_value)
                group_errors.extend(_validate_group_metadata(group_name, metadata, plan, required_steps))
                try:
                    if any(error.status == GateStatus.DATA_ERROR for error in group_errors):
                        raise GateInputError("组身份或输入元数据无效，无法建立有效比较")
                    if group_name == "general":
                        detail, measure_errors = _evaluate_general(
                            group_name, metadata, files, plan, parsed_policy.groups[group_name],
                        )
                    else:
                        detail, measure_errors = _evaluate_sampling(
                            group_name, metadata, files, parsed_policy.groups[group_name],
                        )
                    group_report.update(detail)
                    group_errors.extend(measure_errors)
                except GateExecutionError as error:
                    group_errors.append(ErrorRecord(GateStatus.EXECUTION_ERROR, str(error), group_name))
                except (GateInputError, ValueError, KeyError, TypeError) as error:
                    group_errors.append(ErrorRecord(GateStatus.DATA_ERROR, str(error), group_name))
        group_status: GroupStatus = _status_from_errors(group_errors)
        if not group_errors and group_report.get("status") in (
            GroupStatus.PASS.value, GroupStatus.REGRESSION.value,
        ):
            group_status = GroupStatus(group_report["status"])
        group_report["status"] = group_status.value
        refresh_mode: bool = plan.get("mode") == PlanMode.REFRESH.value
        has_valid_regression: bool = bool(group_report.get("regressions")) and group_report.get(
            "comparison_status",
        ) in ("COMPARED", "CONFIRMED")
        if has_valid_regression and refresh_mode:
            group_report["waived"] = True
            waived_regressions.append({
                "group": group_name,
                "items": list(group_report.get("regressions", [])),
            })
        group_report["errors"] = [_report_error(error) for error in group_errors]
        errors.extend(group_errors)
        group_reports[group_name] = group_report

    any_cancelled: bool = cancelled or any(error.status == GateStatus.CANCELLED for error in errors)
    has_gate_error: bool = any(
        error.status in (
            GateStatus.CONTEXT_ERROR,
            GateStatus.BASELINE_ERROR,
            GateStatus.DATA_ERROR,
            GateStatus.EXECUTION_ERROR,
        )
        for error in errors
    )
    active_regressions: list[dict[str, Any]] = [
        {"group": name, "items": list(group.get("regressions", []))}
        for name, group in group_reports.items()
        if group.get("regressions")
        and group.get("comparison_status") in ("COMPARED", "CONFIRMED")
        and not group.get("waived")
    ]
    if any_cancelled:
        final_status: str = GateStatus.CANCELLED.value
        exit_code: int = 3
    elif has_gate_error:
        final_status = "ERROR"
        exit_code = 2
    elif active_regressions:
        final_status = GroupStatus.REGRESSION.value
        exit_code = 1
    else:
        final_status = GroupStatus.PASS.value
        exit_code = 0

    valid_target: bool = False
    if parsed_policy is not None and _is_mapping(context):
        valid_target = (
            _target_branch(parsed_policy, context)
            and context.get("event") in parsed_policy.baseline.publish_events
            and (context.get("event") == "push" or _dispatch_refresh_allowed(parsed_policy, context))
        )
    publish_baseline: bool = (
        plan.get("valid") is True
        and valid_target
        and not any_cancelled
        and not has_gate_error
        and len(group_reports) == len(GROUP_NAMES)
        and all(
            group.get("status") == GroupStatus.PASS.value
            or (group.get("status") == GroupStatus.REGRESSION.value and group.get("waived") is True)
            for group in group_reports.values()
        )
        and exit_code == 0
    )
    notify_regression: bool = (
        bool(active_regressions)
        and not any_cancelled
        and context.get("fork_pr") is not True
        and context.get("event") in ISSUE_EVENTS
        and not any(error.status == GateStatus.CONTEXT_ERROR for error in errors)
    )
    report: dict[str, Any] = {
        "format_version": FORMAT_VERSION,
        "mode": plan.get("mode", PlanMode.ERROR.value),
        "status": final_status,
        "policy": copy.deepcopy(plan.get("policy", {})),
        "context": copy.deepcopy(context),
        "execution": copy.deepcopy(execution) if _is_mapping(execution) else {},
        "baseline": copy.deepcopy(plan.get("baseline")),
        "sampling_commit": plan.get("sampling_commit"),
        "groups": group_reports,
        "errors": [_report_error(error) for error in errors],
        "waived_regressions": waived_regressions,
        "regressions": active_regressions,
        "publish_baseline": publish_baseline,
        "notify_regression": notify_regression,
        "exit_code": exit_code,
        "report_ready": True,
        "summary_ready": True,
    }
    report["summary"] = _summary(report)
    return report


def _json_file(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _append_github_output(path_value: str | None, values: Mapping[str, Any]) -> None:
    if not path_value:
        return
    path: Path = Path(path_value)
    lines: list[str] = []
    for key, value in values.items():
        if isinstance(value, bool):
            rendered: str = "true" if value else "false"
        elif value is None:
            rendered = ""
        else:
            rendered = str(value)
        if "\n" in rendered or "\r" in rendered:
            raise ValueError(f"GITHUB_OUTPUT 值 {key} 不得包含换行")
        lines.append(f"{key}={rendered}")
    with path.open("a", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def _read_plan_inputs(args: argparse.Namespace) -> tuple[Any, Any, Any, list[ErrorRecord]]:
    values: list[Any] = []
    errors: list[ErrorRecord] = []
    for field, path_value in (("policy", args.policy), ("context", args.context), ("query", args.query)):
        try:
            values.append(_json_file(Path(path_value)))
        except (OSError, json.JSONDecodeError) as error:
            values.append({})
            errors.append(ErrorRecord(GateStatus.DATA_ERROR, f"无法读取 {field} JSON：{error}"))
    return values[0], values[1], values[2], errors


def _fallback_plan(policy: Any, context: Any, errors: Sequence[ErrorRecord]) -> dict[str, Any]:
    return _error_plan(policy, context, errors)


def _plan_command(args: argparse.Namespace) -> int:
    policy, context, query, input_errors = _read_plan_inputs(args)
    plan: dict[str, Any] = (
        _fallback_plan(policy, context, input_errors)
        if input_errors
        else plan_run(policy, context, query)
    )
    _write_json(Path(args.output), plan)
    valid: bool = plan.get("valid") is True
    baseline: Any = plan.get("baseline")
    _append_github_output(args.github_output, {
        "plan_valid": valid,
        "mode": plan.get("mode", PlanMode.ERROR.value),
        "baseline_name": plan.get("policy", {}).get("baseline", {}).get("name", ""),
        "baseline_artifact_id": baseline.get("artifact_id") if _is_mapping(baseline) else "",
        "baseline_run_id": baseline.get("run_id") if _is_mapping(baseline) else "",
        "baseline_commit": baseline.get("commit") if _is_mapping(baseline) else "",
        "sampling_commit": plan.get("sampling_commit") or "",
        "required_groups": ",".join(plan.get("groups", [])),
    })
    return 0


def _load_group_files(groups_dir: Path, plan: Mapping[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    groups_value: Any = plan.get("groups", GROUP_NAMES)
    group_names: list[str] = [name for name in groups_value if name in GROUP_NAMES] if isinstance(groups_value, list) else list(GROUP_NAMES)
    mode: str = str(plan.get("mode", PlanMode.ERROR.value))
    policy: Any = plan.get("policy", {})
    policy_groups: Any = policy.get("groups", {}) if _is_mapping(policy) else {}
    for group_name in group_names:
        group_directory: Path = groups_dir / group_name
        load_errors: list[dict[str, str]] = []
        files: dict[str, Any] = {}
        metadata: Any = None
        try:
            metadata = _json_file(group_directory / "group.json")
        except FileNotFoundError as error:
            load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"group.json 缺失：{error}"})
        except OSError as error:
            load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"无法读取 group.json：{error}"})
        except json.JSONDecodeError as error:
            load_errors.append({"status": GateStatus.DATA_ERROR.value, "message": f"group.json 不是有效 JSON：{error}"})

        group_config: Any = policy_groups.get(group_name, {}) if _is_mapping(policy_groups) else {}
        kind: str = str(group_config.get("kind", "general")) if _is_mapping(group_config) else "general"
        file_names: list[str] = []
        if group_name == "general":
            file_names = ["candidate.json"]
            if mode == PlanMode.COMPARE.value:
                file_names.append("baseline.json")
        elif kind == "sampling":
            file_names = [
                "candidate-initial.json", "baseline-initial.json",
                *[
                    f"rounds/{variant}-{round_number}.json"
                    for variant in ("candidate", "baseline")
                    for round_number in range(1, int(group_config.get("repetitions", 0)) + 1)
                ],
                "confirmation/metadata.json",
            ]
        for file_name in file_names:
            try:
                files[file_name] = _json_file(group_directory / file_name)
            except FileNotFoundError as error:
                load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"{file_name} 缺失：{error}"})
            except OSError as error:
                load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"无法读取 {file_name}：{error}"})
            except json.JSONDecodeError as error:
                load_errors.append({"status": GateStatus.DATA_ERROR.value, "message": f"{file_name} 不是有效 JSON：{error}"})
        if group_name != "general" and _is_mapping(files.get("confirmation/metadata.json")):
            confirmation_cases: Any = files["confirmation/metadata.json"].get("cases")
            confirmation_repetitions: Any = group_config.get("confirmation_repetitions", 0) if _is_mapping(group_config) else 0
            if isinstance(confirmation_cases, list) and confirmation_cases:
                for variant in ("candidate", "baseline"):
                    for round_number in range(1, int(confirmation_repetitions) + 1):
                        file_name = f"confirmation/rounds/{variant}-{round_number}.json"
                        try:
                            files[file_name] = _json_file(group_directory / file_name)
                        except FileNotFoundError as error:
                            load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"{file_name} 缺失：{error}"})
                        except OSError as error:
                            load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"无法读取 {file_name}：{error}"})
                        except json.JSONDecodeError as error:
                            load_errors.append({"status": GateStatus.DATA_ERROR.value, "message": f"{file_name} 不是有效 JSON：{error}"})
                for variant in ("candidate", "baseline"):
                    file_name = f"confirmation/{variant}.json"
                    try:
                        files[file_name] = _json_file(group_directory / file_name)
                    except FileNotFoundError as error:
                        load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"{file_name} 缺失：{error}"})
                    except OSError as error:
                        load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"无法读取 {file_name}：{error}"})
                    except json.JSONDecodeError as error:
                        load_errors.append({"status": GateStatus.DATA_ERROR.value, "message": f"{file_name} 不是有效 JSON：{error}"})
            else:
                for variant in ("candidate", "baseline"):
                    file_name = f"confirmation/{variant}.json"
                    try:
                        files[file_name] = _json_file(group_directory / file_name)
                    except FileNotFoundError as error:
                        load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"{file_name} 缺失：{error}"})
                    except OSError as error:
                        load_errors.append({"status": GateStatus.EXECUTION_ERROR.value, "message": f"无法读取 {file_name}：{error}"})
                    except json.JSONDecodeError as error:
                        load_errors.append({"status": GateStatus.DATA_ERROR.value, "message": f"{file_name} 不是有效 JSON：{error}"})
        result[group_name] = {"metadata": metadata, "files": files, "load_errors": load_errors}
    return result


def _evaluate_command(args: argparse.Namespace) -> int:
    plan: Any = _json_file(Path(args.plan))
    execution: Any = _json_file(Path(args.execution))
    groups: dict[str, Any] = _load_group_files(Path(args.groups_dir), plan)
    report: dict[str, Any] = evaluate_run(plan, execution, groups)
    summary_path: Path = Path(args.summary)
    summary_path.write_text(str(report["summary"]), encoding="utf-8")
    _write_json(Path(args.output), report)
    _append_github_output(args.github_output, {
        "publish_baseline": report.get("publish_baseline") is True,
        "notify_regression": report.get("notify_regression") is True,
        "exit_code": report.get("exit_code", 2),
        "report_ready": report.get("report_ready") is True,
        "summary_ready": report.get("summary_ready") is True,
    })
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    plan_parser: argparse.ArgumentParser = subparsers.add_parser("plan", help="生成运行计划")
    plan_parser.add_argument("--policy", required=True)
    plan_parser.add_argument("--context", required=True)
    plan_parser.add_argument("--query", required=True)
    plan_parser.add_argument("--output", required=True)
    plan_parser.add_argument("--github-output")
    evaluate_parser: argparse.ArgumentParser = subparsers.add_parser("evaluate", help="求值测量组并生成报告")
    evaluate_parser.add_argument("--plan", required=True)
    evaluate_parser.add_argument("--execution", required=True)
    evaluate_parser.add_argument("--groups-dir", required=True)
    evaluate_parser.add_argument("--output", required=True)
    evaluate_parser.add_argument("--summary", default=DEFAULT_SUMMARY_NAME)
    evaluate_parser.add_argument("--github-output")
    arguments: argparse.Namespace = parser.parse_args(argv)
    try:
        if arguments.command == "plan":
            return _plan_command(arguments)
        return _evaluate_command(arguments)
    except (OSError, json.JSONDecodeError, ValueError, TypeError, KeyError) as error:
        print(f"benchmark_gate: 无法生成门禁输出：{error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
