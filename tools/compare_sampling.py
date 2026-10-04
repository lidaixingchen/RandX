"""使用同一抽样生成器比较基点与候选头文件。"""

from __future__ import annotations

import argparse
import difflib
import json
import platform
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


EXIT_SUCCESS: int = 0
EXIT_DIFFERENCE: int = 1
EXIT_PROCESS_FAILURE: int = 2
METADATA_SCHEMA_VERSION: int = 1
SUPPORTED_COMPILER_FAMILIES: tuple[str, ...] = ("gcc", "clang", "msvc")
SUPPORTED_STANDARDS: tuple[str, ...] = ("c++17", "c++20", "c++23")
SUPPORTED_BUILD_MODES: tuple[str, ...] = ("debug", "release")
PRODUCTION_HEADERS: tuple[str, ...] = ("RandX.hpp", "RandX_Cpp17.hpp")
GENERATOR_SOURCE: str = "gen_parity_sequences.cpp"
SAMPLING_SUPPORT_SOURCE: str = "tests/common/sampling_observations.hpp"
CASE_PATTERN: re.Pattern[str] = re.compile(
    r"^(?:case(?:=|\s+)|\[(?:sampling-)?case\]\s*)([^\s,]+)", re.IGNORECASE
)
CASE_CONTEXT_LINES: int = 3
BYTE_CONTEXT_WIDTH: int = 48
STDLIB_PATTERN: re.Pattern[str] = re.compile(r"RANDX_STDLIB=([^\r\n]+)")
COMMON_REQUIRED_CASES: tuple[str, ...] = (
    "iterator-zero",
    "iterator-empty",
    "iterator-full",
    "iterator-oversample",
    "iterator-hash-threshold",
    "iterator-index-threshold",
    "container-bitmap-at-threshold",
    "container-bitmap-above-threshold",
    "reservoir-default",
    "default-iterator-consecutive",
    "iterator-explicit-hash-threshold",
    "iterator-explicit-index-threshold",
    "iterator-explicit-zero",
    "iterator-explicit-empty",
    "iterator-explicit-full",
    "iterator-explicit-oversample",
    "container-explicit-bitmap-threshold",
    "container-explicit-bitmap-above-threshold",
    "container-explicit-bitmap-word",
    "container-explicit-bitmap-word-above",
    "container-explicit-zero",
    "container-explicit-empty",
    "container-explicit-full",
    "container-explicit-oversample",
    "iterator-explicit-32-bit",
    "iterator-explicit-nonzero-minimum",
    "iterator-explicit-noncopyable-engine",
    "iterator-engine-failure",
    "iterator-hash-copy-failure",
    "iterator-index-copy-failure",
    "container-bitmap-copy-failure",
    "container-full-copy-failure",
    "reservoir-initial-copy-failure",
    "reservoir-explicit-zero",
    "reservoir-explicit-empty",
    "reservoir-explicit-full",
    "reservoir-explicit-oversample",
    "reservoir-replacement-assignment-failure",
    "reservoir-explicit",
    "explicit-iterator-consecutive",
)
CPP23_REQUIRED_CASES: tuple[str, ...] = (
    "cpp23-counted-random-access-sentinel",
    "cpp23-move-only-istream-range",
    "cpp23-move-only-istream-explicit",
)


@dataclass(frozen=True)
class CompareConfig:
    baseline_ref: str
    candidate_root: Path
    compiler: str
    compiler_family: str
    standard: str
    build_mode: str
    output_dir: Path


@dataclass(frozen=True)
class OutputComparison:
    identical: bool
    report: str
    first_difference_case: str | None
    missing_cases: tuple[str, ...]
    added_cases: tuple[str, ...]


@dataclass(frozen=True)
class ComparisonResult:
    exit_code: int
    report: str


class CompareSamplingError(Exception):
    def __init__(self, stage: str, message: str, returncode: int | None = None) -> None:
        super().__init__(message)
        self.stage: str = stage
        self.returncode: int | None = returncode


