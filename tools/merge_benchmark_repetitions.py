"""合并交替执行的 Google Benchmark 单轮结果，保留原始记录并生成中位数。"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any, Sequence

TIME_FIELDS: tuple[str, ...] = ("cpu_time", "real_time", "items_per_second", "bytes_per_second")


def merge_results(results: Sequence[dict[str, Any]]) -> dict[str, Any]:
    rounds: list[dict[str, dict[str, Any]]] = []
    raw: list[dict[str, Any]] = []
    for round_index, result in enumerate(results):
        entries: dict[str, dict[str, Any]] = {}
        for entry in result["benchmarks"]:
            if entry.get("error_occurred") or entry.get("error_message"):
                raise ValueError(f"测量失败：{entry['name']}")
            if entry.get("run_type", "iteration") != "iteration":
                continue
            name: str = entry["name"]
            for field in ("cpu_time", "real_time"):
                value: Any = entry[field]
                if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                    raise ValueError(f"测量时间须为非负有限数：{name} {field}")
            if name in entries:
                raise ValueError(f"单轮包含重复测量：{name}")
            entries[name] = entry
            raw.append({**entry, "repetition_index": round_index, "repetitions": len(results)})
        if not entries or (rounds and entries.keys() != rounds[0].keys()):
            raise ValueError("各轮测量项集合须非空且一致")
        rounds.append(entries)
    if not rounds:
        raise ValueError("至少需要一轮测量")
    medians: list[dict[str, Any]] = []
    for name, first in rounds[0].items():
        samples: list[dict[str, Any]] = [entries[name] for entries in rounds]
        if any(entry["time_unit"] != first["time_unit"] for entry in samples):
            raise ValueError(f"各轮时间单位须一致：{name}")
        median: dict[str, Any] = {
            **first, "name": f"{name}_median", "run_name": name, "run_type": "aggregate",
            "aggregate_name": "median", "aggregate_unit": "time",
            "repetitions": len(results), "iterations": len(results),
        }
        median.pop("repetition_index", None)
        for field in TIME_FIELDS:
            if field in first:
                median[field] = statistics.median(entry[field] for entry in samples)
        medians.append(median)
    return {"context": results[0]["context"], "benchmarks": [*raw, *medians]}


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments: argparse.Namespace = parser.parse_args()
    results: list[dict[str, Any]] = [json.loads(path.read_text(encoding="utf-8")) for path in arguments.inputs]
    combined: dict[str, Any] = merge_results(results)
    arguments.output.write_text(json.dumps(combined, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
