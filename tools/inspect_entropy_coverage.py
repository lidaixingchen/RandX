#!/usr/bin/env python3
"""报告熵相关 LCOV 分支并比较追踪文件结构。"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Sequence


ENTROPY_FUNCTION_MARKERS: tuple[str, ...] = (
    "ChaCha20",
    "Entropy",
    "RandomSeed",
    "ScopedWiper",
    "SecureSeed",
    "SecureWipe",
)


class CoverageFormatError(Exception):
    """LCOV 格式或熵分支报告无效。"""


@dataclass(frozen=True)
class FunctionCoverage:
    start_line: int
    name: str
    calls: int
    end_line: int | None = None
    aliases: tuple[str, ...] = ()


@dataclass(frozen=True)
class BranchCoverage:
    line: int
    block: str
    branch: str
    taken: int | None


@dataclass
class SourceCoverage:
    source: str
    lines: dict[int, int] = field(default_factory=dict)
    branches: list[BranchCoverage] = field(default_factory=list)
    functions: list[FunctionCoverage] = field(default_factory=list)


def parse_lcov(path: Path) -> dict[str, SourceCoverage]:
    records: dict[str, SourceCoverage] = {}
    current: SourceCoverage | None = None
    function_starts: list[tuple[int, int | None, str]] = []
    function_calls: dict[str, int] = {}
    indexed_functions: dict[int, tuple[int, int | None]] = {}
    indexed_aliases: list[tuple[int, int, str]] = []

    def finish_record() -> None:
        nonlocal current, function_starts, function_calls, indexed_functions, indexed_aliases
        if current is None:
            raise CoverageFormatError(f"LCOV 记录缺少 SF：{path}")
        if current.source in records:
            raise CoverageFormatError(f"LCOV 含有重复来源记录：{current.source}")
        # LCOV 2.2 起用索引关联函数范围与别名；同一范围的分支只计一次。
        groups: dict[tuple[int, int | None], dict[str, int]] = {}
        for start_line, end_line, name in function_starts:
            groups.setdefault((start_line, end_line), {})[name] = function_calls.get(name, 0)
        for index, calls, name in indexed_aliases:
            if index not in indexed_functions:
                raise CoverageFormatError(f"LCOV 函数别名缺少范围：{index}")
            groups.setdefault(indexed_functions[index], {})[name] = calls
        for (start_line, end_line), aliases in groups.items():
            names: tuple[str, ...] = tuple(sorted(aliases))
            current.functions.append(
                FunctionCoverage(start_line, names[0], sum(aliases.values()), end_line, names)
            )
        current.functions.sort(key=lambda function: (function.start_line, function.name))
        records[current.source] = current
        current = None
        function_starts = []
        function_calls = {}
        indexed_functions = {}
        indexed_aliases = []

    try:
        lines: list[str] = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CoverageFormatError(f"无法读取 LCOV 报告 {path}：{error}") from error

    for raw_line in lines:
        if raw_line.startswith("SF:"):
            if current is not None:
                raise CoverageFormatError(f"LCOV 记录缺少 end_of_record：{path}")
            current = SourceCoverage(source=raw_line[3:])
            continue
        if raw_line == "end_of_record":
            finish_record()
            continue
        if current is None:
            continue

        if raw_line.startswith("FN:"):
            line_number, separator, name = raw_line[3:].partition(",")
            if not separator:
                raise CoverageFormatError(f"LCOV 函数记录格式错误：{raw_line}")
            end_text, end_separator, remaining_name = name.partition(",")
            end_line: int | None = None
            if end_separator and end_text.isdigit():
                end_line, name = int(end_text), remaining_name
            function_starts.append((int(line_number), end_line, name))
        elif raw_line.startswith("FNL:"):
            fields: list[str] = raw_line[4:].split(",")
            if len(fields) not in (2, 3):
                raise CoverageFormatError(f"LCOV 函数范围记录格式错误：{raw_line}")
            indexed_functions[int(fields[0])] = (
                int(fields[1]), int(fields[2]) if len(fields) == 3 else None
            )
        elif raw_line.startswith("FNA:"):
            fields = raw_line[4:].split(",", 2)
            if len(fields) != 3:
                raise CoverageFormatError(f"LCOV 函数别名记录格式错误：{raw_line}")
            indexed_aliases.append((int(fields[0]), int(fields[1]), fields[2]))
        elif raw_line.startswith("FNDA:"):
            call_count, separator, name = raw_line[5:].partition(",")
            if not separator:
                raise CoverageFormatError(f"LCOV 函数次数记录格式错误：{raw_line}")
            function_calls[name] = int(call_count)
        elif raw_line.startswith("DA:"):
            line_number, separator, count_text = raw_line[3:].partition(",")
            if not separator:
                raise CoverageFormatError(f"LCOV 行记录格式错误：{raw_line}")
            current.lines[int(line_number)] = int(count_text.partition(",")[0])
        elif raw_line.startswith("BRDA:"):
            fields: list[str] = raw_line[5:].split(",")
            if len(fields) != 4:
                raise CoverageFormatError(f"LCOV 分支记录格式错误：{raw_line}")
            taken: int | None = None if fields[3] == "-" else int(fields[3])
            current.branches.append(
                BranchCoverage(int(fields[0]), fields[1], fields[2], taken)
            )

    if current is not None:
        raise CoverageFormatError(f"LCOV 记录缺少 end_of_record：{path}")
    if not records:
        raise CoverageFormatError(f"LCOV 报告没有来源记录：{path}")
    return records


def _branch_sites(record: SourceCoverage) -> set[tuple[int, str, str]]:
    return {(branch.line, branch.block, branch.branch) for branch in record.branches}


def _function_sites(record: SourceCoverage) -> set[tuple[int, int | None, tuple[str, ...]]]:
    return {
        (function.start_line, function.end_line, function.aliases)
        for function in record.functions
    }


def compare_layouts(
    production: dict[str, SourceCoverage], fault: dict[str, SourceCoverage]
) -> dict[str, Any]:
    production_sources: set[str] = set(production)
    fault_sources: set[str] = set(fault)
    source_differences: list[dict[str, Any]] = []
    for source in sorted(production_sources | fault_sources):
        production_record: SourceCoverage | None = production.get(source)
        fault_record: SourceCoverage | None = fault.get(source)
        differences: dict[str, Any] = {}
        if production_record is None:
            differences["missing_from_production"] = True
        elif fault_record is None:
            differences["missing_from_fault"] = True
        else:
            production_lines: set[int] = set(production_record.lines)
            fault_lines: set[int] = set(fault_record.lines)
            production_functions = _function_sites(production_record)
            fault_functions = _function_sites(fault_record)
            production_branches: set[tuple[int, str, str]] = _branch_sites(production_record)
            fault_branches: set[tuple[int, str, str]] = _branch_sites(fault_record)
            if production_lines != fault_lines:
                differences["line_sites"] = {
                    "production_only": sorted(production_lines - fault_lines),
                    "fault_only": sorted(fault_lines - production_lines),
                }
            if production_functions != fault_functions:
                differences["functions"] = {
                    "production_only": sorted(production_functions - fault_functions, key=repr),
                    "fault_only": sorted(fault_functions - production_functions, key=repr),
                }
            if production_branches != fault_branches:
                differences["branch_sites"] = {
                    "production_only": sorted(production_branches - fault_branches),
                    "fault_only": sorted(fault_branches - production_branches),
                }
        source_differences.append({"source": source, "compatible": not differences, **differences})

    compatible: bool = all(item["compatible"] for item in source_differences)
    return {
        "production_sources": sorted(production_sources),
        "fault_sources": sorted(fault_sources),
        "compatible": compatible,
        "sources": source_differences,
    }


def _function_for_branch(
    branch: BranchCoverage, functions: Sequence[FunctionCoverage]
) -> FunctionCoverage | None:
    function_starts: list[FunctionCoverage] = sorted(
        (
            function for function in functions
            if function.start_line <= branch.line
            and (function.end_line is None or branch.line <= function.end_line)
        ),
        key=lambda function: function.start_line,
    )
    if not function_starts:
        return None
    return function_starts[-1]


def entropy_branch_report(records: dict[str, SourceCoverage]) -> str:
    report_lines: list[str] = ["故障配置中的熵相关分支覆盖"]
    branch_count: int = 0
    for source, record in sorted(records.items()):
        relevant_functions: list[FunctionCoverage] = [
            function
            for function in record.functions
            if any(
                marker.casefold() in name.casefold()
                for name in function.aliases or (function.name,)
                for marker in ENTROPY_FUNCTION_MARKERS
            )
        ]
        for function in relevant_functions:
            owned_branches: list[BranchCoverage] = [
                branch
                for branch in record.branches
                if _function_for_branch(branch, record.functions) == function
            ]
            if not owned_branches:
                continue
            branch_count += len(owned_branches)
            covered: int = sum(
                1 for branch in owned_branches if branch.taken is not None and branch.taken > 0
            )
            report_lines.append(
                f"{source}: {' | '.join(function.aliases or (function.name,))} @ {function.start_line}; "
                f"calls={function.calls}; branches={covered}/{len(owned_branches)}"
            )
            for branch in owned_branches:
                taken_text: str = "-" if branch.taken is None else str(branch.taken)
                report_lines.append(
                    f"  line {branch.line}, block {branch.block}, branch {branch.branch}: {taken_text}"
                )
    if branch_count == 0:
        raise CoverageFormatError("熵相关函数没有可展示的分支记录。")
    return "\n".join(report_lines) + "\n"


def _build_parser() -> argparse.ArgumentParser:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="列出故障配置的熵函数分支，并比较故障/生产 LCOV 结构。"
    )
    parser.add_argument("--production-report", type=Path, required=True)
    parser.add_argument("--fault-report", type=Path, required=True)
    parser.add_argument("--entropy-output", type=Path, required=True)
    parser.add_argument("--compatibility-output", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser: argparse.ArgumentParser = _build_parser()
    arguments: argparse.Namespace = parser.parse_args(argv)
    try:
        production: dict[str, SourceCoverage] = parse_lcov(arguments.production_report)
        fault: dict[str, SourceCoverage] = parse_lcov(arguments.fault_report)
        branch_text: str = entropy_branch_report(fault)
        compatibility: dict[str, Any] = compare_layouts(production, fault)
        arguments.entropy_output.parent.mkdir(parents=True, exist_ok=True)
        arguments.entropy_output.write_text(branch_text, encoding="utf-8")
        arguments.compatibility_output.parent.mkdir(parents=True, exist_ok=True)
        arguments.compatibility_output.write_text(
            json.dumps(compatibility, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    except (OSError, CoverageFormatError) as error:
        print(f"熵覆盖率报告失败：{error}", file=sys.stderr)
        return 1

    if compatibility["compatible"]:
        print("生产与故障覆盖率结构兼容，可作为补充合并报告。")
        return 0
    print("生产与故障覆盖率结构不同，保留独立报告且不合并。")
    return 3


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