def run_command(command: Sequence[str], cwd: Path) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        list(command),
        cwd=cwd,
        capture_output=True,
        check=False,
    )


def build_command(
    compiler: str | Path,
    compiler_family: str,
    standard: str,
    build_mode: str,
    source: Path,
    header_directory: Path,
    shared_source_directory: Path,
    executable: Path,
    platform_name: str | None = None,
    historical_api_set: bool = False,
) -> list[str]:
    family: str = compiler_family.lower()
    system_name: str = platform_name or platform.system()
    compiler_path: str = str(compiler)
    include_directories: tuple[Path, Path] = (header_directory, shared_source_directory)
    compatibility_define: tuple[str, ...] = ("RANDX_PARITY_CPP17",) if standard != "c++23" else ()

    if family == "msvc":
        standard_flag: str = "/std:c++23preview" if standard == "c++23" else f"/std:{standard}"
        mode_flags: tuple[str, ...] = (
            ("/Od",) if build_mode == "debug" else ("/O2", "/DNDEBUG")
        )
        command: list[str] = [
            compiler_path,
            "/nologo",
            standard_flag,
            "/utf-8",
            "/Zc:__cplusplus",
            "/W4",
            "/EHsc",
            *mode_flags,
        ]
        if compatibility_define:
            command.append("/DRANDX_PARITY_CPP17")
        if historical_api_set:
            command.append("/DRANDX_PARITY_HISTORICAL_API_SET")
        command.extend(f"/I{directory}" for directory in include_directories)
        command.extend((f"/Fe:{executable}", str(source)))
        return command

    if family not in ("gcc", "clang"):
        raise ValueError(f"不支持的编译器系列：{compiler_family}")

    mode_flags = (
        ("-D_GLIBCXX_ASSERTIONS",) if build_mode == "debug" else ("-DNDEBUG",)
    )
    command = [
        compiler_path,
        f"-std={standard}",
        "-O2",
        "-Wall",
        "-Wextra",
        *mode_flags,
    ]
    if compatibility_define:
        command.append("-DRANDX_PARITY_CPP17")
    if historical_api_set:
        command.append("-DRANDX_PARITY_HISTORICAL_API_SET")
    for directory in include_directories:
        command.extend(("-I", str(directory)))
    command.extend(("-o", str(executable), str(source)))
    if system_name == "Windows":
        command.append("-lbcrypt")
    elif system_name == "Darwin":
        command.extend(("-framework", "Security"))
    return command


def _case_ids(output: bytes) -> tuple[str, ...]:
    decoded_output: str = output.decode("utf-8", errors="replace")
    case_ids: list[str] = []
    for line in decoded_output.splitlines():
        match: re.Match[str] | None = CASE_PATTERN.match(line.strip())
        if match is not None:
            case_ids.append(match.group(1))
    return tuple(dict.fromkeys(case_ids))


def _case_at_line(output_lines: Sequence[str], target_line: int) -> str | None:
    for line in reversed(output_lines[:target_line]):
        match: re.Match[str] | None = CASE_PATTERN.match(line.strip())
        if match is not None:
            return match.group(1)
    return None


def _case_on_line(output_lines: Sequence[str], target_line: int) -> str | None:
    if target_line >= len(output_lines):
        return None
    match: re.Match[str] | None = CASE_PATTERN.match(output_lines[target_line].strip())
    return match.group(1) if match is not None else None


def _first_difference_offset(first: bytes, second: bytes) -> int:
    common_byte_count: int = 0
    shared_limit: int = min(len(first), len(second))
    while common_byte_count < shared_limit and first[common_byte_count] == second[common_byte_count]:
        common_byte_count += 1
    return common_byte_count


def _output_context(output: bytes, changed_line: int) -> list[str]:
    lines: list[str] = output.decode("utf-8", errors="replace").splitlines()
    first_line: int = max(0, changed_line - CASE_CONTEXT_LINES)
    final_line: int = min(len(lines), changed_line + CASE_CONTEXT_LINES + 1)
    return lines[first_line:final_line]


