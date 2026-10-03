#!/usr/bin/env python3
"""交错比较两份 RandX 头文件快照的 C++17 与 C++23 默认接口基准。"""

from __future__ import annotations

import argparse
import json
import math
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
from collections import Counter
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence

from compare_benchmark import (
    TIME_UNIT_TO_MS,
    BenchmarkDataError,
    ComparisonResult,
    compare_results,
    parse_results,
)
from merge_benchmark_repetitions import merge_results


PROJECT_ROOT: Path = Path(__file__).resolve().parent.parent
POLICY_GROUP: str = "general"
BENCHMARK_FILTER: str = "^BM_Default"
BENCHMARK_LIST_FLAG: str = "--benchmark_list_tests=true"
STANDARD_CONFIGURATIONS: tuple[tuple[str, int, str], ...] = (
    ("cpp23", 23, "benchmark_gbench"),
    ("cpp17", 17, "benchmark_gbench_default_cpp17"),
)
SIDES: tuple[str, str] = ("baseline", "candidate")
HEADER_NAMES: tuple[str, str] = ("RandX.hpp", "RandX_Cpp17.hpp")
BENCHMARK_TARGET_SOURCES: Mapping[str, tuple[str, ...]] = {
    "benchmark_gbench": ("benchmark_gbench.cpp", "benchmark_gbench_default_cpp23.cpp"),
    "benchmark_gbench_default_cpp17": ("benchmark_gbench_default_cpp17.cpp",),
}
INCLUDE_FLAG_PATTERN: re.Pattern[str] = re.compile(
    r"(?<!\S)(?:-I|/I)(?:\s*)(?:\"([^\"]+)\"|'([^']+)'|([^\s]+))"
)
RESPONSE_FILE_PATTERN: re.Pattern[str] = re.compile(r'(?<!\S)@(?:"([^"]+)"|\'([^\']+)\'|([^\s]+))')
COMPILER_MACRO_PATTERN: re.Pattern[str] = re.compile(
    r"^#define (__GLIBCXX__|_GLIBCXX_RELEASE|_LIBCPP_VERSION|_MSVC_STL_VERSION|_MSVC_STL_UPDATE) (.+)$",
    re.MULTILINE,
)


