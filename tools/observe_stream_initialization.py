#!/usr/bin/env python3
"""记录流定位基准、独立进程首次调用及可执行文件大小。"""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
from pathlib import Path

ENGINES: tuple[str, ...] = ("xoshiro256ss", "xoroshiro128ss", "xoshiro128ss")
SEED: int = 0x4D595DF4D0F33173
STREAM_ID_BITS: int = 64
HALF_BITS: int = STREAM_ID_BITS // 2
STREAM_IDS: tuple[int, ...] = (0, 1, 3, (1 << HALF_BITS) - 1, 1 << HALF_BITS,
                               1 << (STREAM_ID_BITS - 1), (1 << STREAM_ID_BITS) - 1)
LEGACY_IDS: tuple[int, ...] = (0, 1, 3, 1 << HALF_BITS)
POLICY_PATH: Path = Path(__file__).resolve().with_name("benchmark_policy.json")
TIME_UNITS: frozenset[str] = frozenset(("ns", "us", "ms", "s"))


def is_finite_positive_number(value: object) -> bool:
    """判断 Google Benchmark 报告中的时间值是否有效。"""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    try:
        return math.isfinite(value) and value > 0
    except OverflowError:
        return False


def validate_steady_report(report: dict, repetitions: int) -> None:
    """要求三种引擎的全部观察项都有预定轮数的中位数记录。"""
    expected: set[str] = {
        f"BM_StreamInitialization/{engine}/{case}"
        for engine in ENGINES
        for case in (*[f"locate/{index}" for index in range(len(STREAM_IDS))],
                     *[f"legacy_small_id/{STREAM_IDS.index(stream_id)}" for stream_id in LEGACY_IDS],
                     "sequential_by_jump", "seed_construction")
    }
    medians: list[dict] = [record for record in report["benchmarks"]
                          if record.get("aggregate_name") == "median"]
    actual: set[str] = {record["run_name"] for record in medians}
    if actual != expected or len(medians) != len(expected):
        raise ValueError("流定位基准观察项不完整或重复")
    for record in medians:
        if (record.get("run_type") != "aggregate"
                or record.get("aggregate_unit") != "time"
                or record.get("error_occurred", False) is not False
                or record.get("time_unit") not in TIME_UNITS):
            raise ValueError(f"基准聚合记录格式或状态无效：{record['run_name']}")
        if (record.get("repetitions") != repetitions
                or not is_finite_positive_number(record.get("real_time"))
                or not is_finite_positive_number(record.get("cpu_time"))):
            raise ValueError(f"基准轮数或计时不符合观察协议：{record['run_name']}")


def validate_first_call_observations(observations: list[dict], repetitions: int) -> None:
    """核对首次调用记录与全部预定引擎、模式、编号及轮次一致。"""
    expected: set[tuple[str, bool, str, int]] = {
        (engine, legacy, str(stream_id), repetition)
        for engine in ENGINES
        for legacy, stream_ids in ((False, STREAM_IDS), (True, LEGACY_IDS))
        for stream_id in stream_ids
        for repetition in range(repetitions)
    }
    actual: set[tuple[str, bool, str, int]] = set()
    for observation in observations:
        if not isinstance(observation, dict):
            raise ValueError("首次调用观察记录必须是对象")
        engine: object = observation.get("engine")
        legacy: object = observation.get("legacy")
        stream_id: object = observation.get("stream_id")
        elapsed_ns: object = observation.get("elapsed_ns")
        first_output: object = observation.get("first_output")
        repetition: object = observation.get("repetition")
        if (not isinstance(engine, str) or engine not in ENGINES
                or not isinstance(legacy, bool)
                or not isinstance(stream_id, str)
                or not isinstance(repetition, int) or isinstance(repetition, bool)
                or repetition < 0 or repetition >= repetitions):
            raise ValueError("首次调用观察记录与请求参数不符")
        if (not isinstance(elapsed_ns, int) or isinstance(elapsed_ns, bool) or elapsed_ns < 0
                or not isinstance(first_output, str)
                or not first_output.isascii() or not first_output.isdecimal()):
            raise ValueError("首次调用观察记录的结果或计时无效")
        key: tuple[str, bool, str, int] = (engine, legacy, stream_id, repetition)
        if key not in expected or key in actual:
            raise ValueError("首次调用观察项不属于预定集合或重复")
        actual.add(key)
    if actual != expected or len(observations) != len(expected):
        raise ValueError("首次调用观察项不完整或重复")


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args: argparse.Namespace = parser.parse_args()
    binary: Path = args.binary.resolve()
    output_dir: Path = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    policy: dict = json.loads(POLICY_PATH.read_text(encoding="utf-8"))["groups"]["general"]
    subprocess.run(
        [str(binary), f"--benchmark_min_time={policy['min_time']}",
         f"--benchmark_repetitions={policy['repetitions']}", "--benchmark_out_format=json",
         f"--benchmark_out={output_dir / 'steady.json'}"], check=True,
    )
    steady_path: Path = output_dir / "steady.json"
    steady: dict = json.loads(steady_path.read_text(encoding="mbcs" if os.name == "nt" else "utf-8"))
    validate_steady_report(steady, int(policy["repetitions"]))
    steady_path.write_text(json.dumps(steady, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    observations: list[dict] = []
    for engine in ENGINES:
        for legacy, stream_ids in ((False, STREAM_IDS), (True, LEGACY_IDS)):
            option: str = "--randx-first-stream-legacy" if legacy else "--randx-first-stream"
            for stream_id in stream_ids:
                for repetition in range(int(policy["repetitions"])):
                    result: subprocess.CompletedProcess[str] = subprocess.run(
                        [str(binary), option, engine, str(stream_id), str(SEED)],
                        check=True, capture_output=True, text=True,
                    )
                    observation: object = json.loads(result.stdout)
                    if not isinstance(observation, dict):
                        raise ValueError("首次调用观察结果必须是 JSON 对象")
                    if (observation.get("engine") != engine
                            or observation.get("legacy") is not legacy
                            or observation.get("stream_id") != str(stream_id)):
                        raise ValueError("首次调用观察结果与本次请求参数不符")
                    observation["repetition"] = repetition
                    observations.append(observation)
    validate_first_call_observations(observations, int(policy["repetitions"]))
    report: dict = {
        "binary": str(binary), "binary_bytes": binary.stat().st_size,
        "protocol": policy, "seed": SEED,
        "measurement_scope": "每次启动新进程，仅计时首次流构造；包含计时器开销，不包含进程启动，不保证 OS 页面缓存冷态",
        "first_calls": observations,
    }
    (output_dir / "first-calls.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