def _required_cases_for_standard(standard: str) -> tuple[str, ...]:
    if standard == "c++23":
        return COMMON_REQUIRED_CASES + CPP23_REQUIRED_CASES
    return COMMON_REQUIRED_CASES


def compare_outputs(
    baseline: bytes,
    candidate: bytes,
    required_cases: Sequence[str] = (),
) -> OutputComparison:
    baseline_cases: tuple[str, ...] = _case_ids(baseline)
    candidate_cases: tuple[str, ...] = _case_ids(candidate)
    for variant, case_ids in (("基点", baseline_cases), ("候选", candidate_cases)):
        if not case_ids:
            raise CompareSamplingError("观察校验", f"{variant}输出不包含任何案例标识。")
        missing_required_cases: tuple[str, ...] = tuple(
            case for case in required_cases if case not in case_ids
        )
        if missing_required_cases:
            missing_text: str = ", ".join(missing_required_cases)
            raise CompareSamplingError(
                "观察校验",
                f"{variant}输出缺少必需案例：{missing_text}",
            )
    missing_cases: tuple[str, ...] = tuple(
        case for case in baseline_cases if case not in candidate_cases
    )
    added_cases: tuple[str, ...] = tuple(
        case for case in candidate_cases if case not in baseline_cases
    )
    if baseline == candidate:
        return OutputComparison(
            identical=True,
            report="抽样输出逐字节一致。",
            first_difference_case=None,
            missing_cases=(),
            added_cases=(),
        )

    baseline_lines: list[str] = baseline.decode("utf-8", errors="replace").splitlines()
    candidate_lines: list[str] = candidate.decode("utf-8", errors="replace").splitlines()
    difference_offset: int = _first_difference_offset(baseline, candidate)
    changed_line: int = baseline[:difference_offset].count(b"\n")
    first_difference_case: str | None = _case_on_line(baseline_lines, changed_line)
    if first_difference_case is None:
        first_difference_case = _case_on_line(candidate_lines, changed_line)
    if first_difference_case is None:
        first_difference_case = _case_at_line(baseline_lines, changed_line)
    if first_difference_case is None:
        first_difference_case = _case_at_line(candidate_lines, changed_line)

    report_lines: list[str] = ["抽样输出存在差异。"]
    if first_difference_case is not None:
        report_lines.append(f"首个差异案例：{first_difference_case}")
    report_lines.append(f"首个差异行：{changed_line + 1}")
    fragment_start: int = max(0, difference_offset - BYTE_CONTEXT_WIDTH)
    fragment_end: int = difference_offset + BYTE_CONTEXT_WIDTH
    report_lines.append(f"首个差异字节偏移：{difference_offset}")
    report_lines.append(f"基点字节片段：{baseline[fragment_start:fragment_end]!r}")
    report_lines.append(f"候选字节片段：{candidate[fragment_start:fragment_end]!r}")
    if missing_cases:
        report_lines.append(f"候选缺少案例：{', '.join(missing_cases)}")
    if added_cases:
        report_lines.append(f"候选新增案例：{', '.join(added_cases)}")
    report_lines.append("基点上下文：")
    report_lines.extend(f"  {line}" for line in _output_context(baseline, changed_line))
    report_lines.append("候选上下文：")
    report_lines.extend(f"  {line}" for line in _output_context(candidate, changed_line))

    baseline_diff_lines: list[str] = baseline.decode("utf-8", errors="replace").splitlines()
    candidate_diff_lines: list[str] = candidate.decode("utf-8", errors="replace").splitlines()
    unified_diff: list[str] = list(
        difflib.unified_diff(
            baseline_diff_lines,
            candidate_diff_lines,
            fromfile="baseline",
            tofile="candidate",
            lineterm="",
            n=CASE_CONTEXT_LINES,
        )
    )
    report_lines.extend(("逐行差异：", *unified_diff))
    return OutputComparison(
        identical=False,
        report="\n".join(report_lines),
        first_difference_case=first_difference_case,
        missing_cases=missing_cases,
        added_cases=added_cases,
    )


