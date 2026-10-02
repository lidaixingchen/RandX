"""测试 LCOV 熵分支报告与结构兼容性。"""

from __future__ import annotations

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any

from inspect_entropy_coverage import (
    compare_layouts,
    entropy_branch_report,
    parse_lcov,
)


FUNCTION_START_LINE: int = 10
BRANCH_LINE: int = 11
FUNCTION_END_LINE: int = 12


class EntropyCoverageTests(unittest.TestCase):
    def test_indexed_functions_preserve_aliases_and_range(self) -> None:
        trace: str = self._trace().replace(
            f"FN:{FUNCTION_START_LINE},GetOsEntropyBytes\nFNDA:1,GetOsEntropyBytes",
            f"FNL:0,{FUNCTION_START_LINE},{FUNCTION_END_LINE}\n"
            "FNA:0,2,FillOsEntropy<NativeReader, void>\n"
            "FNA:0,3,FillOsEntropy<ScriptReader, void>",
        ).replace(
            "end_of_record", f"BRDA:{FUNCTION_END_LINE + 1},0,0,1\nend_of_record"
        )
        with TemporaryDirectory() as temporary_directory:
            records = parse_lcov(self._write_trace(Path(temporary_directory), trace))
            report: str = entropy_branch_report(records)
        self.assertEqual(len(records["RandX.hpp"].functions), 1)
        self.assertIn("FillOsEntropy<NativeReader, void>", report)
        self.assertIn("FillOsEntropy<ScriptReader, void>", report)
        self.assertIn("calls=5; branches=1/1", report)
        self.assertNotIn(f"line {FUNCTION_END_LINE + 1}", report)

    def test_legacy_function_end_line_matches_indexed_layout(self) -> None:
        legacy: str = self._trace().replace(
            f"FN:{FUNCTION_START_LINE},GetOsEntropyBytes",
            f"FN:{FUNCTION_START_LINE},{FUNCTION_END_LINE},GetOsEntropyBytes",
        )
        indexed: str = self._trace().replace(
            f"FN:{FUNCTION_START_LINE},GetOsEntropyBytes\nFNDA:1,GetOsEntropyBytes",
            f"FNL:0,{FUNCTION_START_LINE},{FUNCTION_END_LINE}\nFNA:0,1,GetOsEntropyBytes",
        )
        with TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            production = parse_lcov(self._write_trace(root, legacy, "production.info"))
            fault = parse_lcov(self._write_trace(root, indexed, "fault.info"))
        self.assertTrue(compare_layouts(production, fault)["compatible"])

    def test_function_range_changes_are_incompatible(self) -> None:
        trace: str = self._trace().replace(
            f"FN:{FUNCTION_START_LINE},GetOsEntropyBytes",
            f"FN:{FUNCTION_START_LINE},{FUNCTION_END_LINE},GetOsEntropyBytes",
        )
        with TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            production = parse_lcov(self._write_trace(root, trace, "production.info"))
            fault = parse_lcov(self._write_trace(
                root, trace.replace(f",{FUNCTION_END_LINE},Get", f",{FUNCTION_END_LINE + 1},Get"),
                "fault.info",
            ))
        self.assertFalse(compare_layouts(production, fault)["compatible"])

    def test_reports_entropy_function_branches(self) -> None:
        with TemporaryDirectory() as temporary_directory:
            report_path: Path = self._write_trace(
                Path(temporary_directory), self._trace(taken="1")
            )
            report: str = entropy_branch_report(parse_lcov(report_path))

        self.assertIn("GetOsEntropyBytes", report)
        self.assertIn("branches=1/1", report)

    def test_compatible_layout_ignores_execution_counts(self) -> None:
        with TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            production_path: Path = self._write_trace(
                root, self._trace(line_count="1", taken="0"), "production.info"
            )
            fault_path: Path = self._write_trace(
                root, self._trace(line_count="9", taken="3"), "fault.info"
            )
            production: dict[str, Any] = parse_lcov(production_path)
            fault: dict[str, Any] = parse_lcov(fault_path)

            result: dict[str, Any] = compare_layouts(production, fault)

            self.assertTrue(result["compatible"])

    def test_incompatible_branch_layout_is_reported(self) -> None:
        production_trace: str = self._trace(taken="1")
        fault_trace: str = self._trace(taken="1").replace(
            f"BRDA:{BRANCH_LINE},0,0,1", f"BRDA:{BRANCH_LINE},0,0,1\nBRDA:{BRANCH_LINE},0,1,0"
        )
        with TemporaryDirectory() as temporary_directory:
            root: Path = Path(temporary_directory)
            production: dict[str, Any] = parse_lcov(
                self._write_trace(root, production_trace, "production.info")
            )
            fault: dict[str, Any] = parse_lcov(
                self._write_trace(root, fault_trace, "fault.info")
            )

            result: dict[str, Any] = compare_layouts(production, fault)

            self.assertFalse(result["compatible"])
            differences: dict[str, Any] = result["sources"][0]
            self.assertIn("branch_sites", differences)

    @staticmethod
    def _trace(line_count: str = "1", taken: str = "1") -> str:
        return "\n".join(
            (
                "TN:",
                "SF:RandX.hpp",
                f"FN:{FUNCTION_START_LINE},GetOsEntropyBytes",
                f"FNDA:{line_count},GetOsEntropyBytes",
                f"DA:{FUNCTION_START_LINE},{line_count}",
                f"DA:{BRANCH_LINE},{line_count}",
                f"BRDA:{BRANCH_LINE},0,0,{taken}",
                "end_of_record",
                "",
            )
        )

    @staticmethod
    def _write_trace(directory: Path, contents: str, name: str = "coverage.info") -> Path:
        path: Path = directory / name
        path.write_text(contents, encoding="utf-8")
        return path


if __name__ == "__main__":
    unittest.main()
