"""从共享源码片段确定性地生成独立分发头文件。"""

from __future__ import annotations

import argparse
import json
import posixpath
import re
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Sequence


SCRIPT_PATH: Path = Path(__file__).resolve()
PROJECT_ROOT: Path = SCRIPT_PATH.parent.parent
CONFIG_PATH: Path = PROJECT_ROOT / "src" / "header_sources" / "targets.json"
SOURCE_ROOT: Path = CONFIG_PATH.parent
TEXT_ENCODING: str = "utf-8"
LINE_ENDING: str = "\n"
INCLUDE_MARKER: str = "// @randx-include"
INCLUDE_PATTERN: re.Pattern[str] = re.compile(
    r"^[ \t]*// @randx-include[ \t]+(?P<path>\S+)[ \t]*$"
)
JSON_INDENT: int = 2


class GenerationError(Exception):
    """包含来源位置的生成错误。"""


@dataclass(frozen=True)
class Target:
    """一个生成目标及其根模板。"""

    name: str
    output: str
    template: str


@dataclass(frozen=True)
class OriginLine:
    """生成文本中的一行及其来源位置。"""

    text: str
    source: str
    source_line: int


@dataclass(frozen=True)
class MappingSpan:
    """连续的目标行与来源行区间。"""

    target_start: int
    target_end: int
    source: str
    source_start: int
    source_end: int


@dataclass(frozen=True)
class GeneratedTarget:
    """一个已展开目标的文本和来源映射。"""

    target: Target
    content: str
    mappings: tuple[MappingSpan, ...]


def _display_chain(paths: Sequence[str]) -> str:
    """以来源相对路径显示当前递归引用链。"""
    return " -> ".join(paths)


def _normalize_relative_path(
    path: str,
    description: str,
    location: str,
    allow_parent: bool = False,
) -> str:
    """验证 POSIX 相对路径并返回不带冗余片段的形式。"""
    if "\\" in path:
        raise GenerationError(f"{description}必须使用 POSIX '/' 分隔符：{location}。")
    if PurePosixPath(path).is_absolute() or PureWindowsPath(path).drive:
        raise GenerationError(f"{description}必须为 POSIX 相对路径：{location}。")

    normalized: str = posixpath.normpath(path)
    if normalized == "." or (
        not allow_parent and (normalized == ".." or normalized.startswith("../"))
    ):
        raise GenerationError(f"{description}不能超出所属目录：{location}。")
    return normalized


def _windows_path_key(path: str) -> str:
    """按 Windows 路径大小写与尾随点、空格规则生成碰撞键。"""
    components: list[str] = [component.rstrip(" .").casefold() for component in path.split("/")]
    return "/".join(components)


def _source_file(source_root: Path, relative_path: str) -> Path:
    """根据来源根目录和 POSIX 相对路径取得输入文件。"""
    return source_root.joinpath(*PurePosixPath(relative_path).parts)


def _read_source_lines(path: Path, relative_path: str, call_site: str | None, chain: Sequence[str]) -> list[str]:
    """读取 UTF-8 来源并统一为 LF 结尾的逻辑行。"""
    try:
        text: str = path.read_bytes().decode(TEXT_ENCODING)
    except FileNotFoundError as error:
        if call_site is None:
            detail: str = f"根模板不存在：{relative_path}"
        else:
            detail = f"引用文件不存在：{relative_path}；引用位置：{call_site}；引用链：{_display_chain(chain)}"
        raise GenerationError(detail) from error
    except UnicodeDecodeError as error:
        if call_site is None:
            detail = f"来源文件不是有效 UTF-8：{relative_path}"
        else:
            detail = f"来源文件不是有效 UTF-8：{relative_path}；引用位置：{call_site}；引用链：{_display_chain(chain)}"
        raise GenerationError(detail) from error
    except OSError as error:
        if call_site is None:
            detail = f"无法读取来源文件：{relative_path}"
        else:
            detail = f"无法读取来源文件：{relative_path}；引用位置：{call_site}；引用链：{_display_chain(chain)}"
        raise GenerationError(detail) from error

    normalized: str = text.replace("\r\n", LINE_ENDING).replace("\r", LINE_ENDING)
    if normalized and not normalized.endswith(LINE_ENDING):
        normalized += LINE_ENDING
    if not normalized:
        return []
    return normalized.split(LINE_ENDING)[:-1]


