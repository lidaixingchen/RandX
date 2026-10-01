"""对抽样性能初测疑点执行一次固定的双边延长测量，保留完整门禁项。"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Sequence

from compare_benchmark import load_results, normalize_ms
from merge_benchmark_repetitions import merge_results

DEFAULT_REPETITIONS: int = 6
DEFAULT_MIN_TIME: str = "1s"
DEFAULT_TOLERANCE: float = 0.25
CASE_PATTERN: re.Pattern[str] = re.compile(
    r"(?P<family>.+)/range_size:(?P<size>\d+)/request:(?P<request>\d+)"
)


def case_name(entry: dict[str, Any]) -> str:
    return entry.get("run_name", entry["name"].removesuffix("_median"))


def select_confirmation_cases(
    baseline: dict[str, dict], candidate: dict[str, dict], tolerance: float,
) -> list[str]:
    if not baseline or baseline.keys() != candidate.keys():
        raise ValueError("初测双边完整测量项集合须非空且一致")
    cases: dict[str, tuple[str, int, int]] = {}
    failed: list[str] = []
    for name, entry in baseline.items():
        case: str = case_name(entry)
        match: re.Match[str] | None = CASE_PATTERN.fullmatch(case)
        if match is None:
            raise ValueError(f"抽样参数名称不可解析：{case}")
        cases[case] = (match["family"], int(match["size"]), int(match["request"]))
        base_time: float = normalize_ms(entry["cpu_time"], entry["time_unit"])
        current_time: float = normalize_ms(candidate[name]["cpu_time"], candidate[name]["time_unit"])
        if current_time > base_time * (1 + tolerance):
            failed.append(case)
    selected: set[str] = set(failed)
    for case in failed:
        family, size, request = cases[case]
        requests: list[tuple[int, str]] = sorted(
            (values[2], name) for name, values in cases.items() if values[:2] == (family, size)
        )
        index: int = next(index for index, item in enumerate(requests) if item[0] == request)
        selected.update(name for _, name in requests[max(0, index - 1):index + 2])
        if size == 0:
            next_size: int = min(values[1] for values in cases.values() if values[0] == family and values[1] > size)
            selected.update(name for name, values in cases.items() if values[:2] == (family, next_size))
    return sorted(selected)


def replace_measurements(initial: dict[str, Any], confirmed: dict[str, Any], cases: Sequence[str]) -> dict[str, Any]:
    selected: set[str] = set(cases)
    measured: set[str] = {case_name(entry) for entry in confirmed["benchmarks"]}
    if measured != selected:
        raise ValueError("确认测量项须与已选定的疑点及邻接项完全一致")
    return {
        **initial,
        "benchmarks": [
            *(entry for entry in initial["benchmarks"] if case_name(entry) not in selected),
            *confirmed["benchmarks"],
        ],
    }


def confirm(
    baseline_path: Path, candidate_path: Path, baseline_binary: Path, candidate_binary: Path,
    output_dir: Path, repetitions: int, min_time: str, tolerance: float, cpu: int | None,
    result_encoding: str = "utf-8",
) -> None:
    if repetitions <= 0 or repetitions % 2 != 0:
        raise ValueError("确认轮数须为正偶数，以使双边先后顺序均衡")
    if not math.isfinite(tolerance) or tolerance < 0:
        raise ValueError("容差须为非负有限数")
    if not min_time.endswith("s") or not math.isfinite(float(min_time[:-1])) or float(min_time[:-1]) <= 0:
        raise ValueError("确认时间须为正秒数")
    cases: list[str] = select_confirmation_cases(load_results(str(baseline_path)), load_results(str(candidate_path)), tolerance)
    output_dir.mkdir(parents=True, exist_ok=True)
    initial: dict[str, dict[str, Any]] = {
        "baseline": json.loads(baseline_path.read_text(encoding="utf-8")),
        "candidate": json.loads(candidate_path.read_text(encoding="utf-8")),
    }
    metadata: dict[str, Any] = {
        "initial": {"baseline": str(baseline_path.resolve()), "candidate": str(candidate_path.resolve())},
        "cases": cases, "repetitions": repetitions, "min_time": min_time,
        "tolerance": tolerance, "cpu": cpu, "result_encoding": result_encoding,
        "order": "balanced alternating pairs", "runs": [],
    }
    rounds: dict[str, list[dict[str, Any]]] = {"baseline": [], "candidate": []}
    binaries: dict[str, Path] = {"baseline": baseline_binary.resolve(), "candidate": candidate_binary.resolve()}
    try:
        if cases:
            case_filter: str = "^(" + "|".join(re.escape(case) for case in cases) + ")$"
            for round_index in range(repetitions):
                variants: tuple[str, str] = ("baseline", "candidate") if round_index % 2 == 0 else ("candidate", "baseline")
                for variant in variants:
                    prefix: Path = output_dir / f"{variant}-{round_index + 1}"
                    command: list[str] = [
                        str(binaries[variant]), f"--benchmark_filter={case_filter}",
                        "--benchmark_format=json", f"--benchmark_out={prefix.resolve()}.json",
                        "--benchmark_repetitions=1", f"--benchmark_min_time={min_time}",
                    ]
                    if cpu is not None:
                        command = ["taskset", "-c", str(cpu), *command]
                    run: dict[str, Any] = {"variant": variant, "round": round_index + 1, "command": command}
                    metadata["runs"].append(run)
                    process: subprocess.CompletedProcess[bytes] = subprocess.run(command, capture_output=True, check=False)
                    run["returncode"] = process.returncode
                    prefix.with_suffix(".stdout.txt").write_bytes(process.stdout)
                    prefix.with_suffix(".stderr.txt").write_bytes(process.stderr)
                    if process.returncode != 0:
                        raise ValueError(f"确认进程失败：{variant} 第 {round_index + 1} 轮，退出码 {process.returncode}")
                    rounds[variant].append(json.loads(prefix.with_suffix(".json").read_text(encoding=result_encoding)))
        for variant, result in initial.items():
            accepted: dict[str, Any] = replace_measurements(result, merge_results(rounds[variant]), cases) if cases else result
            (output_dir / f"{variant}.json").write_text(json.dumps(accepted, ensure_ascii=False, indent=2), encoding="utf-8")
        metadata["complete"] = True
        print(f"抽样确认完成：固定确认 {len(cases)} 项，完整测量项保留。")
    finally:
        (output_dir / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--baseline-binary", required=True, type=Path)
    parser.add_argument("--candidate-binary", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--repetitions", type=int, default=DEFAULT_REPETITIONS)
    parser.add_argument("--min-time", default=DEFAULT_MIN_TIME)
    parser.add_argument("--tolerance", type=float, default=DEFAULT_TOLERANCE)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--result-encoding", default="utf-8", help="基准进程原始 JSON 的编码")
    arguments: argparse.Namespace = parser.parse_args()
    try:
        confirm(arguments.baseline, arguments.candidate, arguments.baseline_binary, arguments.candidate_binary,
                arguments.output_dir, arguments.repetitions, arguments.min_time, arguments.tolerance, arguments.cpu,
                arguments.result_encoding)
    except (OSError, ValueError) as error:
        print(f"抽样确认失败：{error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