def _parse_arguments(argv: Sequence[str] | None = None) -> CompareConfig:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="使用同一抽样生成器比较基点与候选 RandX 头文件。"
    )
    parser.add_argument("--baseline-ref", required=True, help="包含基点头文件的 Git 提交或引用")
    parser.add_argument("--candidate-root", required=True, type=Path, help="候选源码根目录")
    parser.add_argument("--compiler", required=True, help="编译器可执行文件路径或命令名")
    parser.add_argument(
        "--compiler-family",
        required=True,
        choices=SUPPORTED_COMPILER_FAMILIES,
        help="编译器系列",
    )
    parser.add_argument("--standard", required=True, choices=SUPPORTED_STANDARDS, help="C++ 语言标准")
    parser.add_argument(
        "--build-mode", required=True, choices=SUPPORTED_BUILD_MODES, help="CI 构建配置"
    )
    parser.add_argument("--output-dir", required=True, type=Path, help="比较结果输出目录")
    arguments: argparse.Namespace = parser.parse_args(argv)
    return CompareConfig(
        baseline_ref=arguments.baseline_ref,
        candidate_root=arguments.candidate_root,
        compiler=arguments.compiler,
        compiler_family=arguments.compiler_family,
        standard=arguments.standard,
        build_mode=arguments.build_mode,
        output_dir=arguments.output_dir,
    )


def _check_process_result(
    result: subprocess.CompletedProcess[bytes], stage: str, command: Sequence[str]
) -> None:
    if result.returncode == 0:
        return
    details: str = (result.stderr or result.stdout).decode("utf-8", errors="replace").strip()
    command_text: str = subprocess.list2cmdline(list(command))
    message: str = f"{stage}失败，退出码 {result.returncode}。命令：{command_text}"
    if details:
        message = f"{message}\n{details}"
    raise CompareSamplingError(stage, message, result.returncode)


def _write_process_logs(
    output_directory: Path,
    log_name: str,
    result: subprocess.CompletedProcess[bytes],
) -> tuple[str, str]:
    stdout_path: Path = output_directory / f"{log_name}.stdout.txt"
    stderr_path: Path = output_directory / f"{log_name}.stderr.txt"
    stdout_path.write_bytes(result.stdout or b"")
    stderr_path.write_bytes(result.stderr or b"")
    return str(stdout_path), str(stderr_path)


def _run_metadata_command(
    command: Sequence[str],
    cwd: Path,
    output_directory: Path,
    log_name: str,
    stage: str,
) -> subprocess.CompletedProcess[bytes]:
    result: subprocess.CompletedProcess[bytes] = run_command(command, cwd)
    _write_process_logs(output_directory, log_name, result)
    _check_process_result(result, stage, command)
    return result


def _standard_flag(compiler_family: str, standard: str) -> str:
    if compiler_family == "msvc":
        return "/std:c++23preview" if standard == "c++23" else f"/std:{standard}"
    return f"-std={standard}"


def _mode_flags(compiler_family: str, build_mode: str) -> tuple[str, ...]:
    if compiler_family == "msvc":
        return ("/Od",) if build_mode == "debug" else ("/O2", "/DNDEBUG")
    return ("-D_GLIBCXX_ASSERTIONS",) if build_mode == "debug" else ("-DNDEBUG",)


def _compatibility_flag(compiler_family: str, standard: str) -> tuple[str, ...]:
    if standard == "c++23":
        return ()
    flag: str = "/DRANDX_PARITY_CPP17" if compiler_family == "msvc" else "-DRANDX_PARITY_CPP17"
    return (flag,)