def _expand_file(
    source_root: Path,
    relative_path: str,
    active_paths: tuple[Path, ...],
    chain: tuple[str, ...],
    call_site: str | None,
) -> list[OriginLine]:
    """递归展开单个来源文件，只在当前活动链中检测循环。"""
    path: Path = _source_file(source_root, relative_path)
    identity: Path = path.resolve()
    lines: list[str] = _read_source_lines(path, relative_path, call_site, chain)
    expanded: list[OriginLine] = []

    for line_number, line in enumerate(lines, start=1):
        match: re.Match[str] | None = INCLUDE_PATTERN.fullmatch(line)
        if match is None:
            if line.lstrip().startswith(INCLUDE_MARKER):
                location: str = f"{relative_path}:{line_number}"
                raise GenerationError(
                    f"引用语法错误：{location}；引用链：{_display_chain(chain)}"
                )
            expanded.append(OriginLine(f"{line}{LINE_ENDING}", relative_path, line_number))
            continue

        location = f"{relative_path}:{line_number}"
        include_path: str = _normalize_relative_path(
            match.group("path"),
            "引用路径",
            f"{location}；引用链：{_display_chain(chain)}",
            allow_parent=True,
        )
        include_relative: str = posixpath.normpath(
            posixpath.join(posixpath.dirname(relative_path), include_path)
        )
        if include_relative == ".." or include_relative.startswith("../"):
            raise GenerationError(
                f"引用路径不能超出来源目录：{location}；引用链：{_display_chain(chain)}"
            )
        include_file: Path = _source_file(source_root, include_relative)
        include_identity: Path = include_file.resolve()
        include_chain: tuple[str, ...] = chain + (include_relative,)
        if include_identity in active_paths + (identity,):
            raise GenerationError(
                f"检测到循环引用：{location}；引用链：{_display_chain(include_chain)}"
            )

        expanded.extend(
            _expand_file(
                source_root,
                include_relative,
                active_paths + (identity,),
                include_chain,
                f"{relative_path}:{line_number}",
            )
        )

    return expanded


def _build_mapping(lines: Sequence[OriginLine]) -> tuple[MappingSpan, ...]:
    """合并目标行与来源行均连续的映射跨度。"""
    mappings: list[MappingSpan] = []
    for target_line, origin in enumerate(lines, start=1):
        if mappings:
            previous: MappingSpan = mappings[-1]
            if (
                previous.source == origin.source
                and previous.target_end + 1 == target_line
                and previous.source_end + 1 == origin.source_line
            ):
                mappings[-1] = MappingSpan(
                    previous.target_start,
                    target_line,
                    previous.source,
                    previous.source_start,
                    origin.source_line,
                )
                continue
        mappings.append(
            MappingSpan(target_line, target_line, origin.source, origin.source_line, origin.source_line)
        )
    return tuple(mappings)


