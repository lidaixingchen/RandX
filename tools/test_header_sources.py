"""验证真实头文件产物的逐行来源映射。"""

from __future__ import annotations

import posixpath
import unittest
from pathlib import Path, PurePosixPath

import generate_headers


SOURCE_ROOT: Path = generate_headers.SOURCE_ROOT
TARGET_RESULTS: tuple[generate_headers.GeneratedTarget, ...] = generate_headers.generate_targets(
    generate_headers.CONFIG_PATH
)
RESULTS_BY_NAME: dict[str, generate_headers.GeneratedTarget] = {
    result.target.name: result for result in TARGET_RESULTS
}
GEOMETRIC_SIGNATURE: str = "inline T RandGeometric(Engine& engine, double p = 0.5)"
GEOMETRIC_BODY_SOURCE: str = "common/distributions/geometric_body.inc"


def read_source_lines(relative_path: str) -> list[str]:
    """读取与生成器相同的 UTF-8 逻辑行。"""
    source_path: Path = SOURCE_ROOT.joinpath(*PurePosixPath(relative_path).parts)
    source_text: str = source_path.read_bytes().decode("utf-8")
    normalized_text: str = source_text.replace("\r\n", "\n").replace("\r", "\n")
    return normalized_text.splitlines()


def mapped_target_line(
    result: generate_headers.GeneratedTarget,
    source: str,
    source_line: int,
) -> int:
    """从来源行反查唯一的生成目标行。"""
    matches: list[int] = [
        span.target_start + source_line - span.source_start
        for span in result.mappings
        if span.source == source and span.source_start <= source_line <= span.source_end
    ]
    if len(matches) != 1:
        raise AssertionError(
            f"{result.target.name} 的来源 {source}:{source_line} 应映射到一行，实际为 {matches!r}"
        )
    return matches[0]


def mapped_source_line(
    result: generate_headers.GeneratedTarget,
    target_line: int,
) -> tuple[str, int]:
    """从生成目标行反查唯一来源文件与行号。"""
    matches: list[tuple[str, int]] = [
        (
            span.source,
            span.source_start + target_line - span.target_start,
        )
        for span in result.mappings
        if span.target_start <= target_line <= span.target_end
    ]
    if len(matches) != 1:
        raise AssertionError(
            f"{result.target.name} 的目标第 {target_line} 行应有唯一来源，实际为 {matches!r}"
        )
    return matches[0]


def geometric_declaration(
    source_lines: list[str],
) -> tuple[int, int, list[str]]:
    """按签名寻找指定引擎重载的完整模板声明。"""
    signature_lines: list[int] = [
        line_number
        for line_number, line in enumerate(source_lines, start=1)
        if line.strip() == GEOMETRIC_SIGNATURE
    ]
    if len(signature_lines) != 1:
        raise AssertionError(
            f"指定引擎重载签名应出现一次，实际为 {len(signature_lines)} 次"
        )

    signature_line: int = signature_lines[0]
    template_lines: list[int] = [
        line_number
        for line_number, line in enumerate(source_lines[: signature_line - 1], start=1)
        if line.strip().startswith("template <")
    ]
    if not template_lines:
        raise AssertionError("指定引擎重载缺少模板声明")
    declaration_start: int = template_lines[-1]
    declaration: list[str] = source_lines[declaration_start - 1 : signature_line]
    if declaration[-2].strip() != "[[nodiscard]]":
        raise AssertionError("指定引擎重载声明缺少 [[nodiscard]]")
    return declaration_start, signature_line, declaration


