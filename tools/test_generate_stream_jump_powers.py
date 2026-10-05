"""Tests for the stream jump power generator and its GF(2) reference."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


TOOLS_DIRECTORY: Path = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIRECTORY))

import generate_stream_jump_powers as stream_powers


class StreamJumpPowerGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.rendered_source: str = stream_powers.render_source()

    def test_generated_source_matches_maintained_material(self) -> None:
        actual_source: str = stream_powers.OUTPUT_PATH.read_text(encoding="utf-8")
        self.assertEqual(actual_source, self.rendered_source)

    def test_jump_and_long_jump_powers_match_independent_matrices(self) -> None:
        for spec in stream_powers.ENGINE_SPECS:
            with self.subTest(engine=spec.name):
                jump_powers, long_jump_powers = stream_powers.derive_powers(spec)
                self.assertEqual(len(jump_powers), stream_powers.STREAM_ID_HALF_BITS)
                self.assertEqual(len(long_jump_powers), stream_powers.STREAM_ID_HALF_BITS)
                self.assertEqual(
                    jump_powers[0],
                    stream_powers.read_jump_polynomial(spec, "jump"),
                )
                self.assertEqual(
                    long_jump_powers[0],
                    stream_powers.read_jump_polynomial(spec, "longJump"),
                )
                for polynomial in (*jump_powers, *long_jump_powers):
                    self.assertLessEqual(polynomial.bit_length(), spec.state_bits)

    def test_source_recurrences_are_traceable(self) -> None:
        for spec in stream_powers.ENGINE_SPECS:
            with self.subTest(engine=spec.name):
                stream_powers.validate_transition_source(spec)
                for method in ("jump", "longJump"):
                    polynomial: int = stream_powers.read_jump_polynomial(spec, method)
                    self.assertGreater(polynomial, 0)

    def test_source_recurrences_reject_reordered_or_extra_updates(self) -> None:
        spec: stream_powers.EngineSpec = next(
            item for item in stream_powers.ENGINE_SPECS if item.name == "Xoshiro256StarStar"
        )
        source_path: Path = stream_powers.SOURCE_ROOT / spec.transition_source
        original_lines: list[str] = source_path.read_text(encoding="utf-8").splitlines()

        reordered_lines: list[str] = original_lines.copy()
        first_update_index: int = next(
            index for index, line in enumerate(reordered_lines) if line.strip() == "s_[2] ^= s_[0];"
        )
        dependent_update_index: int = next(
            index for index, line in enumerate(reordered_lines) if line.strip() == "s_[1] ^= s_[2];"
        )
        reordered_lines[first_update_index], reordered_lines[dependent_update_index] = (
            reordered_lines[dependent_update_index],
            reordered_lines[first_update_index],
        )

        extra_update_lines: list[str] = original_lines.copy()
        return_index: int = next(
            index for index, line in enumerate(extra_update_lines) if line.strip() == "return result;"
        )
        extra_update_lines.insert(return_index, "\t\ts_[0] ^= s_[1];")

        mutation_cases: tuple[tuple[str, list[str]], ...] = (
            ("reordered", reordered_lines),
            ("extra update", extra_update_lines),
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root: Path = Path(temporary_directory)
            temporary_source: Path = temporary_root / spec.transition_source
            temporary_source.parent.mkdir(parents=True)
            with patch.object(stream_powers, "SOURCE_ROOT", temporary_root):
                for mutation, lines in mutation_cases:
                    with self.subTest(mutation=mutation):
                        temporary_source.write_text("\n".join(lines) + "\n", encoding="utf-8")
                        with self.assertRaisesRegex(ValueError, "语句序列不一致"):
                            stream_powers.validate_transition_source(spec)


if __name__ == "__main__":
    unittest.main()