def load_targets(config_path: Path = CONFIG_PATH) -> tuple[Target, ...]:
    """读取 targets.json 中按文本顺序排列的生成目标。"""
    try:
        config_text: str = config_path.read_bytes().decode(TEXT_ENCODING)
        data: object = json.loads(config_text)
    except FileNotFoundError as error:
        raise GenerationError("找不到 src/header_sources/targets.json。") from error
    except UnicodeDecodeError as error:
        raise GenerationError("targets.json 不是有效 UTF-8。") from error
    except json.JSONDecodeError as error:
        raise GenerationError(f"targets.json 格式错误：第 {error.lineno} 行。") from error
    except OSError as error:
        raise GenerationError("无法读取 src/header_sources/targets.json。") from error

    if not isinstance(data, dict) or not isinstance(data.get("targets"), list):
        raise GenerationError("targets.json 必须包含 targets 数组。")

    targets: list[Target] = []
    name_locations: dict[str, tuple[str, str]] = {}
    output_locations: dict[str, tuple[str, str]] = {}
    for index, item in enumerate(data["targets"]):
        target_location: str = f"targets.json: targets[{index}]"
        if not isinstance(item, dict):
            raise GenerationError(f"{target_location} 必须是对象。")
        name: object = item.get("name")
        output: object = item.get("output")
        template: object = item.get("template")
        if not all(isinstance(value, str) and value for value in (name, output, template)):
            raise GenerationError(f"{target_location} 必须提供非空 name、output 和 template。")

        target_name: str = str(name)
        output_location: str = f"{target_location}.output（{target_name}）"
        template_location: str = f"{target_location}.template（{target_name}）"
        normalized_output: str = _normalize_relative_path(str(output), "目标输出路径", output_location)
        normalized_template: str = _normalize_relative_path(str(template), "目标模板路径", template_location)
        normalized_map_path: str = _normalize_relative_path(
            f"{target_name}.json",
            "目标名称对应的映射路径",
            f"{target_location}.name（{target_name}）",
        )

        name_key: str = _windows_path_key(normalized_map_path)
        previous_name: tuple[str, str] | None = name_locations.get(name_key)
        if previous_name is not None:
            raise GenerationError(
                "目标名称对应的来源映射位置重复："
                f"{previous_name[0]}（{previous_name[1]}）与 "
                f"{normalized_map_path}（{target_location}.name（{target_name}））。"
            )
        name_locations[name_key] = (
            normalized_map_path,
            f"{target_location}.name（{target_name}）",
        )

        output_key: str = _windows_path_key(normalized_output)
        previous_output: tuple[str, str] | None = output_locations.get(output_key)
        if previous_output is not None:
            raise GenerationError(
                "目标输出位置重复："
                f"{previous_output[0]}（{previous_output[1]}）与 "
                f"{normalized_output}（{output_location}）。"
            )
        output_locations[output_key] = (normalized_output, output_location)
        targets.append(Target(target_name, normalized_output, normalized_template))

    return tuple(targets)


def generate_targets(config_path: Path = CONFIG_PATH) -> tuple[GeneratedTarget, ...]:
    """先完整展开配置中的所有目标，再返回文本及双向可查询的映射。"""
    source_root: Path = config_path.parent
    generated: list[GeneratedTarget] = []
    for target in load_targets(config_path):
        template_path: str = target.template
        origins: list[OriginLine] = _expand_file(
            source_root,
            template_path,
            (),
            (template_path,),
            None,
        )
        generated.append(
            GeneratedTarget(
                target=target,
                content="".join(origin.text for origin in origins),
                mappings=_build_mapping(origins),
            )
        )
    return tuple(generated)


def _map_content(result: GeneratedTarget) -> str:
    """序列化一个目标的完整展开位置表。"""
    data: dict[str, object] = {
        "target": result.target.name,
        "output": result.target.output,
        "mappings": [
            {
                "target_start": span.target_start,
                "target_end": span.target_end,
                "source": span.source,
                "source_start": span.source_start,
                "source_end": span.source_end,
            }
            for span in result.mappings
        ],
    }
    return json.dumps(data, ensure_ascii=False, indent=JSON_INDENT) + LINE_ENDING


def _write_if_changed(path: Path, content: bytes, display_name: str) -> bool:
    """仅在目标字节发生变化时写入文件。"""
    try:
        if path.exists() and path.read_bytes() == content:
            return False
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    except OSError as error:
        raise GenerationError(f"无法写入文件：{display_name}") from error
    return True


def _find_source_line(mappings: Sequence[MappingSpan], target_line: int) -> tuple[str, int] | None:
    """查找一个生成行对应的来源文件及行号。"""
    for span in mappings:
        if span.target_start <= target_line <= span.target_end:
            source_line: int = span.source_start + target_line - span.target_start
            return span.source, source_line
    return None


