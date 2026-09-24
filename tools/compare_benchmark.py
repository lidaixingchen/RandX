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
import argparse
import json
import math
import sys


def load_results(path: str) -> dict[str, dict]:
    """加载 Google Benchmark JSON，校验完整性并返回 {name: entry} 字典（仅 median 聚合）。

    若存在测试项执行失败（error_occurred 为 True 或包含 error_message），
    或测量值无效（NaN、无穷大、负耗时），打印错误并以退出码 2 退出。
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

    if not isinstance(data, dict) or "benchmarks" not in data or not isinstance(data["benchmarks"], list):
        print(f"错误：{path} 缺少 'benchmarks' 列表字段（非有效 Google Benchmark 输出）", file=sys.stderr)
        sys.exit(2)

    errors: list[str] = []
    median_entries: dict[str, dict] = {}

    for b in data["benchmarks"]:
        if not isinstance(b, dict):
            continue
        name = b.get("name", "<unknown>")
        if b.get("error_occurred", False) or b.get("error_message"):
            msg = b.get("error_message") or "未提供错误信息"
            errors.append(f"{name}: {msg}")
            continue

        if b.get("aggregate_name") == "median":
            cpu_time = b.get("cpu_time")
            if cpu_time is None or not isinstance(cpu_time, (int, float)):
                errors.append(f"{name}: 缺少或非法的 cpu_time")
                continue
            if not math.isfinite(cpu_time) or cpu_time < 0.0:
                errors.append(f"{name}: cpu_time 测量值无效 ({cpu_time})，必须为非负有限实数")
                continue

            time_unit = b.get("time_unit")
            if time_unit not in ("ns", "us", "ms", "s"):
                errors.append(f"{name}: 未知或非法的 time_unit '{time_unit}'")
                continue

            median_entries[name] = b

    if errors:
        print(f"错误：{path} 中存在测试失败或无效测量记录：", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        sys.exit(2)

    return median_entries


def normalize_ms(value: float, unit: str) -> float:
    """将 cpu_time 归一化到毫秒。单位未知时报错误并退出（退出码 2）。"""
    factors = {"ns": 1e-6, "us": 1e-3, "ms": 1.0, "s": 1000.0}
    if unit not in factors:
        print(f"错误：未知 time_unit '{unit}'，请检查输入文件", file=sys.stderr)
        sys.exit(2)
    return value * factors[unit]


def main() -> None:
    parser = argparse.ArgumentParser(description="对比 benchmark JSON 结果")
    parser.add_argument("current", help="当前运行 JSON")
    parser.add_argument("baseline", help="基线 JSON")
    parser.add_argument("--tolerance", type=float, default=0.25,
                        help="回归容差（0.25 = 25%%），默认 0.25")
    parser.add_argument("--allow-missing", action="store_true",
                        help="允许基线中的测试项在当前结果中缺失（用于计划内的重命名或删除）")
    args = parser.parse_args()

    if not math.isfinite(args.tolerance) or args.tolerance < 0.0:
        print(f"错误：--tolerance 必须为非负有限数，当前值: {args.tolerance}", file=sys.stderr)
        sys.exit(2)

    current = load_results(args.current)
    baseline = load_results(args.baseline)

    if not current or not baseline:
        print("错误：current 或 baseline 缺少有效 median 聚合条目，无法对比"
              "（请确认使用了 --benchmark_report_aggregates_only=true）",
              file=sys.stderr)
        sys.exit(2)

    missing_in_current = [name for name in baseline if name not in current]
    if missing_in_current and not args.allow_missing:
        print(f"错误：基线中存在的 {len(missing_in_current)} 个测试项在当前结果中缺失：", file=sys.stderr)
        for m in missing_in_current:
            print(f"  - {m}", file=sys.stderr)
        print("若为计划内的测试项移除或重命名，请使用 --allow-missing 选项。", file=sys.stderr)
        sys.exit(2)

    common = [name for name in baseline if name in current]
    if not common:
        print("错误：current 与 baseline 无公共 benchmark 名称，"
              "可能是基准改名或参数变化导致，请检查输入",
              file=sys.stderr)
        sys.exit(2)

    regressions: list[str] = []
    new_items: list[str] = [name for name in current if name not in baseline]

    print("| Benchmark | Baseline (ms) | Current (ms) | Change | Status |")
    print("|-----------|---------------|--------------|--------|--------|")

    for name in common:
        base = baseline[name]
        cur = current[name]
        base_ms = normalize_ms(base["cpu_time"], base["time_unit"])
        cur_ms = normalize_ms(cur["cpu_time"], cur["time_unit"])

        if base_ms > 0:
            change = (cur_ms - base_ms) / base_ms
        else:
            change = 0.0 if cur_ms == 0 else 1.0

        if not math.isfinite(change):
            print(f"错误：测试项 {name} 计算的变化量非有限数", file=sys.stderr)
            sys.exit(2)

        status = "REGRESSION" if change > args.tolerance else "OK"
        if change > args.tolerance:
            regressions.append(name)
        print(f"| {name} | {base_ms:.4f} | {cur_ms:.4f} | {change:+.1%} | {status} |")

    if new_items:
        print(f"\n跳过 {len(new_items)} 个新增项（无基线）：")
        for item in new_items:
            print(f"  + {item}")

    if missing_in_current:
        print(f"\n允许跳过 {len(missing_in_current)} 个已移除的基线项：")
        for item in missing_in_current:
            print(f"  - {item}")

    if regressions:
        print(f"\n回归项（超 {args.tolerance:.0%} 容差）：{len(regressions)} 个")
        for r in regressions:
            print(f"  - {r}")
        sys.exit(1)

    print(f"\n无回归（容差 {args.tolerance:.0%}）")


if __name__ == "__main__":
    main()