class BenchmarkPairError(RuntimeError):
    """包含报告分类及可选明细的成对基准错误。"""

    def __init__(
        self,
        kind: str,
        message: str,
        scope: str = "run",
        details: Mapping[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.kind: str = kind
        self.scope: str = scope
        self.details: dict[str, Any] = dict(details or {})


CommandRunner = Callable[..., subprocess.CompletedProcess[str]]


def write_json(path: Path, value: Any) -> None:
    """以 UTF-8 写入 JSON，并创建父目录。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read_json(path: Path) -> Any:
    """读取 UTF-8 JSON。"""
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise BenchmarkPairError("data_error", f"无法读取有效 JSON：{path}：{error}") from error


def resolve_executable(value: str, description: str) -> str:
    """将可执行文件参数解析为可供子进程使用的路径。"""
    candidate: Path = Path(value).expanduser()
    if candidate.is_file():
        return str(candidate.resolve())
    located: str | None = shutil.which(value)
    if located is not None:
        return str(Path(located).resolve())
    raise BenchmarkPairError("execution_error", f"找不到{description}：{value}", description)


def read_benchmark_json(path: Path) -> Any:
    """将 Windows 基准框架的本地编码输出转换为 UTF-8，并保留原始文件。"""
    try:
        content: bytes = path.read_bytes()
        try:
            decoded: str = content.decode("utf-8")
        except UnicodeDecodeError:
            if os.name != "nt":
                raise
            decoded = content.decode("mbcs")
            data: Any = json.loads(decoded)
            shutil.copy2(path, path.with_suffix(".native.json"))
            write_json(path, data)
            return data
        return json.loads(decoded)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise BenchmarkPairError("data_error", f"无法读取有效基准 JSON：{path}：{error}") from error


def load_policy(path: Path) -> dict[str, Any]:
    """读取 general 组的轮数、时长与回归容差。"""
    data: Any = read_json(path)
    if not isinstance(data, dict):
        raise BenchmarkPairError("data_error", "策略 JSON 根节点必须是对象。", "policy")
    groups: Any = data.get("groups")
    group: Any = groups.get(POLICY_GROUP) if isinstance(groups, dict) else None
    if not isinstance(group, dict):
        raise BenchmarkPairError("data_error", "策略缺少 general 组配置。", "policy")

    repetitions: Any = group.get("repetitions")
    if isinstance(repetitions, bool) or not isinstance(repetitions, int) or repetitions <= 0:
        raise BenchmarkPairError("data_error", "general.repetitions 必须是正整数。", "policy")
    min_time: Any = group.get("min_time")
    if not isinstance(min_time, str) or not min_time.strip():
        raise BenchmarkPairError("data_error", "general.min_time 必须是非空字符串。", "policy")
    tolerance: Any = group.get("tolerance")
    if (
        isinstance(tolerance, bool)
        or not isinstance(tolerance, (int, float))
        or not math.isfinite(tolerance)
        or tolerance < 0.0
    ):
        raise BenchmarkPairError("data_error", "general.tolerance 必须是非负有限数。", "policy")

    return {
        "repetitions": repetitions,
        "min_time": min_time,
        "tolerance": float(tolerance),
    }


def validate_header_directory(path: Path, side: str) -> Path:
    """确认快照目录同时包含两个已发布头文件。"""
    resolved: Path = path.expanduser().resolve()
    if not resolved.is_dir():
        raise BenchmarkPairError("data_error", f"{side} 头文件目录不存在：{resolved}", side)
    missing: list[str] = [name for name in HEADER_NAMES if not (resolved / name).is_file()]
    if missing:
        raise BenchmarkPairError(
            "data_error",
            f"{side} 头文件快照缺少文件：{', '.join(missing)}。",
            side,
            {"missing_headers": missing, "header_directory": str(resolved)},
        )
    return resolved


def read_cmake_cache(path: Path) -> dict[str, str]:
    """解析 CMakeCache 中的变量值。"""
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith("//") or line.startswith("#") or "=" not in line:
            continue
        declaration, value = line.split("=", 1)
        key = declaration.split(":", 1)[0]
        values[key] = value
    return values


def normalized_path_key(path: Path) -> str:
    """返回适用于当前平台的绝对路径比较键。"""
    return os.path.normcase(str(path.expanduser().resolve()))


def configure_command(
    cmake: str,
    source_root: Path,
    build_directory: Path,
    header_directory: Path,
    compiler: str,
    standard: int,
    generator: str | None,
    configuration: str,
    extra_arguments: Sequence[str],
    dependency_directory: Path,
) -> list[str]:
    """生成一个快照和一种语言标准对应的 CMake 配置命令。"""
    command: list[str] = [cmake, "-S", str(source_root), "-B", str(build_directory)]
    if generator:
        command.extend(("-G", generator))
    command.extend(extra_arguments)
    command.extend(
        (
            "-DRANDX_BUILD_BENCHMARK=ON",
            f"-DCMAKE_BUILD_TYPE={configuration}",
            f"-DCMAKE_CXX_COMPILER={compiler}",
            f"-DCMAKE_CXX_STANDARD={standard}",
            "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            f"-DFETCHCONTENT_BASE_DIR={dependency_directory}",
            f"-DRANDX_SAMPLING_HEADER_DIR={header_directory}",
        )
    )
    return command


def build_command(cmake: str, build_directory: Path, target: str, configuration: str) -> list[str]:
    """生成可在单配置和多配置生成器下工作的构建命令。"""
    return [
        cmake,
        "--build",
        str(build_directory),
        "--config",
        configuration,
        "--target",
        target,
        "--parallel",
        "--verbose",
    ]


def run_logged(
    command: Sequence[str],
    log_path: Path,
    runner: CommandRunner,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
    input_text: str | None = None,
    failure_kind: str = "execution_error",
    scope: str = "run",
) -> subprocess.CompletedProcess[str]:
    """执行命令并保留完整命令、输出及退出码。"""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        result: subprocess.CompletedProcess[str] = runner(
            list(command),
            cwd=cwd,
            env=dict(env) if env is not None else None,
            input=input_text,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as error:
        raise BenchmarkPairError(failure_kind, f"无法执行命令：{command[0]}：{error}", scope) from error

    with log_path.open("w", encoding="utf-8", newline="\n") as log_file:
        command_text: str = subprocess.list2cmdline(list(command)) if os.name == "nt" else shlex.join(command)
        log_file.write(f"$ {command_text}\n")
        log_file.write(result.stdout or "")
        if result.stdout and not result.stdout.endswith("\n"):
            log_file.write("\n")
        log_file.write(result.stderr or "")
        if result.stderr and not result.stderr.endswith("\n"):
            log_file.write("\n")
        log_file.write(f"exit_code={result.returncode}\n")
    if result.returncode != 0:
        message: str = (result.stderr or result.stdout or "命令没有提供错误输出。").strip()
        raise BenchmarkPairError(
            failure_kind,
            f"命令退出码为 {result.returncode}：{command[0]}；{message}",
            scope,
            {"command": list(command), "exit_code": result.returncode, "log": str(log_path)},
        )
    return result


def compiler_metadata(compiler: str, runner: CommandRunner) -> dict[str, Any]:
    """记录编译器版本以及能够读取到的标准库版本宏。"""
    metadata: dict[str, Any] = {"path": compiler, "version": "", "standard_library_macros": {}}
    version: subprocess.CompletedProcess[str] = runner(
        [compiler, "--version"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    metadata["version"] = (version.stdout or version.stderr or "").strip()
    compiler_name: str = Path(compiler).name.lower()
    if any(marker in compiler_name for marker in ("g++", "gcc", "clang++", "clang")):
        macros: subprocess.CompletedProcess[str] = runner(
            [compiler, "-dM", "-E", "-x", "c++", "-"],
            input="#include <version>\n",
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if macros.returncode == 0:
            metadata["standard_library_macros"] = {
                name: value.strip()
                for name, value in COMPILER_MACRO_PATTERN.findall(macros.stdout)
            }
    return metadata


def environment_metadata() -> dict[str, Any]:
    """记录影响本机基准解释的系统、Python 与 CPU 信息。"""
    return {
        "platform": platform.platform(),
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "processor": platform.processor() or os.environ.get("PROCESSOR_IDENTIFIER", ""),
        "logical_cpu_count": os.cpu_count(),
        "python": sys.version,
        "python_executable": sys.executable,
        "working_directory": str(Path.cwd().resolve()),
    }


def cmake_environment(cache: Mapping[str, str]) -> dict[str, str]:
    """提取构建器、编译器和有效 Release 优化参数。"""
    keys: tuple[str, ...] = (
        "CMAKE_GENERATOR",
        "CMAKE_CXX_COMPILER",
        "CMAKE_CXX_COMPILER_ID",
        "CMAKE_CXX_COMPILER_VERSION",
        "CMAKE_CXX_STANDARD",
        "CMAKE_BUILD_TYPE",
        "CMAKE_CXX_FLAGS",
        "CMAKE_CXX_FLAGS_DEBUG",
        "CMAKE_CXX_FLAGS_RELEASE",
        "CMAKE_CXX_FLAGS_RELWITHDEBINFO",
        "CMAKE_CXX_FLAGS_MINSIZEREL",
        "CMAKE_INTERPROCEDURAL_OPTIMIZATION",
        "CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE",
        "CMAKE_MSVC_RUNTIME_LIBRARY",
        "RANDX_SAMPLING_HEADER_DIR",
    )
    return {key: cache[key] for key in keys if key in cache}


def parse_include_directories(command: str) -> list[str]:
    """按编译命令中的先后顺序提取 GCC、Clang 或 MSVC include 目录。"""
    return [next(value for value in match if value) for match in INCLUDE_FLAG_PATTERN.findall(command)]


def expand_response_files(command: str, directory: Path) -> str:
    """在实际编译工作目录展开响应文件，保持参数顺序和路径引号。"""
    def expand(match: re.Match[str]) -> str:
        response_path: Path = Path(next(value for value in match.groups() if value))
        if not response_path.is_absolute():
            response_path = directory / response_path
        try:
            raw_content: bytes = response_path.read_bytes()
            encoding: str = "utf-16" if raw_content.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
            content: str = raw_content.decode(encoding)
        except (OSError, UnicodeDecodeError) as error:
            raise BenchmarkPairError("data_error", f"无法读取编译响应文件：{response_path}：{error}") from error
        return expand_response_files(content, directory)

    return RESPONSE_FILE_PATTERN.sub(expand, command)


def compile_include_directories(entry: Mapping[str, Any]) -> list[str]:
    """从结构化参数优先读取 include 目录，保留包含空格的路径。"""
    arguments: Any = entry.get("arguments")
    directory: Path = Path(str(entry.get("directory", PROJECT_ROOT)))
    if not isinstance(arguments, list) or not all(isinstance(item, str) for item in arguments):
        command: Any = entry.get("command")
        return parse_include_directories(expand_response_files(command, directory)) if isinstance(command, str) else []

    directories: list[str] = []
    index: int = 0
    while index < len(arguments):
        argument: str = arguments[index]
        if argument.startswith("@"):
            response_command: str = "@" + '"' + argument[1:].strip('"') + '"'
            directories.extend(parse_include_directories(expand_response_files(response_command, directory)))
        if argument in ("-I", "/I"):
            if index + 1 < len(arguments):
                directories.append(arguments[index + 1].strip("\"'"))
                index += 2
                continue
        elif argument.startswith("-I") and len(argument) > 2:
            directories.append(argument[2:].strip("\"'"))
        elif argument.startswith("/I") and len(argument) > 2:
            directories.append(argument[2:].strip("\"'"))
        index += 1
    return directories


def compile_command_text(entry: Mapping[str, Any]) -> str:
    """取得 compilation database 中便于记录和解析的命令文本。"""
    arguments: Any = entry.get("arguments")
    if isinstance(arguments, list) and all(isinstance(item, str) for item in arguments):
        return " ".join(arguments)
    command: Any = entry.get("command")
    return command if isinstance(command, str) else ""


def standard_library_metadata(
    compiler: str,
    standard: int,
    cache: Mapping[str, str],
    runner: CommandRunner,
) -> dict[str, Any]:
    """识别标准库实现，并记录可读取的标准库版本宏。"""
    compiler_name: str = Path(compiler).name.lower()
    compiler_id: str = cache.get("CMAKE_CXX_COMPILER_ID", "")
    macros: dict[str, str] = {}
    if any(marker in compiler_name for marker in ("g++", "gcc", "clang++", "clang")) and "clang-cl" not in compiler_name:
        result: subprocess.CompletedProcess[str] = runner(
            [compiler, f"-std=c++{standard}", "-dM", "-E", "-x", "c++", "-"],
            input="#include <version>\n",
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if result.returncode == 0:
            macros = {name: value.strip() for name, value in COMPILER_MACRO_PATTERN.findall(result.stdout)}

    if "_LIBCPP_VERSION" in macros:
        name: str = "libc++"
        version: str = macros["_LIBCPP_VERSION"]
        detection: str = "standard header macro"
    elif "__GLIBCXX__" in macros:
        name = "libstdc++"
        version = macros.get("_GLIBCXX_RELEASE", macros["__GLIBCXX__"])
        detection = "standard header macro"
    elif "_MSVC_STL_VERSION" in macros:
        name = "MSVC STL"
        version = macros["_MSVC_STL_VERSION"]
        detection = "standard header macro"
    elif compiler_id == "MSVC" or "clang-cl" in compiler_name or compiler_name in ("cl", "cl.exe"):
        name = "MSVC STL"
        version = ""
        detection = "compiler toolchain default"
    elif compiler_id == "GNU":
        name = "libstdc++"
        version = ""
        detection = "compiler toolchain default"
    elif compiler_id == "Clang":
        name = "libc++" if platform.system() == "Darwin" else "libstdc++"
        version = ""
        detection = "platform default"
    else:
        name = "unknown"
        version = ""
        detection = "unavailable"
    return {
        "name": name,
        "version": version,
        "version_macros": macros,
        "detection": detection,
    }


def command_contains_target(command: str, target: str) -> bool:
    """从对象目录的完整名称识别目标，区分带前后缀的其他目标。"""
    pattern: str = rf'(?i)(?<![A-Za-z0-9_.-]){re.escape(target)}\.dir(?=[\\/"\s]|$)'
    return re.search(pattern, command) is not None


def verify_target_compile_entries_override(
    target_entries: Sequence[Mapping[str, Any]],
    header_directory: Path,
    required_header: str,
    target: str,
    method: str,
) -> dict[str, Any]:
    """按编译命令中的 include 顺序确认目标头文件来自请求的快照。"""
    expected_sources: tuple[str, ...] = BENCHMARK_TARGET_SOURCES[target]
    compiled_sources: set[str] = set()
    command_evidence: list[dict[str, Any]] = []
    for entry in target_entries:
        command: str = compile_command_text(entry)
        entry_directory_value: Any = entry.get("directory")
        entry_directory: Path = Path(entry_directory_value) if isinstance(entry_directory_value, str) else PROJECT_ROOT
        include_directories: list[Path] = []
        for include_directory in compile_include_directories(entry):
            include_path: Path = Path(include_directory)
            if not include_path.is_absolute():
                include_path = entry_directory / include_path
            include_directories.append(include_path.resolve())

        first_header_directory: Path | None = next(
            (directory for directory in include_directories if (directory / required_header).is_file()),
            None,
        )
        source: Any = entry.get("file")
        if isinstance(source, str):
            source_name: str = source.replace("\\", "/").rsplit("/", 1)[-1]
            if source_name in expected_sources:
                compiled_sources.add(source_name)
        compiled_sources.update(
            expected_source
            for expected_source in expected_sources
            if command_contains_source(command, expected_source)
        )
        if first_header_directory is None:
            raise BenchmarkPairError(
                "data_error",
                f"无法从 {target} 的实际编译 include 路径找到 {required_header}。",
                target,
                {
                    "source": source,
                    "compile_command": command,
                    "include_directories": [str(path) for path in include_directories],
                },
            )
        if normalized_path_key(first_header_directory) != normalized_path_key(header_directory):
            raise BenchmarkPairError(
                "data_error",
                f"{target} 实际首先包含 {required_header} 的目录为 {first_header_directory}，"
                f"预期快照目录为 {header_directory}。",
                target,
                {
                    "source": source,
                    "compile_command": command,
                    "first_header_directory": str(first_header_directory),
                    "expected_header_directory": str(header_directory),
                },
            )
        command_evidence.append(
            {
                "source": source,
                "selected_directory": str(first_header_directory),
                "compile_command": command,
            }
        )
    missing_sources: list[str] = sorted(set(expected_sources) - compiled_sources)
    if missing_sources:
        raise BenchmarkPairError(
            "data_error",
            f"{target} 的实际编译命令未覆盖预期源文件。",
            target,
            {"missing_sources": missing_sources, "compiled_sources": sorted(compiled_sources)},
        )
    return {
        "verified": True,
        "method": method,
        "header": required_header,
        "selected_directory": str(header_directory.resolve()),
        "commands": command_evidence,
    }


def verify_compile_database_override(
    compile_commands_path: Path,
    header_directory: Path,
    required_header: str,
    target: str,
) -> dict[str, Any]:
    """确认 compilation database 中目标命令首先从请求快照找到发布头文件。"""
    data: Any = read_json(compile_commands_path)
    if not isinstance(data, list):
        raise BenchmarkPairError("data_error", "compile_commands.json 根节点必须是数组。", target)
    entries: list[Mapping[str, Any]] = [
        entry
        for entry in data
        if isinstance(entry, dict) and compile_command_text(entry)
    ]
    target_entries: list[Mapping[str, Any]] = [
        entry
        for entry in entries
        if command_contains_target(compile_command_text(entry), target)
    ]
    if not target_entries:
        target_entries = [
            entry
            for entry in entries
            if isinstance(entry.get("file"), str)
            and str(entry["file"]).replace("\\", "/").rsplit("/", 1)[-1]
            in BENCHMARK_TARGET_SOURCES[target]
        ]
    if not target_entries and len(entries) == 1:
        target_entries = entries
    if not target_entries:
        raise BenchmarkPairError(
            "data_error",
            f"compile_commands.json 中没有可识别为 {target} 的编译命令。",
            target,
        )
    return verify_target_compile_entries_override(
        target_entries,
        header_directory,
        required_header,
        target,
        "compile_commands.json",
    )


def command_contains_compiler(command: str, compiler: str) -> bool:
    """判断构建输出行是否调用了当前配置的编译器。"""
    compiler_name: str = Path(compiler).name
    pattern: str = rf"(?i)(?<![A-Za-z0-9_.+-]){re.escape(compiler_name)}(?![A-Za-z0-9_.+-])"
    return re.search(pattern, command) is not None


def command_contains_source(command: str, source_name: str) -> bool:
    """判断命令是否编译整数基准源文件。"""
    pattern: str = rf'(?i)(?:^|[\s"\\/]){re.escape(source_name)}(?:["\s]|$)'
    return re.search(pattern, command) is not None


def command_contains_compile_switch(command: str) -> bool:
    """判断命令是否处于仅编译阶段。"""
    return re.search(r"(?i)(?<!\S)(?:/c|-c)(?=\s|$)", command) is not None


def response_file_mentions_target(command: str, target: str) -> bool:
    """检查响应文件名是否包含目标对象或基准源标识。"""
    for match in RESPONSE_FILE_PATTERN.finditer(command):
        response_path: str = next(value for value in match.groups() if value)
        if command_contains_target(response_path, target) or any(
            source_name.casefold() in response_path.casefold()
            for source_name in BENCHMARK_TARGET_SOURCES[target]
        ):
            return True
    return False


def verify_build_output_override(
    build_output: str,
    build_directory: Path,
    header_directory: Path,
    required_header: str,
    target: str,
    compiler: str,
) -> dict[str, Any]:
    """从详细构建输出中定位目标编译命令并验证头文件选择。"""
    source_names: tuple[str, ...] = BENCHMARK_TARGET_SOURCES[target]
    target_entries: list[Mapping[str, Any]] = []
    for output_line in build_output.splitlines():
        command: str = output_line.strip()
        if not command or not command_contains_compiler(command, compiler):
            continue
        direct_target_hint: bool = command_contains_target(command, target) or any(
            command_contains_source(command, source_name) for source_name in source_names
        )
        response_target_hint: bool = response_file_mentions_target(command, target)
        if not command_contains_compile_switch(command) and "@" not in command:
            continue
        try:
            expanded_command: str = expand_response_files(command, build_directory)
        except BenchmarkPairError as error:
            if direct_target_hint or response_target_hint:
                raise BenchmarkPairError(
                    "data_error",
                    f"无法解析 {target} 的编译命令响应文件：{error}",
                    target,
                    {"compile_command": command, **error.details},
                ) from error
            continue
        source_name: str | None = next(
            (name for name in source_names if command_contains_source(expanded_command, name)),
            None,
        )
        if (
            command_contains_compiler(expanded_command, compiler)
            and command_contains_compile_switch(expanded_command)
            and source_name is not None
            and command_contains_target(expanded_command, target)
        ):
            target_entries.append(
                {
                    "directory": str(build_directory),
                    "file": source_name,
                    "command": expanded_command,
                }
            )
    if not target_entries:
        raise BenchmarkPairError(
            "data_error",
            f"详细构建输出中没有可识别为 {target} 的基准编译命令。",
            target,
            {
                "required_header": required_header,
                "build_directory": str(build_directory.resolve()),
                "target_compile_command_found": False,
            },
        )
    return verify_target_compile_entries_override(
        target_entries,
        header_directory,
        required_header,
        target,
        "verbose_build_output_and_cmake_cache",
    )


def find_binary(build_directory: Path, target: str) -> Path:
    """定位当前生成器输出的可执行文件。"""
    file_names: tuple[str, ...] = (f"{target}.exe", target) if os.name == "nt" else (target, f"{target}.exe")
    matches: list[Path] = [
        path
        for file_name in file_names
        for path in build_directory.rglob(file_name)
        if path.is_file()
    ]
    if not matches:
        raise BenchmarkPairError("build_error", f"构建成功后找不到可执行文件：{target}。", target)
    return min(matches, key=lambda path: len(path.parts))


def disassembler_path(compiler: str) -> str | None:
    """查找 objdump 或 dumpbin，缺少工具时保留其他构建证据。"""
    for name in ("objdump", "dumpbin"):
        located: str | None = shutil.which(name)
        if located:
            return str(Path(located).resolve())
    compiler_directory: Path = Path(compiler).parent
    for name in ("objdump.exe", "objdump", "dumpbin.exe", "dumpbin"):
        candidate: Path = compiler_directory / name
        if candidate.is_file():
            return str(candidate.resolve())
    return None


def save_disassembly(binary: Path, destination: Path, compiler: str, runner: CommandRunner) -> dict[str, Any]:
    """尽可能保存链接产物的反汇编。"""
    tool: str | None = disassembler_path(compiler)
    if tool is None:
        return {"available": False, "reason": "PATH 和编译器目录中均未找到 objdump 或 dumpbin。"}
    arguments: list[str] = [tool, "/DISASM", str(binary)] if Path(tool).name.lower().startswith("dumpbin") else [
        tool,
        "-d",
        "-C",
        str(binary),
    ]
    try:
        result: subprocess.CompletedProcess[str] = runner(
            arguments,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as error:
        return {"available": False, "tool": tool, "reason": str(error)}
    if result.returncode != 0:
        return {"available": False, "tool": tool, "reason": (result.stderr or "反汇编命令失败。").strip()}
    destination.write_text(result.stdout, encoding="utf-8")
    return {"available": True, "tool": tool, "path": str(destination)}


def benchmark_arguments(output_path: Path, min_time: str) -> list[str]:
    """构造一次完整默认接口基准轮次的 Google Benchmark 参数。"""
    return [
        f"--benchmark_filter={BENCHMARK_FILTER}",
        "--benchmark_format=json",
        "--benchmark_out_format=json",
        f"--benchmark_out={output_path}",
        "--benchmark_repetitions=1",
        f"--benchmark_min_time={min_time}",
    ]


def validate_enumerated_names(names: Sequence[str], scope: str) -> list[str]:
    """校验某一标准及头文件快照枚举出的名称。"""
    counts: Counter[str] = Counter(names)
    duplicates: list[str] = sorted(name for name, count in counts.items() if count > 1)
    invalid: list[str] = sorted(name for name in counts if not name.startswith("BM_Default"))
    if not names or duplicates or invalid:
        raise BenchmarkPairError(
            "data_error",
            f"{scope} 的默认接口基准清单为空、重复或不符合 BM_Default 前缀。",
            scope,
            {"empty": not bool(names), "duplicates": duplicates, "unexpected_names": invalid},
        )
    return sorted(counts)


def verify_expected_sets(name_sets: Mapping[str, Sequence[str]]) -> list[str]:
    """要求双边、双标准枚举得到完全一致的默认接口用例集合。"""
    if not name_sets:
        raise BenchmarkPairError("data_error", "没有可核对的默认接口用例清单。", "expected_names")
    first_key: str = next(iter(name_sets))
    expected: set[str] = set(name_sets[first_key])
    differences: dict[str, dict[str, list[str]]] = {}
    for key, names in name_sets.items():
        actual: set[str] = set(names)
        if actual != expected or len(names) != len(expected):
            differences[key] = {
                "missing": sorted(expected - actual),
                "extra": sorted(actual - expected),
                "duplicates": sorted(name for name, count in Counter(names).items() if count > 1),
            }
    if differences:
        raise BenchmarkPairError(
            "data_error",
            "C++17、C++23 或双边头文件快照的预期用例名称不一致。",
            "expected_names",
            {"reference": first_key, "reference_names": sorted(expected), "differences": differences},
        )
    return sorted(expected)


def validate_raw_round(data: Any, expected_names: Sequence[str], scope: str) -> dict[str, Any]:
    """精确校验一轮的原始 JSON，并区分缺项、重复项和测量错误。"""
    if not isinstance(data, dict) or not isinstance(data.get("benchmarks"), list):
        raise BenchmarkPairError("data_error", f"{scope} 缺少 benchmarks 数组。", scope)
    rows: list[Any] = data["benchmarks"]
    measurement_errors: list[dict[str, str]] = []
    malformed_rows: list[int] = []
    raw_names: list[str] = []
    invalid_measurements: list[str] = []

    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            malformed_rows.append(index)
            continue
        if row.get("error_occurred") or row.get("error_message"):
            name: Any = row.get("name")
            message: Any = row.get("error_message")
            measurement_errors.append({
                "name": name if isinstance(name, str) and name else "<unknown>",
                "message": str(message or "基准报告了测量失败。"),
            })
            continue
        if row.get("aggregate_name") is not None or row.get("run_type", "iteration") != "iteration":
            continue

        name = row.get("name")
        if not isinstance(name, str) or not name:
            invalid_measurements.append(f"第 {index + 1} 条原始测量记录缺少名称。")
            continue
        raw_names.append(name)
        for field in ("cpu_time", "real_time"):
            value: Any = row.get(field)
            if (
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(value)
                or value < 0.0
            ):
                invalid_measurements.append(f"{name} 的 {field} 不是非负有限数。")
        unit: Any = row.get("time_unit")
        if not isinstance(unit, str) or unit not in TIME_UNIT_TO_MS:
            invalid_measurements.append(f"{name} 的 time_unit 无效：{unit!r}。")

    if measurement_errors:
        raise BenchmarkPairError(
            "measurement_error",
            f"{scope} 包含 Google Benchmark 测量错误。",
            scope,
            {"measurement_errors": measurement_errors},
        )
    if malformed_rows or invalid_measurements:
        raise BenchmarkPairError(
            "data_error",
            f"{scope} 包含格式错误或无效测量值。",
            scope,
            {"malformed_rows": malformed_rows, "invalid_measurements": invalid_measurements},
        )

    counts: Counter[str] = Counter(raw_names)
    actual_names: set[str] = set(counts)
    expected_set: set[str] = set(expected_names)
    duplicates: list[dict[str, Any]] = [
        {"name": name, "count": count}
        for name, count in sorted(counts.items())
        if count > 1
    ]
    missing: list[str] = sorted(expected_set - actual_names)
    extra: list[str] = sorted(actual_names - expected_set)
    if duplicates or missing or extra or not raw_names:
        raise BenchmarkPairError(
            "data_error",
            f"{scope} 原始用例未与预期清单完整配对。",
            scope,
            {
                "missing": missing,
                "extra": extra,
                "duplicates": duplicates,
                "actual_unique_names": sorted(actual_names),
                "expected_names": sorted(expected_set),
            },
        )
    return {"raw_run_count": len(raw_names), "names": sorted(actual_names)}


def alternating_sides(round_number: int) -> tuple[str, str]:
    """返回本轮先后顺序，使相邻轮次交换先跑版本。"""
    return SIDES if round_number % 2 == 1 else (SIDES[1], SIDES[0])


def build_one(
    *,
    cmake: str,
    source_root: Path,
    build_directory: Path,
    header_directory: Path,
    compiler: str,
    standard: int,
    target: str,
    generator: str | None,
    configuration: str,
    extra_arguments: Sequence[str],
    dependency_directory: Path,
    artifact_directory: Path,
    runner: CommandRunner,
    environment: Mapping[str, str] | None,
) -> dict[str, Any]:
    """为一个标准和一个快照配置、构建并验证头文件选择。"""
    build_directory.mkdir(parents=True, exist_ok=True)
    artifact_directory.mkdir(parents=True, exist_ok=True)
    configure: list[str] = configure_command(
        cmake,
        source_root,
        build_directory,
        header_directory,
        compiler,
        standard,
        generator,
        configuration,
        extra_arguments,
        dependency_directory,
    )
    configure_result: subprocess.CompletedProcess[str] = run_logged(
        configure,
        artifact_directory / "configure.log",
        runner,
        cwd=source_root,
        env=environment,
        failure_kind="build_error",
        scope=target,
    )
    cache_path: Path = build_directory / "CMakeCache.txt"
    cache: dict[str, str] = read_cmake_cache(cache_path)
    configured_header: str | None = cache.get("RANDX_SAMPLING_HEADER_DIR")
    if configured_header is None or normalized_path_key(Path(configured_header)) != normalized_path_key(header_directory):
        raise BenchmarkPairError(
            "data_error",
            f"CMakeCache 中的头文件覆盖目录与请求不一致：{configured_header!r}。",
            target,
            {
                "requested_header_directory": str(header_directory),
                "configured_header_directory": configured_header,
            },
        )

    build: list[str] = build_command(cmake, build_directory, target, configuration)
    build_result: subprocess.CompletedProcess[str] = run_logged(
        build,
        artifact_directory / "build.log",
        runner,
        cwd=source_root,
        env=environment,
        failure_kind="build_error",
        scope=target,
    )
    binary: Path = find_binary(build_directory, target)

    compile_commands_source: Path = build_directory / "compile_commands.json"
    compile_commands_copy: Path | None = None
    if compile_commands_source.is_file():
        compile_commands_copy = artifact_directory / "compile_commands.json"
        shutil.copy2(compile_commands_source, compile_commands_copy)
        required_header: str = "RandX_Cpp17.hpp" if standard == 17 else "RandX.hpp"
        override_evidence: dict[str, Any] = verify_compile_database_override(
            compile_commands_source,
            header_directory,
            required_header,
            target,
        )
    else:
        required_header = "RandX_Cpp17.hpp" if standard == 17 else "RandX.hpp"
        override_evidence = verify_build_output_override(
            build_result.stdout + "\n" + build_result.stderr,
            build_directory,
            header_directory,
            required_header,
            target,
            compiler,
        )

    cache_copy: Path = artifact_directory / "CMakeCache.txt"
    if cache_path.is_file():
        shutil.copy2(cache_path, cache_copy)
    disassembly: dict[str, Any] = save_disassembly(
        binary,
        artifact_directory / "disassembly.txt",
        compiler,
        runner,
    )
    manifest: dict[str, Any] = {
        "target": target,
        "standard": standard,
        "source_directory": str(source_root),
        "build_directory": str(build_directory),
        "header_directory": str(header_directory),
        "binary": str(binary),
        "generator": cache.get("CMAKE_GENERATOR", generator or "CMake 默认生成器"),
        "configuration": configuration,
        "cmake_environment": cmake_environment(cache),
        "standard_library": standard_library_metadata(compiler, standard, cache, runner),
        "optimization_and_lto_options": {
            "build_type": configuration,
            "common_cxx_flags": cache.get("CMAKE_CXX_FLAGS", ""),
            "configuration_cxx_flags": cache.get(f"CMAKE_CXX_FLAGS_{configuration.upper()}", ""),
            "interprocedural_optimization": cache.get("CMAKE_INTERPROCEDURAL_OPTIMIZATION", ""),
            "configuration_interprocedural_optimization": cache.get(
                f"CMAKE_INTERPROCEDURAL_OPTIMIZATION_{configuration.upper()}", ""
            ),
            "compile_commands": str(compile_commands_copy) if compile_commands_copy else None,
        },
        "override_evidence": override_evidence,
        "configure_command": configure,
        "configure_exit_code": configure_result.returncode,
        "configure_log": str(artifact_directory / "configure.log"),
        "build_command": build,
        "build_exit_code": build_result.returncode,
        "build_log": str(artifact_directory / "build.log"),
        "compile_commands": str(compile_commands_copy) if compile_commands_copy else None,
        "cmake_cache_copy": str(cache_copy) if cache_copy.is_file() else None,
        "code_generation_evidence": disassembly,
    }
    write_json(artifact_directory / "build.json", manifest)
    return manifest


def executable_names(result: subprocess.CompletedProcess[str], scope: str) -> list[str]:
    """读取 Google Benchmark 的用例清单输出。"""
    names: list[str] = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    return validate_enumerated_names(names, scope)


def enumerate_one(
    binary: Path,
    target: str,
    artifact_directory: Path,
    runner: CommandRunner,
    environment: Mapping[str, str] | None,
) -> list[str]:
    """枚举默认接口用例并保留命令输出。"""
    result: subprocess.CompletedProcess[str] = run_logged(
        [str(binary), BENCHMARK_LIST_FLAG, f"--benchmark_filter={BENCHMARK_FILTER}"],
        artifact_directory / "enumerate.log",
        runner,
        env=environment,
        failure_kind="execution_error",
        scope=str(binary),
    )
    return executable_names(result, str(binary))


def write_expected_names(path: Path, names: Sequence[str], name_sets: Mapping[str, Sequence[str]]) -> None:
    """保存共同预期名称以及四个构建各自的清单。"""
    write_json(
        path,
        {
            "filter": BENCHMARK_FILTER,
            "names": sorted(names),
            "sources": {key: sorted(value) for key, value in sorted(name_sets.items())},
        },
    )


def measure_one(
    binary: Path,
    output_path: Path,
    log_path: Path,
    min_time: str,
    runner: CommandRunner,
    environment: Mapping[str, str] | None,
    scope: str,
) -> dict[str, Any]:
    """运行并校验单边单轮原始基准。"""
    arguments: list[str] = [str(binary), *benchmark_arguments(output_path, min_time)]
    run_logged(
        arguments,
        log_path,
        runner,
        env=environment,
        failure_kind="execution_error",
        scope=scope,
    )
    return read_benchmark_json(output_path)


def comparison_document(comparison: ComparisonResult, tolerance: float) -> dict[str, Any]:
    """序列化现有比较器的结构化输出。"""
    return {
        "tolerance": tolerance,
        "regressions": list(comparison.regressions),
        "new_items": list(comparison.new_items),
        "missing_items": list(comparison.missing_items),
        "items": [asdict(item) for item in comparison.items],
    }


def markdown_report(report: Mapping[str, Any]) -> str:
    """创建中文结果报告。"""
    lines: list[str] = [
        "# RandX 默认接口成对基准报告",
        "",
        f"- 运行编号：`{report.get('run_id', '')}`",
        f"- 状态：{report.get('status_text', '')}",
        f"- 编译器：`{report.get('compiler', {}).get('path', '')}`",
        f"- 生成器：`{report.get('generator') or 'CMake 默认生成器'}`",
        f"- 构建配置：`{report.get('configuration', '')}`",
        f"- 基准轮数：{report.get('policy', {}).get('repetitions', '')}",
        f"- 每轮最短测量时间：`{report.get('policy', {}).get('min_time', '')}`",
        f"- 回归容差：{report.get('policy', {}).get('tolerance', '')}",
        f"- CPU：{report.get('environment', {}).get('processor') or report.get('environment', {}).get('machine', '')}",
        f"- 逻辑处理器：{report.get('environment', {}).get('logical_cpu_count', '')}",
        "",
        "## 头文件快照",
        "",
        f"- 基线：`{report.get('header_directories', {}).get('baseline', '')}`",
        f"- 候选：`{report.get('header_directories', {}).get('candidate', '')}`",
        "",
    ]
    standards: Any = report.get("standards", {})
    if isinstance(standards, dict):
        for standard_name in ("cpp23", "cpp17"):
            standard: Any = standards.get(standard_name)
            if not isinstance(standard, dict):
                continue
            lines.extend((f"## {standard.get('standard', standard_name)} 基准", ""))
            expected: Any = standard.get("expected_names", [])
            lines.append(f"共同用例数：{len(expected) if isinstance(expected, list) else 0}。")
            lines.append("")
            comparison: Any = standard.get("comparison")
            if isinstance(comparison, dict):
                lines.extend(
                    (
                        "| 用例 | 基线 (ms) | 候选 (ms) | 变化 | 结果 |",
                        "|---|---:|---:|---:|---|",
                    )
                )
                items: Any = comparison.get("items", [])
                if isinstance(items, list):
                    for item in items:
                        if not isinstance(item, dict):
                            continue
                        change: float = float(item.get("change", 0.0))
                        status: str = "回归" if item.get("regression") else "通过"
                        name: str = str(item.get("name", "")).replace("|", "\\|")
                        lines.append(
                            f"| {name} | {item.get('baseline_ms', 0.0):.6f} "
                            f"| {item.get('current_ms', 0.0):.6f} | {change:+.1%} | {status} |"
                        )
                lines.append("")
            for side in SIDES:
                merged: Any = standard.get("merged_results", {}).get(side)
                if merged:
                    lines.append(f"- {side} 聚合 JSON：`{merged}`")
            lines.append("")

    run_order: Any = report.get("run_order", [])
    if isinstance(run_order, list) and run_order:
        lines.extend(("## 实际测量顺序", ""))
        lines.extend(
            f"- {item.get('standard')} 第 {item.get('round')} 轮：{item.get('side')}"
            for item in run_order
            if isinstance(item, dict)
        )
        lines.append("")

    errors: Any = report.get("errors", [])
    if isinstance(errors, list) and errors:
        lines.extend(("## 数据或执行错误", ""))
        for error in errors:
            if isinstance(error, dict):
                lines.append(f"- `{error.get('kind')}`（{error.get('scope')}）：{error.get('message')}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def finalize_report(report: dict[str, Any], run_directory: Path) -> tuple[int, dict[str, Any]]:
    """确定最终状态并保留 JSON 与中文 Markdown 报告。"""
    if report["errors"]:
        status: str = "error"
        status_text: str = "数据或执行错误"
        exit_code: int = 2
    else:
        all_comparisons: list[Mapping[str, Any]] = [
            standard["comparison"]
            for standard in report["standards"].values()
            if isinstance(standard, dict) and isinstance(standard.get("comparison"), dict)
        ]
        has_regression: bool = any(comparison.get("regressions") for comparison in all_comparisons)
        status = "regression" if has_regression else "passed"
        status_text = "性能回归" if has_regression else "通过"
        exit_code = 1 if has_regression else 0
    report["status"] = status
    report["status_text"] = status_text
    report["exit_code"] = exit_code
    report["report_json"] = str(run_directory / "report.json")
    report["report_markdown"] = str(run_directory / "report.md")
    write_json(run_directory / "report.json", report)
    (run_directory / "report.md").write_text(markdown_report(report), encoding="utf-8")
    return exit_code, report


def add_error(report: dict[str, Any], error: BenchmarkPairError) -> None:
    """将有分类的错误写入报告。"""
    report["errors"].append(
        {
            "kind": error.kind,
            "scope": error.scope,
            "message": str(error),
            "details": error.details,
        }
    )


def _run_pair(
    arguments: argparse.Namespace,
    runner: CommandRunner,
) -> tuple[int, dict[str, Any]]:
    """执行完整构建、用例配对、交错测量及比较流程。"""
    run_id: str = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output_root: Path = arguments.output_dir.expanduser().resolve()
    build_root: Path = arguments.build_root.expanduser().resolve() / run_id
    run_directory: Path = output_root / run_id
    run_directory.mkdir(parents=True, exist_ok=True)
    (build_root / "_deps").mkdir(parents=True, exist_ok=True)
    report: dict[str, Any] = {
        "schema_version": 1,
        "run_id": run_id,
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "status": "running",
        "status_text": "运行中",
        "exit_code": None,
        "source_directory": str(PROJECT_ROOT),
        "build_root": str(build_root),
        "output_directory": str(run_directory),
        "header_directories": {},
        "compiler": {},
        "cmake": {},
        "generator": arguments.generator,
        "configuration": arguments.configuration,
        "policy_path": str(arguments.policy.expanduser().resolve()),
        "policy": {},
        "environment": environment_metadata(),
        "builds": {},
        "expected_names": [],
        "standards": {},
        "run_order": [],
        "errors": [],
    }

    try:
        if arguments.baseline_cmake_arg != arguments.candidate_cmake_arg:
            raise BenchmarkPairError(
                "data_error", "基线与候选的额外 CMake 参数必须一致。", "configuration"
            )
        if not (PROJECT_ROOT / "CMakeLists.txt").is_file():
            raise BenchmarkPairError("data_error", f"项目根目录缺少 CMakeLists.txt：{PROJECT_ROOT}", "source")
        compiler: str = resolve_executable(arguments.compiler, "C++ 编译器")
        cmake: str = resolve_executable(arguments.cmake, "CMake")
        baseline_headers: Path = validate_header_directory(arguments.baseline_header_dir, "baseline")
        candidate_headers: Path = validate_header_directory(arguments.candidate_header_dir, "candidate")
        policy: dict[str, Any] = load_policy(arguments.policy.expanduser().resolve())
        report["compiler"] = compiler_metadata(compiler, runner)
        report["cmake"] = {"path": cmake}
        report["header_directories"] = {
            "baseline": str(baseline_headers),
            "candidate": str(candidate_headers),
        }
        report["policy"] = policy
        dependency_directory: Path = build_root / "_deps"
        build_environment: dict[str, str] | None = None
        if os.name == "nt":
            sibling_make: Path = Path(compiler).parent / "mingw32-make.exe"
            if sibling_make.is_file():
                build_environment = os.environ.copy()
                current_path: str = build_environment.get("PATH", "")
                build_environment["PATH"] = str(sibling_make.parent) + os.pathsep + current_path

        header_by_side: dict[str, Path] = {
            "baseline": baseline_headers,
            "candidate": candidate_headers,
        }
        extra_by_side: dict[str, list[str]] = {
            "baseline": list(arguments.baseline_cmake_arg),
            "candidate": list(arguments.candidate_cmake_arg),
        }
        enumerated: dict[str, dict[str, list[str]]] = {name: {} for name, _, _ in STANDARD_CONFIGURATIONS}
        binary_by_key: dict[str, Path] = {}

        for standard_name, standard, target in STANDARD_CONFIGURATIONS:
            report["standards"][standard_name] = {
                "standard": f"C++{standard}",
                "target": target,
                "expected_names": [],
                "merged_results": {},
                "comparison": None,
            }
            for side in SIDES:
                key: str = f"{standard_name}/{side}"
                build_directory: Path = build_root / standard_name / side
                artifact_directory: Path = run_directory / "builds" / standard_name / side
                manifest: dict[str, Any] = build_one(
                    cmake=cmake,
                    source_root=PROJECT_ROOT,
                    build_directory=build_directory,
                    header_directory=header_by_side[side],
                    compiler=compiler,
                    standard=standard,
                    target=target,
                    generator=arguments.generator,
                    configuration=arguments.configuration,
                    extra_arguments=extra_by_side[side],
                    dependency_directory=dependency_directory,
                    artifact_directory=artifact_directory,
                    runner=runner,
                    environment=build_environment,
                )
                binary: Path = Path(manifest["binary"])
                binary_by_key[key] = binary
                report["builds"][key] = manifest
                names: list[str] = enumerate_one(
                    binary,
                    target,
                    artifact_directory,
                    runner,
                    build_environment,
                )
                enumerated[standard_name][side] = names
                write_json(artifact_directory / "expected-names.json", {"names": names})

        name_sets: dict[str, list[str]] = {
            f"{standard_name}/{side}": enumerated[standard_name][side]
            for standard_name, _, _ in STANDARD_CONFIGURATIONS
            for side in SIDES
        }
        expected_names: list[str] = verify_expected_sets(name_sets)
        report["expected_names"] = expected_names
        for standard_name, _, _ in STANDARD_CONFIGURATIONS:
            report["standards"][standard_name]["expected_names"] = expected_names
            write_expected_names(
                run_directory / standard_name / "expected.json",
                expected_names,
                {
                    side: enumerated[standard_name][side]
                    for side in SIDES
                },
            )

        for standard_name, standard, _ in STANDARD_CONFIGURATIONS:
            standard_report: dict[str, Any] = report["standards"][standard_name]
            raw_paths: dict[str, list[Path]] = {side: [] for side in SIDES}
            for round_number in range(1, policy["repetitions"] + 1):
                for side in alternating_sides(round_number):
                    binary = binary_by_key[f"{standard_name}/{side}"]
                    raw_path: Path = run_directory / standard_name / "rounds" / f"{side}-{round_number}.json"
                    raw_path.parent.mkdir(parents=True, exist_ok=True)
                    log_path: Path = run_directory / standard_name / "logs" / f"{side}-{round_number}.log"
                    scope: str = f"{standard_name} 第 {round_number} 轮 {side}"
                    result: dict[str, Any] = measure_one(
                        binary,
                        raw_path,
                        log_path,
                        policy["min_time"],
                        runner,
                        build_environment,
                        scope,
                    )
                    validation: dict[str, Any] = validate_raw_round(result, expected_names, scope)
                    raw_paths[side].append(raw_path)
                    report["run_order"].append(
                        {
                            "standard": f"C++{standard}",
                            "round": round_number,
                            "side": side,
                            "raw_json": str(raw_path),
                            "measurement_count": validation["raw_run_count"],
                        }
                    )

            parsed_results: dict[str, dict[str, dict[str, Any]]] = {}
            for side in SIDES:
                raw_results: list[dict[str, Any]] = [read_json(path) for path in raw_paths[side]]
                try:
                    merged: dict[str, Any] = merge_results(raw_results)
                    parsed: dict[str, dict[str, Any]] = parse_results(merged, f"{standard_name}/{side}")
                except (ValueError, KeyError, BenchmarkDataError) as error:
                    raise BenchmarkPairError(
                        "data_error",
                        f"{standard_name} {side} 原始轮次无法按现有规则聚合：{error}",
                        f"{standard_name}/{side}",
                    ) from error
                if set(parsed) != set(expected_names):
                    raise BenchmarkPairError(
                        "data_error",
                        f"{standard_name} {side} 聚合后的用例集合与预期清单不一致。",
                        f"{standard_name}/{side}",
                        {
                            "missing": sorted(set(expected_names) - set(parsed)),
                            "extra": sorted(set(parsed) - set(expected_names)),
                        },
                    )
                merged_path: Path = run_directory / standard_name / f"{side}-merged.json"
                write_json(merged_path, merged)
                standard_report["merged_results"][side] = str(merged_path)
                parsed_results[side] = parsed

            baseline_parsed: dict[str, dict[str, Any]] = parsed_results["baseline"]
            candidate_parsed: dict[str, dict[str, Any]] = parsed_results["candidate"]
            try:
                comparison: ComparisonResult = compare_results(
                    candidate_parsed,
                    baseline_parsed,
                    policy["tolerance"],
                )
            except BenchmarkDataError as error:
                raise BenchmarkPairError(
                    "data_error",
                    f"{standard_name} 比较结果无效：{error}",
                    standard_name,
                ) from error
            comparison_data: dict[str, Any] = comparison_document(comparison, policy["tolerance"])
            comparison_path: Path = run_directory / standard_name / "comparison.json"
            write_json(comparison_path, comparison_data)
            standard_report["comparison"] = comparison_data
            standard_report["comparison_json"] = str(comparison_path)
    except BenchmarkPairError as error:
        add_error(report, error)
    except (OSError, ValueError, TypeError, KeyError, subprocess.SubprocessError) as error:
        add_error(
            report,
            BenchmarkPairError(
                "execution_error",
                f"基准流程无法完成：{type(error).__name__}: {error}",
                "run",
            ),
        )

    return finalize_report(report, run_directory)


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    """解析成对比较入口参数。"""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-header-dir", type=Path, required=True, help="修复前头文件快照目录")
    parser.add_argument("--candidate-header-dir", type=Path, required=True, help="修复后头文件快照目录")
    parser.add_argument("--compiler", required=True, help="C++ 编译器可执行文件或 PATH 中的名称")
    parser.add_argument("--cmake", default="cmake", help="CMake 可执行文件或 PATH 中的名称")
    parser.add_argument("--build-root", type=Path, required=True, help="构建产物根目录")
    parser.add_argument("--output-dir", type=Path, required=True, help="结果报告和原始轮次根目录")
    parser.add_argument("--policy", type=Path, required=True, help="benchmark_policy.json 策略路径")
    parser.add_argument("--generator", help="可选 CMake 生成器名称")
    parser.add_argument("--configuration", default="Release", help="CMake 构建配置，默认 Release")
    parser.add_argument(
        "--baseline-cmake-arg",
        action="append",
        default=[],
        help="追加一个仅用于基线构建的 CMake 参数，可重复传入",
    )
    parser.add_argument(
        "--candidate-cmake-arg",
        action="append",
        default=[],
        help="追加一个仅用于候选构建的 CMake 参数，可重复传入",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    """运行成对基准并返回 0、1 或 2。"""
    arguments: argparse.Namespace = parse_arguments(argv)
    try:
        exit_code, report = _run_pair(arguments, subprocess.run)
    except (OSError, ValueError) as error:
        print(f"基准入口失败：{error}", file=sys.stderr)
        return 2
    print(f"结果：{report['status_text']}；中文报告：{report['report_markdown']}")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