def _check_target(result: GeneratedTarget, output_path: Path) -> str | None:
    """比较已提交头文件的实际字节并返回首处差异诊断。"""
    try:
        actual: bytes = output_path.read_bytes()
    except FileNotFoundError:
        location: str = f"；来源 {result.mappings[0].source}:{result.mappings[0].source_start}" if result.mappings else ""
        return f"生成结果缺失：{result.target.output}{location}。"
    except OSError as error:
        raise GenerationError(f"无法读取生成结果：{result.target.output}") from error

    expected: bytes = result.content.encode(TEXT_ENCODING)
    if actual == expected:
        return None

    common_length: int = min(len(actual), len(expected))
    difference: int = common_length
    for index in range(common_length):
        if actual[index] != expected[index]:
            difference = index
            break
    target_line: int = expected[:difference].count(b"\n") + 1
    source_location: tuple[str, int] | None = _find_source_line(result.mappings, target_line)
    if source_location is None and result.mappings:
        last_span: MappingSpan = result.mappings[-1]
        source_location = (last_span.source, last_span.source_end)
    if source_location is None:
        location = ""
    else:
        location = f"；来源 {source_location[0]}:{source_location[1]}"
    return f"生成结果不一致：{result.target.output} 第 {target_line} 行{location}，首个差异字节偏移 {difference}。"


def _write_maps(results: Sequence[GeneratedTarget], map_dir: Path) -> None:
    """将每个目标的所有展开位置写入独立 JSON 文件。"""
    for result in results:
        map_name: str = f"{result.target.name}.json"
        _write_if_changed(
            map_dir / map_name,
            _map_content(result).encode(TEXT_ENCODING),
            map_name,
        )


def run_cli(arguments: Sequence[str] | None = None, config_path: Path = CONFIG_PATH) -> int:
    """执行显式选择的生成、检查或仅映射命令。"""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="按 targets.json 展开 RandX 头文件来源。"
    )
    modes: argparse._MutuallyExclusiveGroup = parser.add_mutually_exclusive_group()
    modes.add_argument("--write", action="store_true", help="更新仓库中的生成头文件")
    modes.add_argument("--check", action="store_true", help="比较仓库头文件的实际字节")
    modes.add_argument("--output-dir", type=Path, help="将生成头文件写入指定目录")
    parser.add_argument("--map-dir", type=Path, help="写入每个目标的 JSON 来源映射")
    options: argparse.Namespace = parser.parse_args(arguments)
    has_mode: bool = bool(options.write or options.check or options.output_dir is not None)
    if not has_mode and options.map_dir is None:
        parser.error("请指定 --write、--check、--output-dir 或 --map-dir。")

    try:
        results: tuple[GeneratedTarget, ...] = generate_targets(config_path)
        project_root: Path = config_path.parent.parent.parent
        if options.map_dir is not None:
            _write_maps(results, options.map_dir)

        if options.check:
            diagnostics: list[str] = []
            for result in results:
                diagnostic: str | None = _check_target(result, project_root / result.target.output)
                if diagnostic is not None:
                    diagnostics.append(diagnostic)
            if diagnostics:
                for diagnostic in diagnostics:
                    print(diagnostic, file=sys.stderr)
                return 1
            print("生成头文件与来源一致。")
            return 0

        if options.write:
            output_root: Path = project_root
        elif options.output_dir is not None:
            output_root = options.output_dir
        else:
            print("来源映射已写入。")
            return 0

        changed_outputs: list[str] = []
        for result in results:
            output_path: Path = output_root / result.target.output
            if _write_if_changed(
                output_path,
                result.content.encode(TEXT_ENCODING),
                result.target.output,
            ):
                changed_outputs.append(result.target.output)
        if changed_outputs:
            print(f"已更新生成头文件：{', '.join(changed_outputs)}。")
        else:
            print("生成头文件已是最新。")
        return 0
    except GenerationError as error:
        print(str(error), file=sys.stderr)
        return 1


def main() -> int:
    """执行命令行入口。"""
    return run_cli()


if __name__ == "__main__":
    raise SystemExit(main())
