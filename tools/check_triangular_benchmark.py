"""核对三角分布基准场景、重复次数与实际随机消耗。"""
from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any

from compare_benchmark import BenchmarkDataError, benchmark_name, parse_results

SCENARIOS = ("symmetric", "left_skewed", "right_skewed", "minimum_endpoint", "maximum_endpoint", "degenerate")
EXPECTED_CALLS = {
    f"BM_RandTriangular/{engine}/{floating}/{scenario}":
        0 if scenario == "degenerate" else (2 if engine == "engine32" and floating == "double" else 1)
    for engine in ("engine32", "engine64")
    for floating in ("float", "double")
    for scenario in SCENARIOS
}


def check_results(data: dict[str, Any], repetitions: int) -> None:
    medians = parse_results(data)
    if repetitions <= 0 or medians.keys() != EXPECTED_CALLS.keys():
        raise BenchmarkDataError("须包含全部三角分布场景及正数重复次数")
    counts: Counter[str] = Counter()
    indices: dict[str, set[int]] = {name: set() for name in EXPECTED_CALLS}
    for entry in data["benchmarks"]:
        if entry.get("run_type") != "iteration" and entry.get("aggregate_name") != "median":
            continue
        name = benchmark_name(entry)
        if name not in EXPECTED_CALLS or entry.get("engine_calls_per_sample") != EXPECTED_CALLS[name]:
            raise BenchmarkDataError(f"引擎消耗与场景契约不符：{name}")
        if entry.get("run_type") == "iteration":
            counts[name] += 1
            indices[name].add(entry["repetition_index"])
    for name in EXPECTED_CALLS:
        if counts[name] != repetitions or indices[name] != set(range(repetitions)):
            raise BenchmarkDataError(f"重复测量集合不完整：{name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--repetitions", type=int, required=True)
    args = parser.parse_args()
    try:
        check_results(json.loads(args.input.read_text(encoding="utf-8")), args.repetitions)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"三角分布基准验证失败：{error}")
        return 1
    print(f"三角分布 {len(EXPECTED_CALLS)} 个场景、各 {args.repetitions} 次测量及引擎消耗通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