def _write_standard_library_probe(source_path: Path) -> None:
    probe_source: str = """#include <version>
#define RANDX_STRINGIZE_INNER(value) #value
#define RANDX_STRINGIZE(value) RANDX_STRINGIZE_INNER(value)
#if defined(_MSVC_STL_VERSION)
#pragma message("RANDX_STDLIB=MSVC STL " RANDX_STRINGIZE(_MSVC_STL_VERSION))
#if defined(_MSVC_STL_UPDATE)
#pragma message("RANDX_STL_UPDATE=" RANDX_STRINGIZE(_MSVC_STL_UPDATE))
#endif
#elif defined(_LIBCPP_VERSION)
#pragma message("RANDX_STDLIB=libc++ " RANDX_STRINGIZE(_LIBCPP_VERSION))
#elif defined(__GLIBCXX__)
#pragma message("RANDX_STDLIB=libstdc++ " RANDX_STRINGIZE(__GLIBCXX__))
#elif defined(__GLIBCPP__)
#pragma message("RANDX_STDLIB=libstdc++ " RANDX_STRINGIZE(__GLIBCPP__))
#else
#pragma message("RANDX_STDLIB=unreported")
#endif
"""
    source_path.write_text(probe_source, encoding="utf-8")


def _standard_library_command(
    config: CompareConfig,
    source_path: Path,
    header_directory: Path,
    shared_source_directory: Path,
) -> list[str]:
    family: str = config.compiler_family
    command: list[str] = [config.compiler, _standard_flag(family, config.standard)]
    if family == "msvc":
        command.extend(("/nologo", "/utf-8", "/Zc:__cplusplus", "/W4", "/EHsc", "/Zs"))
        command.extend(_mode_flags(family, config.build_mode))
        command.extend(_compatibility_flag(family, config.standard))
        command.extend((f"/I{header_directory}", f"/I{shared_source_directory}", str(source_path)))
        return command
    command.extend(("-Wall", "-Wextra", *_mode_flags(family, config.build_mode)))
    command.extend(_compatibility_flag(family, config.standard))
    for directory in (header_directory, shared_source_directory):
        command.extend(("-I", str(directory)))
    command.extend(("-fsyntax-only", str(source_path)))
    return command


def _compiler_version_command(compiler: str, compiler_family: str) -> list[str]:
    if compiler_family == "msvc":
        return [compiler, "/Bv"]
    return [compiler, "--version"]


def _parse_standard_library_version(output: bytes) -> str:
    text: str = output.decode("utf-8", errors="replace")
    match: re.Match[str] | None = STDLIB_PATTERN.search(text)
    if match is None:
        return "未报告"
    version: str = match.group(1).strip().strip("'\"“”‘’")
    update_match: re.Match[str] | None = re.search(r"RANDX_STL_UPDATE=([^\r\n]+)", text)
    if update_match is not None:
        update: str = update_match.group(1).strip().strip("'\"“”‘’")
        version = f"{version} (update {update})"
    return version


def _baseline_headers(
    config: CompareConfig,
    baseline_directory: Path,
    resolved_baseline: str,
    metadata: dict[str, Any],
    output_directory: Path,
) -> None:
    baseline_sources: dict[str, str] = {}
    for header in PRODUCTION_HEADERS:
        command: list[str] = ["git", "show", f"{resolved_baseline}:{header}"]
        result: subprocess.CompletedProcess[bytes] = _run_metadata_command(
            command,
            config.candidate_root,
            output_directory,
            f"git_show_{header.replace('.', '_')}",
            "提取基点头文件",
        )
        header_path: Path = baseline_directory / header
        header_path.write_bytes(result.stdout)
        baseline_sources[header] = str(header_path)
    metadata["baseline_header_sources"] = baseline_sources


def _copy_candidate_sources(
    config: CompareConfig, shared_source_directory: Path
) -> dict[str, dict[str, str]]:
    source_files: tuple[str, ...] = (GENERATOR_SOURCE, SAMPLING_SUPPORT_SOURCE)
    copied_sources: dict[str, dict[str, str]] = {}
    for relative_path in source_files:
        source_path: Path = config.candidate_root / relative_path
        if not source_path.is_file():
            raise CompareSamplingError(
                "候选源码准备",
                f"候选源码缺失：{source_path}",
            )
        destination_path: Path = shared_source_directory / relative_path
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, destination_path)
        copied_sources[relative_path] = {
            "candidate_source": str(source_path),
            "shared_copy": str(destination_path),
        }
    return copied_sources


