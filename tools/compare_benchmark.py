#!/usr/bin/env python3
"""对比两次 Google Benchmark JSON 输出，检测性能回归。

用法：compare_benchmark.py current.json baseline.json --tolerance 0.15
退出码：0 = 无回归（或新增项），1 = 存在超容差回归，2 = 输入文件错误

注：配合 --benchmark_report_aggregates_only=true 使用，
    仅对比 median 聚合值（name 以 "/median" 结尾），避免冗长输出。
"""
import argparse
import json
import sys


def load_results(path: str) -> dict:
    """加载 Google Benchmark JSON，返回 {name: entry} 字典（仅 median 聚合）。

    失败时打印友好错误并退出（退出码 2），避免 CI 日志出现原始 traceback。
    """
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except OSError as e:
        print(f"错误：无法读取文件 {path}: {e}", file=sys.stderr)
        sys.exit(2)
    except json.JSONDecodeError as e:
        print(f"错误：{path} 不是有效 JSON: {e}", file=sys.stderr)
        sys.exit(2)
    if "benchmarks" not in data:
        print(f"错误：{path} 缺少 'benchmarks' 字段（非 Google Benchmark 输出？）",
              file=sys.stderr)
        sys.exit(2)
    # 仅保留 median 聚合条目
    return {
        b["name"]: b
        for b in data["benchmarks"]
        if b.get("aggregate_name") == "median"
    }


def normalize_ms(value: float, unit: str) -> float:
    """将 cpu_time 归一化到毫秒。单位未知时按 ms 处理并打印警告。"""
    factors = {"ns": 1e-6, "us": 1e-3, "ms": 1.0, "s": 1000.0}
    if unit not in factors:
        print(f"警告：未知 time_unit '{unit}'，按 ms 处理", file=sys.stderr)
        return value
    return value * factors[unit]


def main() -> None:
    parser = argparse.ArgumentParser(description="对比 benchmark JSON 结果")
    parser.add_argument("current", help="当前运行 JSON")
    parser.add_argument("baseline", help="基线 JSON")
    parser.add_argument("--tolerance", type=float, default=0.15,
                        help="回归容差（0.15 = 15%%），默认 0.15")
    args = parser.parse_args()

    current = load_results(args.current)
    baseline = load_results(args.baseline)

    if not current or not baseline:
        print("错误：current/baseline 缺少 median 聚合条目，无法对比"
              "（请确认使用了 --benchmark_report_aggregates_only=true）",
              file=sys.stderr)
        sys.exit(2)

    regressions: list[str] = []
    skipped = 0
    print("| Benchmark | Baseline (ms) | Current (ms) | Change | Status |")
    print("|-----------|---------------|--------------|--------|--------|")

    for name, cur in current.items():
        if name not in baseline:
            skipped += 1
            continue
        base = baseline[name]
        base_ms = normalize_ms(base["cpu_time"], base["time_unit"])
        cur_ms = normalize_ms(cur["cpu_time"], cur["time_unit"])
        change = (cur_ms - base_ms) / base_ms if base_ms > 0 else (0.0 if cur_ms == 0 else 1.0)
        status = "REGRESSION" if change > args.tolerance else "OK"
        if change > args.tolerance:
            regressions.append(name)
        print(f"| {name} | {base_ms:.4f} | {cur_ms:.4f} | {change:+.1%} | {status} |")

    if skipped:
        print(f"\n跳过 {skipped} 个新增项（无基线）")

    if regressions:
        print(f"\n回归项（超 {args.tolerance:.0%} 容差）：{len(regressions)} 个")
        for r in regressions:
            print(f"  - {r}")
        sys.exit(1)
    print(f"\n无回归（容差 {args.tolerance:.0%}）")


if __name__ == "__main__":
    main()
