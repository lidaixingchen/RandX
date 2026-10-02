"""验证头文件展开器的格式、递归、映射与命令行行为。"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path, PurePosixPath, PureWindowsPath

import generate_headers


PYTHON_EXECUTABLE: Path = Path(sys.executable)


class GenerateHeadersTests(unittest.TestCase):
    """使用临时来源目录验证生成规则。"""

    temporary_directory: tempfile.TemporaryDirectory[str]
    project_root: Path
    source_root: Path
    config_path: Path

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.project_root = Path(self.temporary_directory.name) / "project"
        self.source_root = self.project_root / "src" / "header_sources"
        self.source_root.mkdir(parents=True)
        self.config_path = self.source_root / "targets.json"

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_source(self, relative_path: str, content: str) -> Path:
        """在来源目录中创建 UTF-8 测试片段。"""
        path: Path = self.source_root.joinpath(*PurePosixPath(relative_path).parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content.encode("utf-8"))
        return path

    def write_targets(self, targets: list[dict[str, str]]) -> None:
        """按配置顺序写入临时目标清单。"""
        data: dict[str, list[dict[str, str]]] = {"targets": targets}
        self.config_path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

    def default_targets(self, template: str = "cpp23/header.inc") -> list[dict[str, str]]:
        """返回一组便于测试的目标配置。"""
        return [{"name": "cpp23", "output": "RandX.hpp", "template": template}]

    def call_cli(self, arguments: list[str]) -> tuple[int, str, str]:
        """捕获命令行状态及其诊断。"""
        stdout: StringIO = StringIO()
        stderr: StringIO = StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            try:
                status: int = generate_headers.run_cli(arguments, self.config_path)
            except SystemExit as error:
                status = int(error.code)
        return status, stdout.getvalue(), stderr.getvalue()

    def test_output_is_deterministic_utf8_lf_and_has_final_newline(self) -> None:
        """换行、中文、缩进与结尾格式固定。"""
        self.write_targets(self.default_targets())
        self.write_source("cpp23/header.inc", "  first\r\n\r\n中文")

        first: tuple[generate_headers.GeneratedTarget, ...] = generate_headers.generate_targets(
            self.config_path
        )
        second: tuple[generate_headers.GeneratedTarget, ...] = generate_headers.generate_targets(
            self.config_path
        )

        self.assertEqual(first, second)
        content: bytes = first[0].content.encode("utf-8")
        self.assertEqual(first[0].content, "  first\n\n中文\n")
        self.assertTrue(content.endswith(b"\n"))
        self.assertNotIn(b"\r", content)
        self.assertIn("中文".encode("utf-8"), content)

    def test_marker_inside_ordinary_code_and_comments_is_preserved(self) -> None:
        """普通字符串与说明注释中的指令字样按原文输出。"""
        self.write_targets(self.default_targets())
        content: str = (
            'constexpr auto example = "// @randx-include fragment.inc";\n'
            '// 示例指令：// @randx-include fragment.inc\n'
        )
        self.write_source("cpp23/header.inc", content)

        result: generate_headers.GeneratedTarget = generate_headers.generate_targets(self.config_path)[0]

        self.assertEqual(result.content, content)
        self.assertEqual(result.mappings[0].source, "cpp23/header.inc")

    def test_nested_and_repeated_includes_keep_every_mapping_occurrence(self) -> None:
        """嵌套片段与重复引用均展开，并可按来源或目标行检索。"""
        self.write_targets(self.default_targets())
        self.write_source(
            "cpp23/header.inc",
            "A\n// @randx-include ../common/parent.inc\n// @randx-include ../common/leaf.inc\nZ",
        )
        self.write_source("common/parent.inc", "P\n  // @randx-include leaf.inc\nQ\n")
        self.write_source("common/leaf.inc", "中文\n\n")

        result: generate_headers.GeneratedTarget = generate_headers.generate_targets(self.config_path)[0]
        self.assertEqual(result.content, "A\nP\n中文\n\nQ\n中文\n\nZ\n")
        map_directory: Path = self.project_root / "maps"
        status, _, diagnostic = self.call_cli(["--map-dir", str(map_directory)])
        self.assertEqual(status, 0, diagnostic)

        map_path: Path = map_directory / "cpp23.json"
        map_text: str = map_path.read_text(encoding="utf-8")
        parsed_map: object = json.loads(map_text)
        self.assertIsInstance(parsed_map, dict)
        map_data: dict[str, object] = parsed_map
        raw_mappings: object = map_data.get("mappings")
        self.assertIsInstance(raw_mappings, list)
        mappings: list[dict[str, object]] = [
            mapping for mapping in raw_mappings if isinstance(mapping, dict)
        ]
        leaf_mappings: list[dict[str, object]] = [
            mapping for mapping in mappings if mapping["source"] == "common/leaf.inc"
        ]
        self.assertEqual(len(leaf_mappings), 2)
        self.assertEqual(
            [(mapping["target_start"], mapping["target_end"]) for mapping in leaf_mappings],
            [(3, 4), (6, 7)],
        )
        source_line_one_occurrences: list[int] = [
            int(mapping["target_start"])
            for mapping in leaf_mappings
            if mapping["source_start"] == 1
        ]
        self.assertEqual(source_line_one_occurrences, [3, 6])
        self.assertNotIn(str(self.project_root), map_text)
        self.assertFalse(PurePosixPath(str(leaf_mappings[0]["source"])).is_absolute())
        self.assertFalse(PureWindowsPath(str(leaf_mappings[0]["source"])).is_absolute())

    def test_missing_malformed_and_cyclic_references_report_source_chain(self) -> None:
        """引用失败诊断包含当前来源行和完整递归链。"""
        self.write_targets(self.default_targets())

        self.write_source("cpp23/header.inc", "第一行\n// @randx-include ../common/missing.inc\n")
        with self.assertRaises(generate_headers.GenerationError) as missing:
            generate_headers.generate_targets(self.config_path)
        self.assertIn("cpp23/header.inc:2", str(missing.exception))
        self.assertIn("cpp23/header.inc -> common/missing.inc", str(missing.exception))

        self.write_source("cpp23/header.inc", "正常\n// @randx-include\n")
        with self.assertRaises(generate_headers.GenerationError) as malformed:
            generate_headers.generate_targets(self.config_path)
        self.assertIn("引用语法错误", str(malformed.exception))
        self.assertIn("cpp23/header.inc:2", str(malformed.exception))

        self.write_source("cpp23/header.inc", "// @randx-include ../common/a.inc\n")
        self.write_source("common/a.inc", "// @randx-include b.inc\n")
        self.write_source("common/b.inc", "// @randx-include a.inc\n")
        with self.assertRaises(generate_headers.GenerationError) as cycle:
            generate_headers.generate_targets(self.config_path)
        self.assertIn("循环引用", str(cycle.exception))
        self.assertIn("common/b.inc:1", str(cycle.exception))
        self.assertIn(
            "cpp23/header.inc -> common/a.inc -> common/b.inc -> common/a.inc",
            str(cycle.exception),
        )

    def test_all_targets_expand_before_write_begins(self) -> None:
        """后续目标失败时不会留下先前目标的部分写入。"""
        targets: list[dict[str, str]] = [
            {"name": "cpp23", "output": "RandX.hpp", "template": "cpp23/header.inc"},
            {"name": "cpp17", "output": "RandX_Cpp17.hpp", "template": "cpp17/header.inc"},
        ]
        self.write_targets(targets)
        self.write_source("cpp23/header.inc", "valid\n")
        self.write_source("cpp17/header.inc", "// @randx-include missing.inc\n")

        status: int
        status, _, diagnostic = self.call_cli(["--write"])

        self.assertEqual(status, 1)
        self.assertIn("cpp17/header.inc:1", diagnostic)
        self.assertFalse((self.project_root / "RandX.hpp").exists())
        self.assertFalse((self.project_root / "RandX_Cpp17.hpp").exists())

    def test_duplicate_target_names_fail_before_header_or_map_writes(self) -> None:
        """映射文件名重复时，在任何产物写入前报告配置位置。"""
        targets: list[dict[str, str]] = [
            {"name": "cpp23", "output": "RandX.hpp", "template": "cpp23/header.inc"},
            {"name": "cpp23", "output": "RandX_Cpp17.hpp", "template": "cpp17/header.inc"},
        ]
        self.write_targets(targets)
        self.write_source("cpp23/header.inc", "valid\n")
        self.write_source("cpp17/header.inc", "valid\n")
        map_directory: Path = self.project_root / "maps"

        status, _, diagnostic = self.call_cli(
            ["--write", "--map-dir", str(map_directory)]
        )

        self.assertEqual(status, 1)
        self.assertIn("来源映射位置重复", diagnostic)
        self.assertIn("targets[0].name", diagnostic)
        self.assertIn("targets[1].name", diagnostic)
        self.assertFalse((self.project_root / "RandX.hpp").exists())
        self.assertFalse((self.project_root / "RandX_Cpp17.hpp").exists())
        self.assertFalse(map_directory.exists())

    def test_normalized_output_aliases_fail_before_writes(self) -> None:
        """消去点段后的同一输出位置不能被多个目标占用。"""
        targets: list[dict[str, str]] = [
            {"name": "cpp23", "output": "generated/../RandX.hpp", "template": "cpp23/header.inc"},
            {"name": "cpp17", "output": "RandX.hpp", "template": "cpp17/header.inc"},
        ]
        self.write_targets(targets)
        self.write_source("cpp23/header.inc", "first\n")
        self.write_source("cpp17/header.inc", "second\n")
        map_directory: Path = self.project_root / "maps"

        status, _, diagnostic = self.call_cli(
            ["--write", "--map-dir", str(map_directory)]
        )

        self.assertEqual(status, 1)
        self.assertIn("目标输出位置重复", diagnostic)
        self.assertIn("targets[0].output", diagnostic)
        self.assertIn("targets[1].output", diagnostic)
        self.assertFalse((self.project_root / "RandX.hpp").exists())
        self.assertFalse(map_directory.exists())

    def test_windows_case_colliding_outputs_fail_before_writes(self) -> None:
        """大小写不同但 Windows 上相同的输出路径不能并存。"""
        targets: list[dict[str, str]] = [
            {"name": "cpp23", "output": "RandX.hpp", "template": "cpp23/header.inc"},
            {"name": "cpp17", "output": "randx.hpp", "template": "cpp17/header.inc"},
        ]
        self.write_targets(targets)
        self.write_source("cpp23/header.inc", "first\n")
        self.write_source("cpp17/header.inc", "second\n")

        status, _, diagnostic = self.call_cli(["--write"])

        self.assertEqual(status, 1)
        self.assertIn("目标输出位置重复", diagnostic)
        self.assertFalse((self.project_root / "RandX.hpp").exists())

    def test_backslashes_in_template_and_output_paths_are_rejected(self) -> None:
        """配置路径统一采用 POSIX 分隔符并在写入前给出配置位置。"""
        self.write_targets(
            [{"name": "cpp23", "output": "RandX.hpp", "template": r"cpp23\header.inc"}]
        )
        map_directory: Path = self.project_root / "maps"

        status, _, diagnostic = self.call_cli(
            ["--write", "--map-dir", str(map_directory)]
        )

        self.assertEqual(status, 1)
        self.assertIn("POSIX '/' 分隔符", diagnostic)
        self.assertIn("targets[0].template", diagnostic)
        self.assertFalse((self.project_root / "RandX.hpp").exists())
        self.assertFalse(map_directory.exists())

        self.write_targets(
            [{"name": "cpp23", "output": r"generated\RandX.hpp", "template": "cpp23/header.inc"}]
        )
        self.write_source("cpp23/header.inc", "content\n")

        status, _, diagnostic = self.call_cli(
            ["--write", "--map-dir", str(map_directory)]
        )

        self.assertEqual(status, 1)
        self.assertIn("POSIX '/' 分隔符", diagnostic)
        self.assertIn("targets[0].output", diagnostic)
        self.assertFalse((self.project_root / "RandX.hpp").exists())
        self.assertFalse(map_directory.exists())

    def test_backslash_in_include_reports_location_and_reference_chain(self) -> None:
        """引用路径中的反斜杠不会按宿主平台解释，并指出完整来源链。"""
        self.write_targets(self.default_targets())
        self.write_source(
            "cpp23/header.inc",
            "// @randx-include nested\\fragment.inc\n",
        )
        map_directory: Path = self.project_root / "maps"

        status, _, diagnostic = self.call_cli(
            ["--write", "--map-dir", str(map_directory)]
        )

        self.assertEqual(status, 1)
        self.assertIn("POSIX '/' 分隔符", diagnostic)
        self.assertIn("cpp23/header.inc:1", diagnostic)
        self.assertIn("引用链：cpp23/header.inc", diagnostic)
        self.assertFalse((self.project_root / "RandX.hpp").exists())
        self.assertFalse(map_directory.exists())

    def test_second_write_preserves_unchanged_file_mtime(self) -> None:
        """重复写入不触碰字节未变化的产物。"""
        self.write_targets(self.default_targets())
        self.write_source("cpp23/header.inc", "stable\n")

        status, _, diagnostic = self.call_cli(["--write"])
        self.assertEqual(status, 0, diagnostic)
        output_path: Path = self.project_root / "RandX.hpp"
        original_mtime: int = output_path.stat().st_mtime_ns
        status, _, diagnostic = self.call_cli(["--write"])

        self.assertEqual(status, 0, diagnostic)
        self.assertEqual(output_path.stat().st_mtime_ns, original_mtime)

    def test_check_compares_bytes_and_does_not_rewrite_mismatch(self) -> None:
        """检查报告首处差异，并保持已经存在的错误字节。"""
        self.write_targets(self.default_targets())
        self.write_source("cpp23/header.inc", "expected\n")
        status, _, diagnostic = self.call_cli(["--write"])
        self.assertEqual(status, 0, diagnostic)

        status, _, diagnostic = self.call_cli(["--check"])
        self.assertEqual(status, 0, diagnostic)
        output_path: Path = self.project_root / "RandX.hpp"
        output_path.write_bytes(b"changed\n")
        changed_bytes: bytes = output_path.read_bytes()
        status, _, diagnostic = self.call_cli(["--check"])

        self.assertEqual(status, 1)
        self.assertIn("RandX.hpp 第 1 行", diagnostic)
        self.assertIn("来源 cpp23/header.inc:1", diagnostic)
        self.assertEqual(output_path.read_bytes(), changed_bytes)

    def test_output_modes_are_exclusive_and_output_directory_is_explicit(self) -> None:
        """写入模式互斥，临时输出只更新指定目录。"""
        self.write_targets(self.default_targets())
        self.write_source("cpp23/header.inc", "content\n")
        status, _, diagnostic = self.call_cli(["--write", "--output-dir", str(self.project_root / "out")])
        self.assertEqual(status, 2)
        self.assertIn("not allowed with argument", diagnostic)

        output_directory: Path = self.project_root / "out"
        status, _, diagnostic = self.call_cli(["--output-dir", str(output_directory)])
        self.assertEqual(status, 0, diagnostic)
        self.assertEqual((output_directory / "RandX.hpp").read_bytes(), b"content\n")
        self.assertFalse((self.project_root / "RandX.hpp").exists())

    def test_map_only_writes_maps_without_header_outputs(self) -> None:
        """仅指定映射目录时只生成来源映射。"""
        self.write_targets(self.default_targets())
        self.write_source("cpp23/header.inc", "content\n")
        map_directory: Path = self.project_root / "maps"

        status, _, diagnostic = self.call_cli(["--map-dir", str(map_directory)])

        self.assertEqual(status, 0, diagnostic)
        self.assertTrue((map_directory / "cpp23.json").exists())
        self.assertFalse((self.project_root / "RandX.hpp").exists())

    def test_cli_finds_configuration_from_script_when_cwd_differs(self) -> None:
        """CLI 配置定位只依赖脚本布局，与当前工作目录无关。"""
        self.write_targets(self.default_targets())
        self.write_source("cpp23/header.inc", "from-script\n")
        script_directory: Path = self.project_root / "tools"
        script_directory.mkdir(parents=True)
        copied_script: Path = script_directory / "generate_headers.py"
        shutil.copyfile(Path(generate_headers.__file__), copied_script)
        unrelated_directory: Path = Path(self.temporary_directory.name) / "elsewhere"
        unrelated_directory.mkdir()

        completed: subprocess.CompletedProcess[str] = subprocess.run(
            [str(PYTHON_EXECUTABLE), "-X", "utf8", str(copied_script), "--write"],
            cwd=unrelated_directory,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual((self.project_root / "RandX.hpp").read_bytes(), b"from-script\n")
        self.assertNotIn(str(self.project_root), completed.stdout + completed.stderr)

    def test_check_missing_output_only_writes_explicit_maps(self) -> None:
        """检查缺失产物时仅允许显式指定的映射目录写入。"""
        self.write_targets(self.default_targets())
        self.write_source("cpp23/header.inc", "content\n")
        map_directory: Path = self.project_root / "maps"

        status, _, diagnostic = self.call_cli(["--check", "--map-dir", str(map_directory)])

        self.assertEqual(status, 1)
        self.assertIn("生成结果缺失：RandX.hpp", diagnostic)
        self.assertFalse((self.project_root / "RandX.hpp").exists())
        self.assertEqual([path.name for path in map_directory.iterdir()], ["cpp23.json"])

    def test_config_order_and_common_source_mappings_across_targets(self) -> None:
        """配置顺序决定目标顺序，共同来源保留各目标的独立展开位置。"""
        targets: list[dict[str, str]] = [
            {"name": "cpp17", "output": "RandX_Cpp17.hpp", "template": "cpp17/header.inc"},
            {"name": "cpp23", "output": "RandX.hpp", "template": "cpp23/header.inc"},
        ]
        self.write_targets(targets)
        self.write_source("cpp17/header.inc", "// @randx-include ../common/body.inc\n")
        self.write_source("cpp23/header.inc", "prefix\n// @randx-include ../common/body.inc\n")
        self.write_source("common/body.inc", "共同正文\n")

        results: tuple[generate_headers.GeneratedTarget, ...] = generate_headers.generate_targets(
            self.config_path
        )

        self.assertEqual([result.target.name for result in results], ["cpp17", "cpp23"])
        self.assertEqual([result.content for result in results], ["共同正文\n", "prefix\n共同正文\n"])
        shared_spans: list[generate_headers.MappingSpan] = [
            span for result in results for span in result.mappings if span.source == "common/body.inc"
        ]
        self.assertEqual([span.target_start for span in shared_spans], [1, 2])
        self.assertEqual([span.source_start for span in shared_spans], [1, 1])


if __name__ == "__main__":
    unittest.main()