def _make_build_directories(output_directory: Path) -> dict[str, Path]:
    directories: dict[str, Path] = {
        "baseline_headers": output_directory / "baseline_headers",
        "candidate_headers": output_directory / "candidate_headers",
        "public_source": output_directory / "public_source",
        "metadata_probe": output_directory / "metadata_probe",
        "baseline_build": output_directory / "baseline_build",
        "candidate_build": output_directory / "candidate_build",
    }
    for directory in directories.values():
        directory.mkdir(parents=True, exist_ok=True)
    return directories


def _build_variant(
    config: CompareConfig,
    variant: str,
    source_path: Path,
    header_directory: Path,
    build_directory: Path,
    output_directory: Path,
    metadata: dict[str, Any],
) -> Path:
    executable_name: str = "sampling.exe" if platform.system() == "Windows" else "sampling"
    executable_path: Path = build_directory / executable_name
    command: list[str] = build_command(
        compiler=config.compiler,
        compiler_family=config.compiler_family,
        standard=config.standard,
        build_mode=config.build_mode,
        source=source_path,
        header_directory=header_directory,
        shared_source_directory=source_path.parent,
        executable=executable_path,
        historical_api_set=variant == "baseline",
    )
    build_started: float = time.perf_counter()
    result: subprocess.CompletedProcess[bytes] = run_command(command, build_directory)
    build_seconds: float = time.perf_counter() - build_started
    stdout_path, stderr_path = _write_process_logs(output_directory, f"build_{variant}", result)
    metadata["builds"][variant] = {
        "command": command,
        "working_directory": str(build_directory),
        "elapsed_seconds": build_seconds,
        "returncode": result.returncode,
        "stdout_path": stdout_path,
        "stderr_path": stderr_path,
        "executable": str(executable_path),
    }
    _check_process_result(result, f"{variant} 编译", command)
    metadata["builds"][variant]["executable_bytes"] = executable_path.stat().st_size
    return executable_path


def _execute_variant(
    executable_path: Path,
    variant: str,
    build_directory: Path,
    output_directory: Path,
    metadata: dict[str, Any],
) -> bytes:
    command: list[str] = [str(executable_path), "--sampling-only"]
    result: subprocess.CompletedProcess[bytes] = run_command(command, build_directory)
    output_path: Path = output_directory / f"{variant}_output.txt"
    stderr_path: Path = output_directory / f"{variant}_execution.stderr.txt"
    output_path.write_bytes(result.stdout or b"")
    stderr_path.write_bytes(result.stderr or b"")
    metadata["executions"][variant] = {
        "command": command,
        "working_directory": str(build_directory),
        "returncode": result.returncode,
        "output_path": str(output_path),
        "stderr_path": str(stderr_path),
    }
    if result.returncode != 0:
        details: str = (result.stderr or result.stdout).decode("utf-8", errors="replace").strip()
        message: str = f"{variant} 生成器运行失败，退出码 {result.returncode}。"
        if details:
            message = f"{message}\n{details}"
        raise CompareSamplingError(f"{variant} execution", message, result.returncode)
    return result.stdout or b""


def _write_metadata(output_directory: Path, metadata: dict[str, Any]) -> None:
    metadata_path: Path = output_directory / "metadata.json"
    metadata_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def _initial_metadata(config: CompareConfig) -> dict[str, Any]:
    return {
        "schema_version": METADATA_SCHEMA_VERSION,
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "baseline_ref": config.baseline_ref,
        "candidate_root": str(config.candidate_root.resolve()),
        "compiler": {
            "command": config.compiler,
            "family": config.compiler_family,
            "version": None,
        },
        "standard_library": {"version": None},
        "platform": {
            "system": platform.system(),
            "details": platform.platform(),
        },
        "standard": config.standard,
        "build_mode": config.build_mode,
        "source_parameters": {
            "generator": GENERATOR_SOURCE,
            "support_header": SAMPLING_SUPPORT_SOURCE,
            "run_arguments": ["--sampling-only"],
        },
        "builds": {},
        "executions": {},
        "environment_commands": {},
    }


