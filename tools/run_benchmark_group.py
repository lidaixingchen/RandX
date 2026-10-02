"""在单一 runner 上构建并测量一组 benchmark 的候选与基线。"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shlex
import subprocess
import sys
from pathlib import Path
from shutil import copy2
from typing import Any, Mapping, Sequence

from compare_benchmark import benchmark_name, parse_results


GROUP_NAMES: tuple[str, ...] = ("general", "sampling_cpp17", "sampling_cpp23")
SAMPLING_GROUPS: frozenset[str] = frozenset(("sampling_cpp17", "sampling_cpp23"))
VARIANT_NAMES: tuple[str, ...] = ("candidate", "baseline")
RUN_NAME_FLAG: str = "--benchmark_list_tests=true"
RUN_FORMAT_FLAG: str = "--benchmark_format=json"
REPORT_AGGREGATES_FLAG: str = "--benchmark_report_aggregates_only=true"


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


def policy_group(plan: Mapping[str, Any], group: str) -> dict[str, Any]:
    """从冻结的门禁计划读取比较组参数。"""
    policy: Any = plan.get("policy")
    groups: Any = policy.get("groups") if isinstance(policy, dict) else None
    configuration: Any = groups.get(group) if isinstance(groups, dict) else None
    if not isinstance(configuration, dict):
        raise ValueError(f"计划策略缺少 {group} 组配置")
    return configuration


def workspace_root() -> Path:
    """返回当前 Actions 工作树根目录。"""
    value: str | None = os.environ.get("GITHUB_WORKSPACE")
    return Path(value).resolve() if value else Path.cwd().resolve()


def group_directory(root: Path, group: str) -> Path:
    """返回比较组产物目录。"""
    return root / "groups" / group


def run_logged(command: Sequence[str], log_path: Path, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    """执行命令并将完整标准输出和错误输出留存到组日志。"""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    command_text: str = shlex.join(command)
    result: subprocess.CompletedProcess[str] = subprocess.run(
        list(command),
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    with log_path.open("a", encoding="utf-8", newline="\n") as log_file:
        log_file.write(f"$ {command_text}\n")
        log_file.write(result.stdout)
        if result.stdout and not result.stdout.endswith("\n"):
            log_file.write("\n")
        log_file.write(result.stderr)
        if result.stderr and not result.stderr.endswith("\n"):
            log_file.write("\n")
        log_file.write(f"exit_code={result.returncode}\n")
    if result.stdout:
        sys.stdout.write(result.stdout)
    if result.stderr:
        sys.stderr.write(result.stderr)
    if result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, list(command), result.stdout, result.stderr)
    return result


def first_line(command: Sequence[str]) -> str:
    """取得工具版本第一行。"""
    result: subprocess.CompletedProcess[str] = subprocess.run(
        list(command), check=True, capture_output=True, text=True, encoding="utf-8", errors="replace"
    )
    return next((line.strip() for line in result.stdout.splitlines() if line.strip()), "")


def actual_commit(directory: Path) -> str:
    """读取指定 checkout 的实际 HEAD。"""
    result: subprocess.CompletedProcess[str] = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=directory,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    commit: str = result.stdout.strip()
    if not commit:
        raise ValueError(f"checkout 没有提供 HEAD：{directory}")
    return commit


def read_environment(directory: Path) -> dict[str, Any]:
    """读取已记录的 runner 环境，文件不存在时返回空对象。"""
    path: Path = directory / "environment.json"
    return read_json(path) if path.exists() else {}


def update_environment(directory: Path, updates: Mapping[str, Any]) -> dict[str, Any]:
    """增量记录 runner 和 checkout 信息。"""
    environment: dict[str, Any] = read_environment(directory)
    environment.update(updates)
    write_json(directory / "environment.json", environment)
    return environment


def build_locations(root: Path, group: str, variant: str) -> tuple[Path, Path, Path | None]:
    """返回构建源码、构建目录和抽样头文件覆盖目录。"""
    if group == "general":
        if variant == "candidate":
            return root, root / "build" / "general-candidate", None
        source: Path = root / "baseline-src"
        return source, source / "build" / "general-baseline", None

    if group not in SAMPLING_GROUPS:
        raise ValueError(f"未知比较组：{group}")
    source = root
    build_name: str = f"{group}-{variant}"
    build_directory: Path = root / "build" / "sampling" / build_name
    override_directory: Path = root if variant == "candidate" else root / "sampling-baseline-src"
    return source, build_directory, override_directory


def binary_path(root: Path, group: str, variant: str, target: str) -> Path:
    """返回当前比较组指定版本的可执行文件。"""
    _, build_directory, _ = build_locations(root, group, variant)
    return build_directory / target


def cmake_configure_command(
    root: Path, group: str, variant: str, configuration: Mapping[str, Any],
) -> tuple[list[str], Path, Path]:
    """生成单组构建命令，并返回源码与构建目录。"""
    source, build_directory, header_override = build_locations(root, group, variant)
    target: Any = configuration.get("target")
    standard: Any = configuration.get("standard")
    if not isinstance(target, str) or not target:
        raise ValueError(f"{group} 缺少 target")
    if isinstance(standard, bool) or not isinstance(standard, int):
        raise ValueError(f"{group} standard 必须为整数")

    command: list[str] = [
        "cmake",
        "-S",
        str(source),
        "-B",
        str(build_directory),
        "-DRANDX_BUILD_BENCHMARK=ON",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_COMPILER=g++-14",
        f"-DCMAKE_CXX_STANDARD={standard}",
        f"-DFETCHCONTENT_BASE_DIR={root / 'build' / '_deps'}",
    ]
    if header_override is not None:
        command.append(f"-DRANDX_SAMPLING_HEADER_DIR={header_override}")
    return command, source, build_directory


def ensure_group_directory(root: Path, group: str) -> Path:
    """创建组目录、日志目录和空的完整用例清单。"""
    directory: Path = group_directory(root, group)
    (directory / "logs").mkdir(parents=True, exist_ok=True)
    expected_path: Path = directory / "expected.json"
    if not expected_path.exists():
        write_json(expected_path, {"candidate": [], "baseline": []})
    return directory


def initialize(plan_path: Path, group: str) -> None:
    """初始化比较组目录并记录候选 checkout 身份。"""
    root: Path = workspace_root()
    if group not in GROUP_NAMES:
        raise ValueError(f"未知比较组：{group}")
    directory: Path = ensure_group_directory(root, group)
    plan: dict[str, Any] = read_json(plan_path)
    context: Any = plan.get("context")
    baseline: Any = plan.get("baseline")
    candidate_commit: str = actual_commit(root)
    baseline_commit: str | None = None
    if group in SAMPLING_GROUPS:
        baseline_commit = str(plan.get("sampling_commit") or "") or None
    elif plan.get("mode") == "COMPARE" and isinstance(baseline, dict):
        commit: Any = baseline.get("commit")
        baseline_commit = commit if isinstance(commit, str) else None
    if not isinstance(context, dict):
        raise ValueError("计划缺少 context 对象")

    hardware: subprocess.CompletedProcess[str] = run_logged(
        ["lscpu", "--json"], directory / "logs" / "hardware.log",
    )

    environment: dict[str, Any] = {
        "compiler": "g++-14",
        "compiler_version": first_line(["g++-14", "--version"]),
        "cmake_version": first_line(["cmake", "--version"]),
        "candidate_commit": candidate_commit,
        "baseline_commit": baseline_commit,
        "runner_os": os.environ.get("RUNNER_OS", ""),
        "runner_arch": os.environ.get("RUNNER_ARCH", ""),
        "system": platform.platform(),
        "hardware": json.loads(hardware.stdout),
        "cpu": None,
    }
    write_json(directory / "environment.json", environment)


def build(plan_path: Path, group: str, variant: str) -> None:
    """配置并构建指定版本当前比较组的唯一目标。"""
    if variant not in VARIANT_NAMES:
        raise ValueError(f"未知 variant：{variant}")
    root: Path = workspace_root()
    directory: Path = ensure_group_directory(root, group)
    plan: dict[str, Any] = read_json(plan_path)
    configuration: dict[str, Any] = policy_group(plan, group)
    source, build_directory, _ = build_locations(root, group, variant)
    command, _, _ = cmake_configure_command(root, group, variant, configuration)
    target: str = str(configuration["target"])
    log_path: Path = directory / "logs" / f"build-{variant}.log"
    run_logged(command, log_path)
    run_logged(["cmake", "--build", str(build_directory), "--target", target, "--parallel"], log_path)

    environment_updates: dict[str, Any] = {
        "generator": read_cmake_generator(build_directory),
        "fetchcontent_base_dir": str(root / "build" / "_deps"),
    }
    if group == "general" and variant == "baseline":
        environment_updates["baseline_commit"] = actual_commit(source)
    if group in SAMPLING_GROUPS and variant == "baseline":
        environment_updates["baseline_commit"] = actual_commit(root / "sampling-baseline-src")
    update_environment(directory, environment_updates)


def read_cmake_generator(build_directory: Path) -> str:
    """从 CMakeCache 读取实际生成器。"""
    cache_path: Path = build_directory / "CMakeCache.txt"
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_GENERATOR:INTERNAL="):
            return line.partition("=")[2]
    return ""


def enumerate_benchmarks(plan_path: Path, group: str, variant: str) -> None:
    """枚举二进制的完整 benchmark 清单，不应用过滤器。"""
    root: Path = workspace_root()
    directory: Path = ensure_group_directory(root, group)
    configuration: dict[str, Any] = policy_group(read_json(plan_path), group)
    binary: Path = binary_path(root, group, variant, str(configuration["target"]))
    log_path: Path = directory / "logs" / f"enumerate-{variant}.log"
    result: subprocess.CompletedProcess[str] = run_logged([str(binary), RUN_NAME_FLAG], log_path)
    names: list[str] = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not names or len(names) != len(set(names)):
        raise ValueError(f"{group} {variant} 完整用例清单为空或包含重复项")

    expected_path: Path = directory / "expected.json"
    expected: dict[str, Any] = read_json(expected_path) if expected_path.exists() else {}
    expected[variant] = names
    for name in VARIANT_NAMES:
        expected.setdefault(name, [])
    write_json(expected_path, expected)


def result_run_names(path: Path, raw_only: bool = False) -> list[str]:
    """读取结果中的实际运行名称；支持原始轮次与 median 聚合。"""
    data: dict[str, Any] = read_json(path)
    benchmarks: Any = data.get("benchmarks")
    if not isinstance(benchmarks, list):
        raise ValueError(f"结果缺少 benchmarks 数组：{path}")
    names: list[str] = []
    for entry in benchmarks:
        if not isinstance(entry, dict):
            raise ValueError(f"结果包含非对象 benchmark：{path}")
        if raw_only:
            if entry.get("run_type", "iteration") != "iteration":
                continue
            if entry.get("error_occurred") or entry.get("error_message"):
                raise ValueError(f"测量记录失败：{entry.get('name', '<unknown>')}")
            name: Any = entry.get("name")
            if not isinstance(name, str) or not name:
                raise ValueError(f"原始测量记录缺少名称：{path}")
            names.append(name)
        else:
            names.extend(parse_results(data, str(path)).keys())
            break
    if not names or len(names) != len(set(names)):
        raise ValueError(f"结果用例集合为空或重复：{path}")
    return names


def expected_names(directory: Path, variant: str) -> list[str]:
    """读取已枚举的完整运行名称。"""
    expected: dict[str, Any] = read_json(directory / "expected.json")
    names: Any = expected.get(variant)
    if not isinstance(names, list) or not names or not all(isinstance(name, str) for name in names):
        raise ValueError(f"缺少 {variant} 的完整 benchmark 清单")
    if len(names) != len(set(names)):
        raise ValueError(f"{variant} benchmark 清单包含重复项")
    return names


def verify_names(path: Path, names: Sequence[str], raw_only: bool = False) -> None:
    """要求测量结果和枚举得到的完整名称集合完全相同。"""
    actual: list[str] = result_run_names(path, raw_only=raw_only)
    if set(actual) != set(names) or len(actual) != len(names):
        missing: list[str] = sorted(set(names) - set(actual))
        extra: list[str] = sorted(set(actual) - set(names))
        raise ValueError(f"完整用例清单与测量结果不一致；缺失={missing}，新增={extra}")


def benchmark_arguments(configuration: Mapping[str, Any], output_path: Path, repetitions: int) -> list[str]:
    """生成不带筛选项的完整基准运行参数。"""
    min_time: Any = configuration.get("min_time")
    if not isinstance(min_time, str) or not min_time:
        raise ValueError("策略缺少 min_time")
    return [
        RUN_FORMAT_FLAG,
        f"--benchmark_out={output_path}",
        f"--benchmark_repetitions={repetitions}",
        f"--benchmark_min_time={min_time}",
    ]


def measure_general(plan_path: Path, variant: str) -> None:
    """测量通用 benchmark 的一个版本。"""
    root: Path = workspace_root()
    plan: dict[str, Any] = read_json(plan_path)
    configuration: dict[str, Any] = policy_group(plan, "general")
    directory: Path = ensure_group_directory(root, "general")
    names: list[str] = expected_names(directory, variant)
    binary: Path = binary_path(root, "general", variant, str(configuration["target"]))
    output_path: Path = directory / f"{variant}.json"
    repetitions: Any = configuration.get("repetitions")
    if isinstance(repetitions, bool) or not isinstance(repetitions, int):
        raise ValueError("通用组 repetitions 必须为整数")
    arguments: list[str] = benchmark_arguments(configuration, output_path, repetitions)
    arguments.append(REPORT_AGGREGATES_FLAG)
    run_logged([str(binary), *arguments], directory / "logs" / f"measure-{variant}.log")
    verify_names(output_path, names)


def sampling_cpu() -> int:
    """选择 runner 当前可用 CPU 集合中的最小编号。"""
    if not hasattr(os, "sched_getaffinity"):
        raise RuntimeError("抽样测量需要 sched_getaffinity 支持")
    available: set[int] = os.sched_getaffinity(0)
    if not available:
        raise RuntimeError("runner 未提供可用 CPU 集合")
    return min(available)


def measure_sampling_rounds(plan_path: Path, group: str) -> None:
    """交替顺序运行抽样基准的全部初测轮次。"""
    if group not in SAMPLING_GROUPS:
        raise ValueError("measure-rounds 只接受 sampling_cpp17 或 sampling_cpp23")
    root: Path = workspace_root()
    plan: dict[str, Any] = read_json(plan_path)
    configuration: dict[str, Any] = policy_group(plan, group)
    directory: Path = ensure_group_directory(root, group)
    repetitions: Any = configuration.get("repetitions")
    if isinstance(repetitions, bool) or not isinstance(repetitions, int) or repetitions <= 0:
        raise ValueError("抽样组 repetitions 必须是正整数")
    names: dict[str, list[str]] = {variant: expected_names(directory, variant) for variant in VARIANT_NAMES}
    cpu: int = sampling_cpu()
    update_environment(directory, {"cpu": cpu})
    target: str = str(configuration["target"])

    for round_number in range(1, repetitions + 1):
        order: tuple[str, str] = ("baseline", "candidate") if round_number % 2 == 1 else ("candidate", "baseline")
        for variant in order:
            output_path: Path = directory / "rounds" / f"{variant}-{round_number}.json"
            output_path.parent.mkdir(parents=True, exist_ok=True)
            binary: Path = binary_path(root, group, variant, target)
            arguments: list[str] = benchmark_arguments(configuration, output_path, 1)
            run_logged(
                ["taskset", "-c", str(cpu), str(binary), *arguments],
                directory / "logs" / f"measure-{variant}-{round_number}.log",
            )
            verify_names(output_path, names[variant], raw_only=True)


def aggregate_sampling(plan_path: Path, group: str) -> None:
    """合并抽样初测轮次并验证 median 集合。"""
    if group not in SAMPLING_GROUPS:
        raise ValueError("aggregate 只接受 sampling_cpp17 或 sampling_cpp23")
    root: Path = workspace_root()
    directory: Path = ensure_group_directory(root, group)
    configuration: dict[str, Any] = policy_group(read_json(plan_path), group)
    repetitions: Any = configuration.get("repetitions")
    if isinstance(repetitions, bool) or not isinstance(repetitions, int):
        raise ValueError("抽样组 repetitions 必须为整数")
    tool_path: Path = root / "tools" / "merge_benchmark_repetitions.py"
    for variant in VARIANT_NAMES:
        round_paths: list[Path] = [
            directory / "rounds" / f"{variant}-{round_number}.json"
            for round_number in range(1, repetitions + 1)
        ]
        output_path: Path = directory / f"{variant}-initial.json"
        command: list[str] = [
            sys.executable,
            str(tool_path),
            *(str(path) for path in round_paths),
            "--output",
            str(output_path),
        ]
        run_logged(command, directory / "logs" / f"aggregate-{variant}.log")
        verify_names(output_path, expected_names(directory, variant))


def copy_confirmation_files(tool_output: Path, confirmation: Path, repetitions: int) -> None:
    """将固定确认工具的结果和每轮日志复制到门禁约定路径。"""
    rounds_directory: Path = confirmation / "rounds"
    rounds_directory.mkdir(parents=True, exist_ok=True)
    for filename in ("metadata.json", "candidate.json", "baseline.json"):
        source: Path = tool_output / filename
        if source.exists():
            copy2(source, confirmation / filename)
    for variant in VARIANT_NAMES:
        for round_number in range(1, repetitions + 1):
            prefix: str = f"{variant}-{round_number}"
            for suffix in (".json", ".stdout.txt", ".stderr.txt"):
                source = tool_output / f"{prefix}{suffix}"
                if source.exists():
                    copy2(source, rounds_directory / source.name)


def confirm_sampling(plan_path: Path, group: str) -> None:
    """执行固定的抽样疑点双边延长确认并归档完整结果。"""
    if group not in SAMPLING_GROUPS:
        raise ValueError("confirm 只接受 sampling_cpp17 或 sampling_cpp23")
    root: Path = workspace_root()
    plan: dict[str, Any] = read_json(plan_path)
    configuration: dict[str, Any] = policy_group(plan, group)
    directory: Path = ensure_group_directory(root, group)
    confirmation: Path = directory / "confirmation"
    tool_output: Path = confirmation / "tool-output"
    confirmation_rounds: Any = configuration.get("confirmation_repetitions")
    min_time: Any = configuration.get("confirmation_min_time")
    tolerance: Any = configuration.get("tolerance")
    cpu_value: Any = read_environment(directory).get("cpu")
    if isinstance(confirmation_rounds, bool) or not isinstance(confirmation_rounds, int):
        raise ValueError("策略 confirmation_repetitions 必须为整数")
    if not isinstance(min_time, str) or not isinstance(tolerance, (int, float)):
        raise ValueError("策略缺少确认测量参数")
    if isinstance(cpu_value, bool) or not isinstance(cpu_value, int):
        raise ValueError("缺少抽样绑定 CPU")

    tool_path: Path = root / "tools" / "confirm_sampling_benchmark.py"
    binary: str = str(configuration["target"])
    command: list[str] = [
        sys.executable,
        str(tool_path),
        "--baseline",
        str(directory / "baseline-initial.json"),
        "--candidate",
        str(directory / "candidate-initial.json"),
        "--baseline-binary",
        str(binary_path(root, group, "baseline", binary)),
        "--candidate-binary",
        str(binary_path(root, group, "candidate", binary)),
        "--output-dir",
        str(tool_output),
        "--repetitions",
        str(confirmation_rounds),
        "--min-time",
        min_time,
        "--tolerance",
        str(tolerance),
        "--cpu",
        str(cpu_value),
    ]
    try:
        run_logged(command, directory / "logs" / "confirm.log")
    finally:
        copy_confirmation_files(tool_output, confirmation, confirmation_rounds)
    verify_names(confirmation / "candidate.json", expected_names(directory, "candidate"))
    verify_names(confirmation / "baseline.json", expected_names(directory, "baseline"))


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    init_parser: argparse.ArgumentParser = commands.add_parser("init")
    init_parser.add_argument("--plan", type=Path, required=True)
    init_parser.add_argument("--group", choices=GROUP_NAMES, required=True)

    build_parser: argparse.ArgumentParser = commands.add_parser("build")
    build_parser.add_argument("--plan", type=Path, required=True)
    build_parser.add_argument("--group", choices=GROUP_NAMES, required=True)
    build_parser.add_argument("--variant", choices=VARIANT_NAMES, required=True)

    enumerate_parser: argparse.ArgumentParser = commands.add_parser("enumerate")
    enumerate_parser.add_argument("--plan", type=Path, required=True)
    enumerate_parser.add_argument("--group", choices=GROUP_NAMES, required=True)
    enumerate_parser.add_argument("--variant", choices=VARIANT_NAMES, required=True)

    measure_parser: argparse.ArgumentParser = commands.add_parser("measure")
    measure_parser.add_argument("--plan", type=Path, required=True)
    measure_parser.add_argument("--group", choices=GROUP_NAMES, required=True)
    measure_parser.add_argument("--variant", choices=VARIANT_NAMES)

    aggregate_parser: argparse.ArgumentParser = commands.add_parser("aggregate")
    aggregate_parser.add_argument("--plan", type=Path, required=True)
    aggregate_parser.add_argument("--group", choices=tuple(SAMPLING_GROUPS), required=True)

    confirm_parser: argparse.ArgumentParser = commands.add_parser("confirm")
    confirm_parser.add_argument("--plan", type=Path, required=True)
    confirm_parser.add_argument("--group", choices=tuple(SAMPLING_GROUPS), required=True)

    arguments: argparse.Namespace = parser.parse_args()
    try:
        if arguments.command == "init":
            initialize(arguments.plan, arguments.group)
            return 0
        if arguments.command == "build":
            build(arguments.plan, arguments.group, arguments.variant)
            return 0
        if arguments.command == "enumerate":
            enumerate_benchmarks(arguments.plan, arguments.group, arguments.variant)
            return 0
        if arguments.command == "measure":
            if arguments.group == "general" and arguments.variant is not None:
                measure_general(arguments.plan, arguments.variant)
            elif arguments.group in SAMPLING_GROUPS and arguments.variant is None:
                measure_sampling_rounds(arguments.plan, arguments.group)
            else:
                raise ValueError("general 要指定 variant，sampling 由单一交替轮次调用")
            return 0
        if arguments.command == "aggregate":
            aggregate_sampling(arguments.plan, arguments.group)
            return 0
        if arguments.command == "confirm":
            confirm_sampling(arguments.plan, arguments.group)
            return 0
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"比较组执行失败：{error}", file=sys.stderr)
        return 1
    return 1


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