class HeaderSourceMappingTests(unittest.TestCase):
    """检查两个分发头文件与共享源码之间的真实映射。"""

    def test_mappings_cover_every_generated_line(self) -> None:
        """每个产物行恰好映射到来源行，且文本保持一致。"""
        self.assertEqual(set(RESULTS_BY_NAME), {"cpp17", "cpp23"})

        for result in TARGET_RESULTS:
            with self.subTest(target=result.target.name):
                generated_lines: list[str] = result.content.splitlines()
                expected_target_line: int = 1

                for span in result.mappings:
                    self.assertEqual(span.target_start, expected_target_line)
                    self.assertLessEqual(span.target_start, span.target_end)
                    self.assertEqual(
                        span.target_end - span.target_start,
                        span.source_end - span.source_start,
                    )
                    source_lines: list[str] = read_source_lines(span.source)

                    for target_line in range(span.target_start, span.target_end + 1):
                        source_line: int = span.source_start + target_line - span.target_start
                        self.assertGreaterEqual(source_line, 1)
                        self.assertLessEqual(source_line, len(source_lines))
                        self.assertEqual(
                            generated_lines[target_line - 1],
                            source_lines[source_line - 1],
                            f"{result.target.name} 第 {target_line} 行映射到 "
                            f"{span.source}:{source_line}，但文本不同",
                        )

                    expected_target_line = span.target_end + 1

                self.assertEqual(expected_target_line, len(generated_lines) + 1)

    def test_every_source_is_reachable_and_common_sources_serve_both_targets(self) -> None:
        """来源均由装配图引用，共同片段在两个版本中保持共同归属。"""
        source_files: set[str] = {
            path.relative_to(SOURCE_ROOT).as_posix()
            for path in SOURCE_ROOT.rglob("*.inc")
        }
        used_by_target: dict[str, set[str]] = {}
        # 根模板及仅含引用的装配片段不产生文本行，通过引用图核对其可达性。
        reachable: set[str] = set()

        def visit(source: str, target_sources: set[str]) -> None:
            if source in target_sources:
                return
            target_sources.add(source)
            for line in read_source_lines(source):
                match = generate_headers.INCLUDE_PATTERN.fullmatch(line)
                if match is not None:
                    included: str = posixpath.normpath(
                        posixpath.join(
                            posixpath.dirname(source), match.group(1)
                        )
                    )
                    visit(included, target_sources)

        for name, result in RESULTS_BY_NAME.items():
            target_sources: set[str] = set()
            visit(result.target.template, target_sources)
            used_by_target[name] = target_sources
            reachable.update(target_sources)
        self.assertEqual(source_files, reachable)
        common_sources: set[str] = {
            source for source in source_files if source.startswith("common/")
        }
        for name, used_sources in used_by_target.items():
            self.assertTrue(common_sources <= used_sources, name)

    def test_geometric_declaration_and_complete_body_map_both_ways(self) -> None:
        """指定引擎声明归属各自便利 API 源码，完整正文共享且可反查。"""
        self.assertEqual(set(RESULTS_BY_NAME), {"cpp17", "cpp23"})
        geometric_body_lines: list[str] = read_source_lines(GEOMETRIC_BODY_SOURCE)
        mapped_body_signatures: dict[str, tuple[tuple[str, int, int], ...]] = {}

        for target_name in ("cpp17", "cpp23"):
            result: generate_headers.GeneratedTarget = RESULTS_BY_NAME[target_name]
            convenience_source: str = f"{target_name}/convenience.inc"
            convenience_lines: list[str] = read_source_lines(convenience_source)
            declaration_start: int
            declaration_end: int
            declaration_lines: list[str]
            declaration_start, declaration_end, declaration_lines = geometric_declaration(
                convenience_lines
            )
            declaration_text: str = "\n".join(declaration_lines)
            self.assertIn(declaration_text, "\n".join(convenience_lines))
            generated_lines: list[str] = result.content.splitlines()
            mapped_declaration_target_lines: list[int] = []

            for source_line in range(declaration_start, declaration_end + 1):
                target_line: int = mapped_target_line(result, convenience_source, source_line)
                self.assertEqual(
                    mapped_source_line(result, target_line),
                    (convenience_source, source_line),
                )
                self.assertEqual(
                    generated_lines[target_line - 1],
                    convenience_lines[source_line - 1],
                )
                mapped_declaration_target_lines.append(target_line)

            self.assertEqual(
                mapped_declaration_target_lines,
                list(
                    range(
                        mapped_declaration_target_lines[0],
                        mapped_declaration_target_lines[-1] + 1,
                    )
                ),
            )
            self.assertEqual(
                "\n".join(
                    generated_lines[
                        mapped_declaration_target_lines[0] - 1 : mapped_declaration_target_lines[-1]
                    ]
                ),
                declaration_text,
            )

            body_spans: tuple[generate_headers.MappingSpan, ...] = tuple(
                span for span in result.mappings if span.source == GEOMETRIC_BODY_SOURCE
            )
            self.assertEqual(len(body_spans), 1)
            self.assertEqual(body_spans[0].source_start, 1)
            self.assertEqual(body_spans[0].source_end, len(geometric_body_lines))
            body_lines_by_source: dict[int, tuple[int, str]] = {}

            for span in body_spans:
                for source_line in range(span.source_start, span.source_end + 1):
                    target_line: int = mapped_target_line(
                        result, GEOMETRIC_BODY_SOURCE, source_line
                    )
                    self.assertEqual(
                        mapped_source_line(result, target_line),
                        (GEOMETRIC_BODY_SOURCE, source_line),
                    )
                    body_lines_by_source[source_line] = (
                        target_line,
                        generated_lines[target_line - 1],
                    )

            self.assertEqual(
                sorted(body_lines_by_source),
                list(range(1, len(geometric_body_lines) + 1)),
            )
            mapped_body_lines: list[str] = [
                body_lines_by_source[source_line][1]
                for source_line in range(1, len(geometric_body_lines) + 1)
            ]
            self.assertEqual(mapped_body_lines, geometric_body_lines)
            mapped_body_signatures[target_name] = tuple(
                (span.source, span.source_start, span.source_end) for span in body_spans
            )

        self.assertEqual(mapped_body_signatures["cpp17"], mapped_body_signatures["cpp23"])


if __name__ == "__main__":
    unittest.main()
