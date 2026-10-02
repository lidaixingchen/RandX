"""在显式隔离目录演练共享头文件来源的诊断映射。"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Sequence

import generate_headers


GEOMETRIC_SIGNATURE: str = "inline T RandGeometric(Engine& engine, double p = 0.5)"
GEOMETRIC_BODY_SOURCE: str = "common/distributions/geometric_body.inc"
DIAGNOSTIC_SYMBOL: str = "RandXHeaderSourceDiagnostic"
DENOMINATOR_LINE: bytes = b"const double denom = -std::log1p(-p);"
DIAGNOSTIC_DENOMINATOR_LINE: bytes = (
    b"const double denom = RandXHeaderSourceDiagnostic(p);"
)
TEXT_ENCODING: str = "utf-8"
TARGET_STANDARDS: dict[str, str] = {"cpp17": "17", "cpp23": "23"}


class MaintenanceCheckError(Exception):
    """维护演练无法完成或诊断映射不符合预期。"""


def read_source_lines(source_root: Path, relative_path: str) -> list[str]:
    """读取与生成器相同的 UTF-8 逻辑行。"""
    source_path: Path = source_root.joinpath(*PurePosixPath(relative_path).parts)
    source_text: str = source_path.read_bytes().decode(TEXT_ENCODING)
    normalized_text: str = source_text.replace("\r\n", "\n").replace("\r", "\n")
    return normalized_text.splitlines()


def mapped_source_line(
    result: generate_headers.GeneratedTarget,
    target_line: int,
) -> tuple[str, int]:
    """从生成目标行反查唯一来源文件及行号。"""
    matches: list[tuple[str, int]] = [
        (
            span.source,
            span.source_start + target_line - span.target_start,
        )
        for span in result.mappings
        if span.target_start <= target_line <= span.target_end
    ]
    if len(matches) != 1:
        raise MaintenanceCheckError(
            f"{result.target.name} 的目标第 {target_line} 行应有唯一来源，实际为 {matches!r}"
        )
    return matches[0]


def mapped_target_line(
    result: generate_headers.GeneratedTarget,
    source: str,
    source_line: int,
) -> int:
    """从来源行反查唯一生成目标行。"""
    matches: list[int] = [
        span.target_start + source_line - span.source_start
        for span in result.mappings
        if span.source == source and span.source_start <= source_line <= span.source_end
    ]
    if len(matches) != 1:
        raise MaintenanceCheckError(
            f"{result.target.name} 的来源 {source}:{source_line} 应映射到一行，实际为 {matches!r}"
        )
    return matches[0]


def validate_geometric_declaration(
    result: generate_headers.GeneratedTarget,
    source_root: Path,
) -> tuple[int, int, str, int, int]:
    """验证指定引擎重载完整声明的正向与反向映射。"""
    source: str = f"{result.target.name}/convenience.inc"
    source_lines: list[str] = read_source_lines(source_root, source)
    signature_lines: list[int] = [
        line_number
        for line_number, line in enumerate(source_lines, start=1)
        if line.strip() == GEOMETRIC_SIGNATURE
    ]
    if len(signature_lines) != 1:
        raise MaintenanceCheckError(
            f"{source} 中指定引擎重载签名应出现一次，实际为 {len(signature_lines)} 次"
        )

    signature_line: int = signature_lines[0]
    template_lines: list[int] = [
        line_number
        for line_number, line in enumerate(source_lines[: signature_line - 1], start=1)
        if line.strip().startswith("template <")
    ]
    if not template_lines:
        raise MaintenanceCheckError(f"{source} 中指定引擎重载缺少模板声明")
    declaration_start: int = template_lines[-1]
    declaration_lines: list[str] = source_lines[declaration_start - 1 : signature_line]
    if declaration_lines[-2].strip() != "[[nodiscard]]":
        raise MaintenanceCheckError(f"{source} 中指定引擎重载声明缺少 [[nodiscard]]")

    generated_lines: list[str] = result.content.splitlines()
    target_lines: list[int] = []
    for source_line in range(declaration_start, signature_line + 1):
        target_line: int = mapped_target_line(result, source, source_line)
        if mapped_source_line(result, target_line) != (source, source_line):
            raise MaintenanceCheckError(
                f"{result.target.name} 指定引擎重载声明的反向映射不一致"
            )
        if generated_lines[target_line - 1] != source_lines[source_line - 1]:
            raise MaintenanceCheckError(
                f"{result.target.name} 指定引擎重载声明与来源行文本不一致"
            )
        target_lines.append(target_line)

    if not target_lines or target_lines != list(range(target_lines[0], target_lines[-1] + 1)):
        raise MaintenanceCheckError(
            f"{result.target.name} 指定引擎重载声明在产物中不是连续行"
        )
    declaration_text: str = "\n".join(declaration_lines)
    if "\n".join(generated_lines[target_lines[0] - 1 : target_lines[-1]]) != declaration_text:
        raise MaintenanceCheckError(
            f"{result.target.name} 指定引擎重载声明原文未出现在产物中"
        )
    return target_lines[0], target_lines[-1], source, declaration_start, signature_line


def diagnostic_header_line(
    diagnostic_text: str,
    header_name: str,
) -> int:
    """读取编译器指出的头文件错误行。"""
    diagnostic_pattern: re.Pattern[str] = re.compile(
        rf"(?:^|[\\/])?{re.escape(header_name)}:(?P<line>[0-9]+):[0-9]+:\s*(?:fatal )?error:",
        re.MULTILINE,
    )
    matches: list[re.Match[str]] = list(diagnostic_pattern.finditer(diagnostic_text))
    if len(matches) != 1:
        raise MaintenanceCheckError(
            f"编译器应为 {header_name} 报告一处错误行，实际匹配 {len(matches)} 处：\n"
            f"{diagnostic_text.strip()}"
        )
    return int(matches[0].group("line"))


def run_diagnostic(
    compiler: str,
    result: generate_headers.GeneratedTarget,
    source_root: Path,
    generated_directory: Path,
) -> tuple[int, str, int]:
    """编译一个临时头文件并验证错误行映射到共享正文。"""
    standard: str | None = TARGET_STANDARDS.get(result.target.name)
    if standard is None:
        raise MaintenanceCheckError(f"不支持的头文件目标：{result.target.name}")

    header_name: str = Path(result.target.output).name
    header_path: Path = generated_directory / header_name
    header_path.write_bytes(result.content.encode(TEXT_ENCODING))
    translation_unit: Path = generated_directory / f"{result.target.name}.cpp"
    translation_unit.write_text(f'#include "{header_name}"\n', encoding=TEXT_ENCODING)
    command: list[str] = [
        compiler,
        f"-std=c++{standard}",
        "-fsyntax-only",
        "-I",
        str(generated_directory),
        str(translation_unit),
    ]
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(
        command,
        check=False,
        capture_output=True,
    )
    diagnostic_text: str = (completed.stdout + completed.stderr).decode(
        TEXT_ENCODING, errors="replace"
    )
    if completed.returncode == 0:
        raise MaintenanceCheckError(
            f"C++{standard} 编译器接受了故意错误的 {header_name}"
        )
    if DIAGNOSTIC_SYMBOL not in diagnostic_text:
        raise MaintenanceCheckError(
            f"C++{standard} 编译失败原因不是预期测试符号：\n{diagnostic_text.strip()}"
        )

    target_line: int = diagnostic_header_line(diagnostic_text, header_name)
    generated_lines: list[str] = result.content.splitlines()
    changed_lines: list[int] = [
        line_number
        for line_number, line in enumerate(generated_lines, start=1)
        if DIAGNOSTIC_SYMBOL in line
    ]
    if len(changed_lines) != 1 or target_line != changed_lines[0]:
        raise MaintenanceCheckError(
            f"C++{standard} 实际诊断第 {target_line} 行与唯一故意错误行 {changed_lines!r} 不一致"
        )

    source, source_line = mapped_source_line(result, target_line)
    if source != GEOMETRIC_BODY_SOURCE:
        raise MaintenanceCheckError(
            f"C++{standard} 错误行映射到 {source}:{source_line}，预期为共享几何分布正文"
        )
    copied_source_lines: list[str] = read_source_lines(source_root, source)
    if DIAGNOSTIC_SYMBOL not in copied_source_lines[source_line - 1]:
        raise MaintenanceCheckError(
            f"C++{standard} 错误行映射到的来源行未包含测试符号：{source}:{source_line}"
        )
    return target_line, source, source_line


def run_maintenance_check(compiler: str, artifact_directory: Path) -> None:
    """在新建的演练目录中修改、诊断、恢复并比较两个头文件。"""
    artifact_root: Path = artifact_directory.expanduser().resolve()
    artifact_root.mkdir(parents=True, exist_ok=True)
    drill_directory: Path = artifact_root / "header-maintenance-drill"
    if drill_directory.exists():
        raise MaintenanceCheckError(
            f"演练目录已存在，未覆盖其中内容：{drill_directory}"
        )
    drill_directory.mkdir()

    source_copy: Path = drill_directory / "src" / "header_sources"
    shutil.copytree(generate_headers.SOURCE_ROOT, source_copy)
    config_copy: Path = source_copy / "targets.json"
    original_results: tuple[generate_headers.GeneratedTarget, ...] = (
        generate_headers.generate_targets(config_copy)
    )
    original_bytes: dict[str, bytes] = {
        result.target.name: result.content.encode(TEXT_ENCODING)
        for result in original_results
    }
    body_copy: Path = source_copy / "common" / "distributions" / "geometric_body.inc"
    body_before: bytes = body_copy.read_bytes()
    if body_before.count(DENOMINATOR_LINE) != 1:
        raise MaintenanceCheckError(
            "复制来源中的 RandGeometric denom 表达式应恰好出现一次"
        )
    body_modified: bytes = body_before.replace(
        DENOMINATOR_LINE,
        DIAGNOSTIC_DENOMINATOR_LINE,
        1,
    )
    if body_modified == body_before:
        raise MaintenanceCheckError("共享几何分布正文未产生演练修改")

    generated_directory: Path = drill_directory / "generated-mutated"
    generated_directory.mkdir()
    drill_error: Exception | None = None

    try:
        body_copy.write_bytes(body_modified)
        modified_results: tuple[generate_headers.GeneratedTarget, ...] = (
            generate_headers.generate_targets(config_copy)
        )
        modified_by_name: dict[str, generate_headers.GeneratedTarget] = {
            result.target.name: result for result in modified_results
        }

        if set(modified_by_name) != set(TARGET_STANDARDS):
            raise MaintenanceCheckError(
                f"演练目标应为 {sorted(TARGET_STANDARDS)!r}，实际为 {sorted(modified_by_name)!r}"
            )

        for result in modified_results:
            declaration_start: int
            declaration_end: int
            declaration_source: str
            source_declaration_start: int
            source_declaration_end: int
            (
                declaration_start,
                declaration_end,
                declaration_source,
                source_declaration_start,
                source_declaration_end,
            ) = validate_geometric_declaration(result, source_copy)
            print(
                f"声明定位 {result.target.name}: 目标第 {declaration_start}-{declaration_end} 行 "
                f"-> {declaration_source}:{source_declaration_start}-{source_declaration_end}"
            )

            target_line: int
            source: str
            source_line: int
            target_line, source, source_line = run_diagnostic(
                compiler,
                result,
                source_copy,
                generated_directory,
            )
            print(
                f"诊断定位 {result.target.name}: {Path(result.target.output).name}:{target_line} "
                f"-> {source}:{source_line}"
            )
    except Exception as error:
        drill_error = error
    finally:
        body_copy.write_bytes(body_before)

    if body_copy.read_bytes() != body_before:
        raise MaintenanceCheckError("演练来源未能恢复为原始字节")

    restored_results: tuple[generate_headers.GeneratedTarget, ...] = (
        generate_headers.generate_targets(config_copy)
    )
    restored_directory: Path = drill_directory / "generated-restored"
    restored_directory.mkdir()
    for result in restored_results:
        before: bytes | None = original_bytes.get(result.target.name)
        after: bytes = result.content.encode(TEXT_ENCODING)
        if before is None or after != before:
            raise MaintenanceCheckError(
                f"恢复后的 {result.target.output} 与演练前生成字节不一致"
            )
        restored_path: Path = restored_directory / Path(result.target.output).name
        restored_path.write_bytes(after)

    if drill_error is not None:
        raise MaintenanceCheckError(f"维护演练失败：{drill_error}") from drill_error

    print("来源已恢复；RandX.hpp 与 RandX_Cpp17.hpp 均与演练前生成字节一致。")


def parse_arguments(arguments: Sequence[str] | None = None) -> argparse.Namespace:
    """解析编译器与明确指定的演练产物目录。"""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="验证 C++17/C++23 头文件的共享来源定位、编译诊断映射与来源恢复；要求 --artifact-dir 下的 header-maintenance-drill 子目录不存在。"
    )
    parser.add_argument("--compiler", required=True, help="支持 -std、-fsyntax-only 和 -I 参数的 GCC/Clang 兼容编译器")
    parser.add_argument(
        "--artifact-dir",
        required=True,
        type=Path,
        help="演练副本与生成产物的父目录；其中 header-maintenance-drill 子目录必须不存在",
    )
    return parser.parse_args(arguments)


def main(arguments: Sequence[str] | None = None) -> int:
    """命令行入口。"""
    parsed_arguments: argparse.Namespace = parse_arguments(arguments)
    try:
        run_maintenance_check(parsed_arguments.compiler, parsed_arguments.artifact_dir)
    except (MaintenanceCheckError, OSError, generate_headers.GenerationError) as error:
        print(f"维护检查失败：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
