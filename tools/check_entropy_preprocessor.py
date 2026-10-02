#!/usr/bin/env python3
"""核对生产预处理结果保留原生熵源直连。"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Sequence


HOOK_SYMBOLS: tuple[str, ...] = (
    "RANDX_ENABLE_ENTROPY_TEST_HOOKS",
    "EntropyTestHook",
    "entropyTestHook",
)
SUPPORTED_HEADERS: frozenset[str] = frozenset({"RandX.hpp", "RandX_Cpp17.hpp"})
SUPPORTED_STANDARDS: frozenset[str] = frozenset({"c++17", "c++20", "c++23"})
OS_ENTROPY_FUNCTION: re.Pattern[str] = re.compile(
    r"inline\s+bool\s+GetOsEntropyBytes\s*\(\s*void\s*\*\s*buf\s*,"
    r"\s*std::size_t\s+n\s*\)\s+noexcept\s*\{(?P<body>.*?)\}",
    re.DOTALL,
)
NATIVE_READER_DISPATCH: re.Pattern[str] = re.compile(
    r"NativeOsEntropyReader\s+(?P<reader>[A-Za-z_]\w*)\s*;\s*"
    r"return\s+FillOsEntropy\s*\(\s*(?P=reader)\s*,\s*buf\s*,\s*n\s*\)\s*;"
)


class PreprocessorError(Exception):
    """生产预处理命令或熵源直连检查失败。"""


def validate_preprocessed_source(source: str, source_name: str) -> None:
    present_hooks: list[str] = [symbol for symbol in HOOK_SYMBOLS if symbol in source]
    if present_hooks:
        raise PreprocessorError(f"生产预处理结果包含测试 hook 标识：{present_hooks!r}")

    function_match: re.Match[str] | None = OS_ENTROPY_FUNCTION.search(source)
    if function_match is None:
        raise PreprocessorError(f"{source_name} 预处理结果缺少 GetOsEntropyBytes 函数体。")
    normalized_body: str = re.sub(r"\s+", " ", function_match.group("body"))
    if NATIVE_READER_DISPATCH.search(normalized_body) is None:
        raise PreprocessorError(
            f"{source_name} 的 GetOsEntropyBytes 未直接使用 NativeOsEntropyReader。"
        )


def _preprocess_command(compiler: str, standard: str, root: Path) -> list[str]:
    return [
        compiler,
        f"-std={standard}",
        "-E",
        "-P",
        "-x",
        "c++",
        "-I",
        str(root),
        "-",
    ]


def check_header(
    compiler: str, standard: str, header: str, root: Path, output: Path
) -> dict[str, Any]:
    if header not in SUPPORTED_HEADERS:
        raise PreprocessorError(f"不支持的 RandX 头文件：{header}")
    if standard not in SUPPORTED_STANDARDS:
        raise PreprocessorError(f"不支持的语言标准：{standard}")
    compatible_standards: frozenset[str] = (
        frozenset({"c++23"})
        if header == "RandX.hpp"
        else frozenset({"c++17", "c++20"})
    )
    if standard not in compatible_standards:
        raise PreprocessorError(f"头文件与语言标准不匹配：{header} / {standard}")

    command: list[str] = _preprocess_command(compiler, standard, root)
    include_source: str = f'#include "{header}"\n'
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command,
        cwd=root,
        input=include_source,
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        details: str = result.stderr.strip() or result.stdout.strip()
        raise PreprocessorError(
            f"预处理命令失败，退出码 {result.returncode}：{command!r}\n{details}"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(result.stdout, encoding="utf-8")
    validate_preprocessed_source(result.stdout, header)
    report: dict[str, Any] = {
        "compiler": compiler,
        "standard": standard,
        "header": header,
        "command": command,
        "hook_symbols_absent": list(HOOK_SYMBOLS),
        "native_reader_dispatch_present": True,
        "preprocessed_output": str(output),
    }
    report_path: Path = output.with_suffix(".report.json")
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def _build_parser() -> argparse.ArgumentParser:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="检查生产预处理结果中测试 hook 已移除且原生熵 reader 直连。"
    )
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--standard", choices=sorted(SUPPORTED_STANDARDS), required=True)
    parser.add_argument("--header", choices=sorted(SUPPORTED_HEADERS), required=True)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser: argparse.ArgumentParser = _build_parser()
    arguments: argparse.Namespace = parser.parse_args(argv)
    try:
        report: dict[str, Any] = check_header(
            arguments.compiler,
            arguments.standard,
            arguments.header,
            arguments.root,
            arguments.output,
        )
    except (OSError, PreprocessorError) as error:
        print(f"生产预处理检查失败：{error}", file=sys.stderr)
        return 1
    print(
        f"{report['header']} 生产预处理通过：未包含 hook 标识，GetOsEntropyBytes 直连原生 reader。"
    )
    print(f"预处理证据：{report['preprocessed_output']}")
    print(f"命令记录：{arguments.output.with_suffix('.report.json')}")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