def _validate_config(config: CompareConfig) -> None:
    if config.compiler_family not in SUPPORTED_COMPILER_FAMILIES:
        raise CompareSamplingError("参数校验", f"不支持的编译器系列：{config.compiler_family}")
    if config.standard not in SUPPORTED_STANDARDS:
        raise CompareSamplingError("参数校验", f"不支持的语言标准：{config.standard}")
    if config.build_mode not in SUPPORTED_BUILD_MODES:
        raise CompareSamplingError("参数校验", f"不支持的构建模式：{config.build_mode}")
    if not config.candidate_root.is_dir():
        raise CompareSamplingError("参数校验", f"候选根目录不存在：{config.candidate_root}")


def run_comparison(config: CompareConfig) -> ComparisonResult:
    output_directory: Path = config.output_dir.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    metadata: dict[str, Any] = _initial_metadata(config)
    metadata_path: Path = output_directory / "metadata.json"
    (output_directory / "diff.txt").write_text("", encoding="utf-8")
    for variant in ("baseline", "candidate"):
        (output_directory / f"{variant}_output.txt").write_bytes(b"")

    stage: str = "参数校验"
    try:
        _validate_config(config)
        directories: dict[str, Path] = _make_build_directories(output_directory)
        metadata["paths"] = {name: str(path) for name, path in directories.items()}

        stage = "基点解析"
        resolve_command: list[str] = [
            "git",
            "rev-parse",
            "--verify",
            f"{config.baseline_ref}^{{commit}}",
        ]
        resolve_result: subprocess.CompletedProcess[bytes] = _run_metadata_command(
            resolve_command,
            config.candidate_root,
            output_directory,
            "git_rev_parse",
            stage,
        )
        resolved_baseline: str = resolve_result.stdout.decode("utf-8", errors="replace").strip()
        metadata["baseline_commit"] = resolved_baseline

        stage = "基点头文件提取"
        _baseline_headers(
            config,
            directories["baseline_headers"],
            resolved_baseline,
            metadata,
            output_directory,
        )
        candidate_header_sources: dict[str, dict[str, str]] = {}
        for header in PRODUCTION_HEADERS:
            source_path: Path = config.candidate_root / header
            if not source_path.is_file():
                raise CompareSamplingError("候选头文件准备", f"候选头文件缺失：{source_path}")
            destination_path: Path = directories["candidate_headers"] / header
            shutil.copy2(source_path, destination_path)
            candidate_header_sources[header] = {
                "candidate_source": str(source_path),
                "comparison_copy": str(destination_path),
            }
        metadata["candidate_header_sources"] = candidate_header_sources

        stage = "候选生成器准备"
        copied_sources: dict[str, str] = _copy_candidate_sources(config, directories["public_source"])
        metadata["shared_source_files"] = copied_sources
        source_path: Path = directories["public_source"] / GENERATOR_SOURCE

        probe_source_path: Path = directories["metadata_probe"] / "standard_library_probe.cpp"
        _write_standard_library_probe(probe_source_path)
        stage = "编译器版本"
        version_command: list[str] = _compiler_version_command(
            config.compiler, config.compiler_family
        )
        if config.compiler_family == "msvc":
            version_command.extend(
                (
                    "/nologo",
                    _standard_flag(config.compiler_family, config.standard),
                    "/utf-8",
                    "/Zs",
                    str(probe_source_path),
                )
            )
            version_result: subprocess.CompletedProcess[bytes] = _run_metadata_command(
                version_command,
                directories["metadata_probe"],
                output_directory,
                "compiler_information",
                stage,
            )
            compiler_output: bytes = (version_result.stdout or b"") + (version_result.stderr or b"")
            metadata["compiler"]["version"] = compiler_output.decode(
                "utf-8", errors="replace"
            ).strip()
            metadata["standard_library"]["version"] = _parse_standard_library_version(
                compiler_output
            )
            metadata["environment_commands"]["compiler_and_standard_library"] = version_command
        else:
            version_result = _run_metadata_command(
                version_command,
                directories["public_source"],
                output_directory,
                "compiler_information",
                stage,
            )
            metadata["compiler"]["version"] = (
                (version_result.stdout or b"") + (version_result.stderr or b"")
            ).decode("utf-8", errors="replace").strip()
            stage = "标准库版本"
            probe_command: list[str] = _standard_library_command(
                config,
                probe_source_path,
                directories["baseline_headers"],
                directories["public_source"],
            )
            metadata["environment_commands"]["compiler_version"] = version_command
            probe_result: subprocess.CompletedProcess[bytes] = _run_metadata_command(
                probe_command,
                directories["metadata_probe"],
                output_directory,
                "standard_library_information",
                stage,
            )
            probe_output: bytes = (probe_result.stdout or b"") + (probe_result.stderr or b"")
            metadata["standard_library"]["version"] = _parse_standard_library_version(probe_output)
            metadata["environment_commands"]["standard_library"] = probe_command

        for variant, header_directory, build_directory in (
            (
                "baseline",
                directories["baseline_headers"],
                directories["baseline_build"],
            ),
            (
                "candidate",
                directories["candidate_headers"],
                directories["candidate_build"],
            ),
        ):
            stage = f"{variant} 编译"
            executable_path: Path = _build_variant(
                config,
                variant,
                source_path,
                header_directory,
                build_directory,
                output_directory,
                metadata,
            )
            stage = f"{variant} execution"
            _execute_variant(
                executable_path,
                variant,
                build_directory,
                output_directory,
                metadata,
            )

        stage = "输出比较"
        baseline_output: bytes = (output_directory / "baseline_output.txt").read_bytes()
        candidate_output: bytes = (output_directory / "candidate_output.txt").read_bytes()
        comparison: OutputComparison = compare_outputs(
            baseline_output,
            candidate_output,
            required_cases=_required_cases_for_standard(config.standard),
        )
        (output_directory / "diff.txt").write_text(comparison.report, encoding="utf-8")
        metadata["comparison"] = {
            "identical": comparison.identical,
            "first_difference_case": comparison.first_difference_case,
            "missing_cases": list(comparison.missing_cases),
            "added_cases": list(comparison.added_cases),
            "report_path": str(output_directory / "diff.txt"),
            "baseline_output_path": str(output_directory / "baseline_output.txt"),
            "candidate_output_path": str(output_directory / "candidate_output.txt"),
        }
        _write_metadata(output_directory, metadata)
        exit_code: int = EXIT_SUCCESS if comparison.identical else EXIT_DIFFERENCE
        return ComparisonResult(exit_code=exit_code, report=comparison.report)
    except CompareSamplingError as error:
        failure: dict[str, Any] = {"stage": error.stage, "message": str(error)}
        if error.returncode is not None:
            failure["returncode"] = error.returncode
        metadata["failure"] = failure
        _write_metadata(output_directory, metadata)
        return ComparisonResult(exit_code=EXIT_PROCESS_FAILURE, report=str(error))
    except OSError as error:
        failure = {"stage": stage, "message": str(error)}
        metadata["failure"] = failure
        _write_metadata(output_directory, metadata)
        return ComparisonResult(exit_code=EXIT_PROCESS_FAILURE, report=f"{stage}失败：{error}")


def main(argv: Sequence[str] | None = None) -> int:
    config: CompareConfig = _parse_arguments(argv)
    try:
        result: ComparisonResult = run_comparison(config)
    except OSError as error:
        print(f"比较输出不可用：{error}", file=sys.stderr)
        return EXIT_PROCESS_FAILURE
    output_stream: Any = sys.stdout if result.exit_code in (EXIT_SUCCESS, EXIT_DIFFERENCE) else sys.stderr
    print(result.report, file=output_stream)
    return result.exit_code


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
