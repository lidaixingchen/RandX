#!/usr/bin/env python3
"""收集并核对 RandX 双版本 doctest 注册清单。"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence
from xml.etree import ElementTree


SCHEMA_VERSION: int = 1
BASE_PUBLIC_ROOT: str = "公共/基础"
BASE_PUBLIC_PREFIX: str = f"{BASE_PUBLIC_ROOT}/"
CONDITION_PUBLIC_ROOT: str = "公共/条件"
CONDITION_PUBLIC_PREFIX: str = f"{CONDITION_PUBLIC_ROOT}/"
CHAR8_T_CAPABILITY: str = "char8_t"
SUPPORTED_STANDARDS: frozenset[str] = frozenset({"c++17", "c++20", "c++23"})
SUPPORTED_VARIANTS: frozenset[str] = frozenset({"cpp17", "cpp23"})
MSVC_COMPILER_NAME: str = "msvc"
DEBUG_MODE: str = "debug"
RELEASE_MODE: str = "release"
BUILD_MODES: frozenset[str] = frozenset({DEBUG_MODE, RELEASE_MODE})
EXPECTED_GROUP_ARGUMENTS: tuple[str, str, str] = (
    "COMPILER",
    "PLATFORM",
    "BUILD_MODE",
)


class ContractError(Exception):
    """清单采集或契约核对失败。"""


@dataclass(frozen=True)
class TestManifest:
    metadata: dict[str, str]
    registrations: dict[tuple[str, str], int]


def _run_doctest_query(binary: Path, arguments: Sequence[str]) -> str:
    command: list[str] = [str(binary.resolve()), *arguments, "--no-version", "--no-colors"]
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command,
        check=False,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        details: str = result.stderr.strip() or result.stdout.strip()
        raise ContractError(
            f"执行 doctest 清单命令失败，退出码 {result.returncode}: "
            f"{command!r}\n{details}"
        )
    return result.stdout


def parse_doctest_xml(output: str) -> dict[tuple[str, str], int]:
    """读取 doctest XML reporter 的注册项，保留重复出现次数。"""
    try:
        root: ElementTree.Element = ElementTree.fromstring(output)
    except ElementTree.ParseError as error:
        raise ContractError(f"无法解析 doctest XML 清单：{error}") from error
    if root.tag != "doctest":
        raise ContractError(f"doctest XML 根节点无效：{root.tag!r}")

    registrations: Counter[tuple[str, str]] = Counter()
    for test_case in root.iter("TestCase"):
        suite: str | None = test_case.get("testsuite")
        case: str | None = test_case.get("name")
        if not suite or not case:
            raise ContractError("doctest XML 注册项缺少套件名或用例名。")
        registrations[(suite, case)] += 1
    if not registrations:
        raise ContractError("doctest XML 注册清单为空。")
    return dict(registrations)


def collect_registrations(binary: Path) -> dict[tuple[str, str], int]:
    """通过 doctest XML reporter 读取套件与用例注册信息。"""
    if not binary.is_file():
        raise ContractError(f"找不到测试程序：{binary}")

    report: str = _run_doctest_query(binary, ["--reporters=xml", "--list-test-cases"])
    return parse_doctest_xml(report)


def _public_kind(suite: str) -> tuple[str, str] | None:
    if suite.startswith(BASE_PUBLIC_ROOT):
        if not suite.startswith(BASE_PUBLIC_PREFIX):
            raise ContractError(f"基础公共套件名前缀格式错误：{suite!r}")
        domain: str = suite[len(BASE_PUBLIC_PREFIX) :]
        if not domain:
            raise ContractError(f"基础公共套件名缺少领域名称：{suite!r}")
        return ("base", "")
    if suite.startswith(CONDITION_PUBLIC_ROOT):
        if not suite.startswith(CONDITION_PUBLIC_PREFIX):
            raise ContractError(f"条件公共套件名前缀格式错误：{suite!r}")
        capability_and_domain: str = suite[len(CONDITION_PUBLIC_PREFIX) :]
        capability, separator, domain = capability_and_domain.partition("/")
        if not separator or not capability or not domain:
            raise ContractError(f"条件公共套件名格式错误：{suite!r}")
        return ("condition", capability)
    return None


def _validate_metadata(
    metadata: dict[str, str], source: str, allow_local_build_mode: bool = False
) -> None:
    for key in ("compiler", "platform", "standard", "build_mode", "variant"):
        value: Any = metadata.get(key)
        if not isinstance(value, str) or not value:
            raise ContractError(f"清单元数据 {key} 缺失或无效：{source}")
    if metadata["standard"] not in SUPPORTED_STANDARDS:
        raise ContractError(f"清单使用不支持的语言标准：{metadata['standard']} ({source})")
    if metadata["build_mode"] not in BUILD_MODES and not (
        allow_local_build_mode and metadata["build_mode"] == "local"
    ):
        raise ContractError(f"清单使用不支持的构建模式：{metadata['build_mode']} ({source})")
    if metadata["variant"] not in SUPPORTED_VARIANTS:
        raise ContractError(f"清单使用不支持的测试版本：{metadata['variant']} ({source})")
    expected_variant: str = "cpp23" if metadata["standard"] == "c++23" else "cpp17"
    if metadata["variant"] != expected_variant:
        raise ContractError(
            f"语言标准与测试版本不匹配：{metadata['standard']} / {metadata['variant']} ({source})"
        )


def validate_manifest(
    manifest: TestManifest, source: str, allow_local_build_mode: bool = False
) -> None:
    _validate_metadata(manifest.metadata, source, allow_local_build_mode)
    base_cases: int = 0
    for (suite, case), count in manifest.registrations.items():
        if not suite or not case:
            raise ContractError(f"{source} 包含空套件名或用例名。")
        kind: tuple[str, str] | None = _public_kind(suite)
        if kind is None:
            continue
        if count != 1:
            raise ContractError(
                f"{source} 的公共用例必须恰好注册一次，实际为 {count} 次："
                f"{suite} / {case}"
            )
        if kind[0] == "base":
            base_cases += 1
    if base_cases == 0:
        raise ContractError(f"{source} 的基础公共清单为空。")


def _registrations_for(
    manifest: TestManifest,
    kind: str,
    capability: str | None = None,
    exclude_char8_t: bool = False,
) -> dict[tuple[str, str], int]:
    selected: dict[tuple[str, str], int] = {}
    for registration, count in manifest.registrations.items():
        suite: str = registration[0]
        public_kind: tuple[str, str] | None = _public_kind(suite)
        if public_kind is None or public_kind[0] != kind:
            continue
        current_capability: str = public_kind[1]
        if kind == "condition":
            if capability is not None and current_capability != capability:
                continue
            if exclude_char8_t and current_capability == CHAR8_T_CAPABILITY:
                continue
        selected[registration] = count
    return selected


def _format_difference(
    first: dict[tuple[str, str], int],
    second: dict[tuple[str, str], int],
) -> str:
    first_keys: set[tuple[str, str]] = set(first)
    second_keys: set[tuple[str, str]] = set(second)
    only_first: list[tuple[str, str]] = sorted(first_keys - second_keys)
    only_second: list[tuple[str, str]] = sorted(second_keys - first_keys)
    differing_counts: list[tuple[tuple[str, str], int, int]] = [
        (key, first[key], second[key])
        for key in sorted(first_keys & second_keys)
        if first[key] != second[key]
    ]
    parts: list[str] = []
    if only_first:
        parts.append(f"仅左侧存在：{only_first!r}")
    if only_second:
        parts.append(f"仅右侧存在：{only_second!r}")
    if differing_counts:
        parts.append(f"次数不同：{differing_counts!r}")
    return "；".join(parts)


def _require_equal(
    first: dict[tuple[str, str], int],
    second: dict[tuple[str, str], int],
    description: str,
    first_label: str,
    second_label: str,
) -> None:
    if first != second:
        raise ContractError(
            f"{description}不一致（{first_label} 与 {second_label}）："
            f"{_format_difference(first, second)}"
        )


def _validate_pair(
    compat17: TestManifest,
    main23: TestManifest,
    compat_label: str,
    main_label: str,
    compare_char8_t: bool,
    allow_local_build_mode: bool = False,
) -> None:
    validate_manifest(compat17, compat_label, allow_local_build_mode)
    validate_manifest(main23, main_label, allow_local_build_mode)
    compat_base: dict[tuple[str, str], int] = _registrations_for(compat17, "base")
    main_base: dict[tuple[str, str], int] = _registrations_for(main23, "base")
    _require_equal(compat_base, main_base, "基础公共注册清单", compat_label, main_label)

    compat_conditions: dict[tuple[str, str], int] = _registrations_for(
        compat17, "condition", exclude_char8_t=True
    )
    main_conditions: dict[tuple[str, str], int] = _registrations_for(
        main23, "condition", exclude_char8_t=True
    )
    _require_equal(
        compat_conditions,
        main_conditions,
        "平台条件公共注册清单",
        compat_label,
        main_label,
    )

    compat_char8: dict[tuple[str, str], int] = _registrations_for(
        compat17, "condition", capability=CHAR8_T_CAPABILITY
    )
    main_char8: dict[tuple[str, str], int] = _registrations_for(
        main23, "condition", capability=CHAR8_T_CAPABILITY
    )
    if compat17.metadata.get("standard") == "c++17" and compat_char8:
        raise ContractError(
            f"C++17 兼容目标不应注册 char8_t 公共用例：{compat_char8!r}"
        )
    if compare_char8_t:
        if not compat_char8 and not main_char8:
            raise ContractError("char8_t 能力已启用，但公共注册清单为空。")
        _require_equal(
            compat_char8,
            main_char8,
            "char8_t 条件公共注册清单",
            compat_label,
            main_label,
        )


def _manifest_from_collected(
    registrations: dict[tuple[str, str], int], metadata: dict[str, str]
) -> TestManifest:
    manifest: TestManifest = TestManifest(metadata=metadata, registrations=registrations)
    validate_manifest(manifest, "测试程序", allow_local_build_mode=True)
    return manifest


def _load_manifest(path: Path) -> TestManifest:
    try:
        raw: Any = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError(f"无法读取清单 {path}：{error}") from error
    if not isinstance(raw, dict) or raw.get("schema_version") != SCHEMA_VERSION:
        raise ContractError(f"清单版本错误或格式无效：{path}")
    metadata_value: Any = raw.get("metadata")
    entries_value: Any = raw.get("registrations")
    if not isinstance(metadata_value, dict) or not isinstance(entries_value, list):
        raise ContractError(f"清单缺少 metadata 或 registrations：{path}")
    metadata: dict[str, str] = {}
    for key in ("compiler", "platform", "standard", "build_mode", "variant"):
        value: Any = metadata_value.get(key)
        if not isinstance(value, str) or not value:
            raise ContractError(f"清单元数据 {key} 缺失或无效：{path}")
        metadata[key] = value
    registrations: Counter[tuple[str, str]] = Counter()
    for entry in entries_value:
        if not isinstance(entry, dict):
            raise ContractError(f"清单注册项格式无效：{path}")
        suite: Any = entry.get("suite")
        case: Any = entry.get("case")
        count: Any = entry.get("count")
        if (
            not isinstance(suite, str)
            or not isinstance(case, str)
            or not isinstance(count, int)
            or isinstance(count, bool)
            or count < 1
        ):
            raise ContractError(f"清单注册项名称或次数无效：{path}: {entry!r}")
        registrations[(suite, case)] += count
    manifest: TestManifest = TestManifest(
        metadata=metadata,
        registrations=dict(registrations),
    )
    validate_manifest(manifest, str(path))
    return manifest


def _write_manifest(
    path: Path,
    metadata: dict[str, str],
    registrations: dict[tuple[str, str], int],
) -> None:
    validate_manifest(TestManifest(metadata=metadata, registrations=registrations), str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = [
        {"suite": suite, "case": case, "count": count}
        for (suite, case), count in sorted(registrations.items())
    ]
    payload: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "metadata": metadata,
        "registrations": rows,
    }
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def _collect_command(arguments: argparse.Namespace) -> None:
    metadata: dict[str, str] = {
        "compiler": arguments.compiler,
        "platform": arguments.platform,
        "standard": arguments.standard,
        "build_mode": arguments.build_mode,
        "variant": arguments.variant,
    }
    registrations: dict[tuple[str, str], int] = collect_registrations(arguments.binary)
    _write_manifest(arguments.output, metadata, registrations)
    print(
        f"已记录 {len(registrations)} 个套件/用例项：{arguments.output}"
    )


def _compare_local_binaries(arguments: argparse.Namespace) -> None:
    compat: TestManifest = _manifest_from_collected(
        collect_registrations(arguments.cpp17_binary),
        {
            "compiler": "local",
            "platform": "local",
            "standard": "c++17",
            "build_mode": "local",
            "variant": "cpp17",
        },
    )
    main: TestManifest = _manifest_from_collected(
        collect_registrations(arguments.cpp23_binary),
        {
            "compiler": "local",
            "platform": "local",
            "standard": "c++23",
            "build_mode": "local",
            "variant": "cpp23",
        },
    )
    _validate_pair(
        compat,
        main,
        str(arguments.cpp17_binary),
        str(arguments.cpp23_binary),
        compare_char8_t=False,
        allow_local_build_mode=True,
    )
    print("基础及适用的条件公共注册清单一致。")


def _group_key(metadata: dict[str, str]) -> tuple[str, str, str]:
    return (metadata["compiler"], metadata["platform"], metadata["build_mode"])


def _validate_matrix(
    manifests: list[TestManifest],
    expected_groups: Sequence[tuple[str, str, str]] | None = None,
) -> None:
    if not manifests and not expected_groups:
        raise ContractError("没有找到可核对的注册清单。")
    groups: dict[tuple[str, str, str], list[TestManifest]] = {}
    unique_keys: set[tuple[str, str, str, str, str]] = set()
    for manifest in manifests:
        validate_manifest(manifest, str(manifest.metadata))
        metadata: dict[str, str] = manifest.metadata
        key: tuple[str, str, str, str, str] = (
            metadata["compiler"],
            metadata["platform"],
            metadata["build_mode"],
            metadata["standard"],
            metadata["variant"],
        )
        if key in unique_keys:
            raise ContractError(f"存在重复的矩阵注册清单：{key!r}")
        unique_keys.add(key)
        groups.setdefault(_group_key(metadata), []).append(manifest)

    if expected_groups is not None:
        expected_group_set: set[tuple[str, str, str]] = set(expected_groups)
        if len(expected_group_set) != len(expected_groups):
            raise ContractError("预期矩阵组包含重复项。")
        for compiler, platform, build_mode in expected_group_set:
            if not compiler or not platform or build_mode not in BUILD_MODES:
                raise ContractError(f"预期矩阵组元数据无效：{(compiler, platform, build_mode)!r}")
        actual_group_set: set[tuple[str, str, str]] = set(groups)
        missing_groups: set[tuple[str, str, str]] = expected_group_set - actual_group_set
        unexpected_groups: set[tuple[str, str, str]] = actual_group_set - expected_group_set
        if missing_groups or unexpected_groups:
            differences: list[str] = []
            if missing_groups:
                differences.append(f"缺少矩阵组：{sorted(missing_groups)!r}")
            if unexpected_groups:
                differences.append(f"额外矩阵组：{sorted(unexpected_groups)!r}")
            raise ContractError("预期矩阵组不匹配：" + "；".join(differences))

    for group, group_manifests in sorted(groups.items()):
        by_standard: dict[str, TestManifest] = {
            manifest.metadata["standard"]: manifest for manifest in group_manifests
        }
        compiler, platform, build_mode = group
        group_label: str = f"{compiler}/{platform}/{build_mode}"
        required_standards: set[str] = {"c++17", "c++23"}
        if not (compiler == MSVC_COMPILER_NAME and build_mode == RELEASE_MODE):
            required_standards.add("c++20")
        missing: set[str] = required_standards - set(by_standard)
        if missing:
            raise ContractError(f"{group_label} 缺少矩阵清单：{sorted(missing)!r}")

        compat17: TestManifest = by_standard["c++17"]
        compat20: TestManifest | None = by_standard.get("c++20")
        main23: TestManifest = by_standard["c++23"]
        _validate_pair(
            compat17,
            main23,
            f"{group_label}/c++17",
            f"{group_label}/c++23",
            compare_char8_t=False,
        )
        if compat20 is not None:
            _validate_pair(
                compat20,
                main23,
                f"{group_label}/c++20",
                f"{group_label}/c++23",
                compare_char8_t=True,
            )


def _compare_manifests(arguments: argparse.Namespace) -> None:
    paths: list[Path] = sorted(arguments.manifest_dir.glob("*.json"))
    manifests: list[TestManifest] = [_load_manifest(path) for path in paths]
    _validate_matrix(manifests, arguments.expected_groups)
    print(f"已核对 {len(manifests)} 份清单及其跨标准公共注册契约。")


class _ExpectedGroupAction(argparse.Action):
    def __call__(
        self,
        parser: argparse.ArgumentParser,
        namespace: argparse.Namespace,
        values: Sequence[str],
        option_string: str | None = None,
    ) -> None:
        if len(values) != len(EXPECTED_GROUP_ARGUMENTS):
            raise argparse.ArgumentError(self, "矩阵组需要 compiler、platform、build_mode 三个值。")
        compiler, platform, build_mode = values
        if not compiler or not platform:
            raise argparse.ArgumentError(self, "compiler 与 platform 不能为空。")
        if build_mode not in BUILD_MODES:
            choices: str = ", ".join(sorted(BUILD_MODES))
            raise argparse.ArgumentError(self, f"build_mode 必须为以下值之一：{choices}")
        expected_groups: list[tuple[str, str, str]] = getattr(namespace, self.dest, None) or []
        expected_groups.append((compiler, platform, build_mode))
        setattr(namespace, self.dest, expected_groups)


def _build_parser() -> argparse.ArgumentParser:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="提取 doctest 注册清单并核对双版本公共契约。"
    )
    subparsers: argparse._SubParsersAction[argparse.ArgumentParser] = parser.add_subparsers(
        dest="command", required=True
    )

    collect_parser: argparse.ArgumentParser = subparsers.add_parser(
        "collect", help="从单个测试程序生成明文注册清单。"
    )
    collect_parser.add_argument("--binary", type=Path, required=True)
    collect_parser.add_argument("--compiler", required=True)
    collect_parser.add_argument("--platform", required=True)
    collect_parser.add_argument("--standard", choices=sorted(SUPPORTED_STANDARDS), required=True)
    collect_parser.add_argument("--build-mode", choices=sorted(BUILD_MODES), required=True)
    collect_parser.add_argument("--variant", choices=sorted(SUPPORTED_VARIANTS), required=True)
    collect_parser.add_argument("--output", type=Path, required=True)
    collect_parser.set_defaults(handler=_collect_command)

    compare_binaries_parser: argparse.ArgumentParser = subparsers.add_parser(
        "compare-binaries", help="直接比较本机构建的 C++17 与 C++23 测试程序。"
    )
    compare_binaries_parser.add_argument("--cpp17-binary", type=Path, required=True)
    compare_binaries_parser.add_argument("--cpp23-binary", type=Path, required=True)
    compare_binaries_parser.set_defaults(handler=_compare_local_binaries)

    compare_parser: argparse.ArgumentParser = subparsers.add_parser(
        "compare", help="核对矩阵清单及跨标准公共注册规则。"
    )
    compare_parser.add_argument("--manifest-dir", type=Path, required=True)
    compare_parser.add_argument(
        "--expected-group",
        dest="expected_groups",
        action=_ExpectedGroupAction,
        nargs=len(EXPECTED_GROUP_ARGUMENTS),
        metavar=EXPECTED_GROUP_ARGUMENTS,
        default=None,
        help="预期矩阵组，可重复指定，每次提供 compiler、platform、build_mode。",
    )
    compare_parser.set_defaults(handler=_compare_manifests)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser: argparse.ArgumentParser = _build_parser()
    arguments: argparse.Namespace = parser.parse_args(argv)
    handler: Any = arguments.handler
    try:
        handler(arguments)
    except ContractError as error:
        print(f"注册契约检查失败：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
