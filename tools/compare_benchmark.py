#!/usr/bin/env python3
"""对比两次 Google Benchmark JSON 输出，检测性能回归。

用法：compare_benchmark.py current.json baseline.json --tolerance 0.25 [--allow-missing]
退出码：
  0 = 无回归且基线项均成功比对
  1 = 存在超容差回归
  2 = 输入无效（文件错误、测试执行报错、测量值无效或基线测试项缺失）

注：配合 --benchmark_report_aggregates_only=true 使用，
    仅对比 median 聚合值，避免冗长输出。
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


TIME_UNIT_TO_MS: dict[str, float] = {"ns": 1e-6, "us": 1e-3, "ms": 1.0, "s": 1000.0}
DEFAULT_TOLERANCE: float = 0.25


class BenchmarkDataError(ValueError):
    """Benchmark 输入或比较条件不符合数据契约。"""


@dataclass(frozen=True)
class ComparisonItem:
    name: str
    baseline_ms: float
    current_ms: float
    change: float
    regression: bool


@dataclass(frozen=True)
class ComparisonResult:
    items: list[ComparisonItem]
    regressions: list[str]
    new_items: list[str]
    missing_items: list[str]


def benchmark_name(entry: dict[str, Any]) -> str:
    """返回实测名称，聚合记录优先采用 run_name。"""
    run_name: Any = entry.get("run_name")
    if run_name is not None:
        if not isinstance(run_name, str) or not run_name:
            raise BenchmarkDataError("benchmark run_name 必须为非空字符串")
        return run_name

    name: Any = entry.get("name")
    if not isinstance(name, str) or not name:
        raise BenchmarkDataError("benchmark name 必须为非空字符串")
    return name.removesuffix("_median")


def parse_results(data: dict[str, Any], source: str = "<memory>") -> dict[str, dict[str, Any]]:
    """验证 Google Benchmark JSON 数据并返回按名称索引的 median 记录。"""
    if not isinstance(data, dict) or not isinstance(data.get("benchmarks"), list):
        raise BenchmarkDataError(
            f"{source} 缺少 'benchmarks' 列表字段（非有效 Google Benchmark 输出）"
        )

    errors: list[str] = []
    median_entries: dict[str, dict[str, Any]] = {}

    for index, entry in enumerate(data["benchmarks"]):
        if not isinstance(entry, dict):
            errors.append(f"第 {index + 1} 条记录不是对象")
            continue

        display_name: Any = entry.get("name", "<unknown>")
        if entry.get("error_occurred", False) or entry.get("error_message"):
            message: Any = entry.get("error_message") or "未提供错误信息"
            errors.append(f"{display_name}: {message}")
            continue

        if entry.get("aggregate_name") != "median":
            continue

        try:
            name: str = benchmark_name(entry)
        except BenchmarkDataError as error:
            errors.append(f"{display_name}: {error}")
            continue

        if name in median_entries:
            errors.append(f"{name}: 存在重复 median 聚合记录")
            continue

        cpu_time: Any = entry.get("cpu_time")
        if isinstance(cpu_time, bool) or not isinstance(cpu_time, (int, float)):
            errors.append(f"{display_name}: 缺少或非法的 cpu_time")
            continue
        if not math.isfinite(cpu_time) or cpu_time < 0.0:
            errors.append(f"{display_name}: cpu_time 测量值无效 ({cpu_time})，必须为非负有限实数")
            continue

        time_unit: Any = entry.get("time_unit")
        if not isinstance(time_unit, str) or time_unit not in TIME_UNIT_TO_MS:
            errors.append(f"{display_name}: 未知或非法的 time_unit '{time_unit}'")
            continue

        median_entries[name] = entry

    if errors:
        details: str = "\n".join(f"  - {error}" for error in errors)
        raise BenchmarkDataError(f"{source} 中存在测试失败或无效测量记录：\n{details}")

    return median_entries


def load_results(path: str | Path) -> dict[str, dict[str, Any]]:
    """加载并验证 Google Benchmark JSON 文件。"""
    source: str = str(path)
    try:
        with open(path, encoding="utf-8") as result_file:
            data: Any = json.load(result_file)
    except OSError as error:
        raise BenchmarkDataError(f"无法读取文件 {source}: {error}") from error
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise BenchmarkDataError(f"{source} 不是有效 JSON: {error}") from error

    return parse_results(data, source)


def normalize_ms(value: float, unit: str) -> float:
    """将合法的 benchmark 时间转换为毫秒。"""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BenchmarkDataError("cpu_time 必须为非负有限实数")
    if not math.isfinite(value) or value < 0.0:
        raise BenchmarkDataError("cpu_time 必须为非负有限实数")
    if not isinstance(unit, str) or unit not in TIME_UNIT_TO_MS:
        raise BenchmarkDataError(f"未知 time_unit '{unit}'，请检查输入文件")

    normalized: float = value * TIME_UNIT_TO_MS[unit]
    if not math.isfinite(normalized):
        raise BenchmarkDataError("归一化后的 cpu_time 必须为有限数")
    return normalized


def compare_results(
    current: dict[str, dict[str, Any]],
    baseline: dict[str, dict[str, Any]],
    tolerance: float,
    allow_missing: bool = False,
) -> ComparisonResult:
    """比较当前结果与基线，严格按变化率是否大于容差标记回归。"""
    if not math.isfinite(tolerance) or tolerance < 0.0:
        raise BenchmarkDataError(f"容差必须为非负有限数，当前值: {tolerance}")
    if not current or not baseline:
        raise BenchmarkDataError(
            "current 或 baseline 缺少有效 median 聚合条目，无法对比"
            "（请确认使用了 --benchmark_report_aggregates_only=true）"
        )

    missing_items: list[str] = [name for name in baseline if name not in current]
    if missing_items and not allow_missing:
        names: str = "\n".join(f"  - {name}" for name in missing_items)
        raise BenchmarkDataError(
            f"基线中存在的 {len(missing_items)} 个测试项在当前结果中缺失：\n{names}\n"
            "若为计划内的测试项移除或重命名，请使用 --allow-missing 选项。"
        )

    common: list[str] = [name for name in baseline if name in current]
    if not common:
        raise BenchmarkDataError(
            "current 与 baseline 无公共 benchmark 名称，"
            "可能是基准改名或参数变化导致，请检查输入"
        )

    items: list[ComparisonItem] = []
    regressions: list[str] = []
    for name in common:
        baseline_entry: dict[str, Any] = baseline[name]
        current_entry: dict[str, Any] = current[name]
        baseline_ms: float = normalize_ms(baseline_entry["cpu_time"], baseline_entry["time_unit"])
        current_ms: float = normalize_ms(current_entry["cpu_time"], current_entry["time_unit"])
        if baseline_ms > 0.0:
            change: float = (current_ms - baseline_ms) / baseline_ms
        else:
            change = 0.0 if current_ms == 0.0 else 1.0

        if not math.isfinite(change):
            raise BenchmarkDataError(f"测试项 {name} 计算的变化量非有限数")

        regression: bool = change > tolerance
        items.append(ComparisonItem(name, baseline_ms, current_ms, change, regression))
        if regression:
            regressions.append(name)

    new_items: list[str] = [name for name in current if name not in baseline]
    return ComparisonResult(items, regressions, new_items, missing_items)


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description="对比 benchmark JSON 结果")
    parser.add_argument("current", help="当前运行 JSON")
    parser.add_argument("baseline", help="基线 JSON")
    parser.add_argument(
        "--tolerance", type=float, default=DEFAULT_TOLERANCE,
        help=f"回归容差（0.25 = 25%%），默认 {DEFAULT_TOLERANCE}",
    )
    parser.add_argument(
        "--allow-missing", action="store_true",
        help="允许基线中的测试项在当前结果中缺失（用于计划内的重命名或删除）",
    )
    arguments: argparse.Namespace = parser.parse_args()

    if not math.isfinite(arguments.tolerance) or arguments.tolerance < 0.0:
        print(
            f"错误：--tolerance 必须为非负有限数，当前值: {arguments.tolerance}",
            file=sys.stderr,
        )
        return 2

    try:
        comparison: ComparisonResult = compare_results(
            load_results(arguments.current),
            load_results(arguments.baseline),
            arguments.tolerance,
            arguments.allow_missing,
        )
    except BenchmarkDataError as error:
        print(f"错误：{error}", file=sys.stderr)
        return 2

    print("| Benchmark | Baseline (ms) | Current (ms) | Change | Status |")
    print("|-----------|---------------|--------------|--------|--------|")
    for item in comparison.items:
        status: str = "REGRESSION" if item.regression else "OK"
        print(
            f"| {item.name} | {item.baseline_ms:.4f} | {item.current_ms:.4f} "
            f"| {item.change:+.1%} | {status} |"
        )

    if comparison.new_items:
        print(f"\n跳过 {len(comparison.new_items)} 个新增项（无基线）：")
        for item in comparison.new_items:
            print(f"  + {item}")

    if comparison.missing_items:
        print(f"\n允许跳过 {len(comparison.missing_items)} 个已移除的基线项：")
        for item in comparison.missing_items:
            print(f"  - {item}")

    if comparison.regressions:
        print(f"\n回归项（超 {arguments.tolerance:.0%} 容差）：{len(comparison.regressions)} 个")
        for regression in comparison.regressions:
            print(f"  - {regression}")
        return 1

    print(f"\n无回归（容差 {arguments.tolerance:.0%}）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
