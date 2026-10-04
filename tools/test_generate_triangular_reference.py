"""独立校验三角分布高精度参考生成器。"""

from __future__ import annotations

import sys
import unittest
from decimal import Decimal
from fractions import Fraction
from pathlib import Path


TOOLS_DIRECTORY: Path = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIRECTORY))

import generate_triangular_reference as reference


class BinaryQuantizationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.small_format: reference.BinaryFormat = reference.BinaryFormat(
            "Small", "float", 3, -2, 2, "F", 3, -2, 2, 3
        )

    def test_rounds_ties_to_even(self) -> None:
        self.assertEqual(
            reference.quantize_binary(Fraction(9, 8), self.small_format), Fraction(1, 1)
        )
        self.assertEqual(
            reference.quantize_binary(Fraction(11, 8), self.small_format), Fraction(3, 2)
        )

    def test_rounds_subnormal_boundaries(self) -> None:
        minimum_subnormal: Fraction = Fraction(1, 16)
        self.assertEqual(
            reference.quantize_binary(minimum_subnormal / 2, self.small_format), Fraction(0)
        )
        self.assertEqual(
            reference.quantize_binary(minimum_subnormal * Fraction(3, 2), self.small_format),
            minimum_subnormal * 2,
        )
        self.assertEqual(
            reference.quantize_binary(Fraction(15, 64), self.small_format), Fraction(1, 4)
        )

    def test_hex_literal_round_trips_format_boundaries(self) -> None:
        minimum_subnormal: Fraction = Fraction(1, 16)
        maximum_finite: Fraction = Fraction(7, 4) * 4
        self.assertEqual(
            reference.fraction_to_hex(minimum_subnormal, self.small_format), "0x0.4p-2F"
        )
        self.assertEqual(
            reference.fraction_to_hex(maximum_finite, self.small_format), "0x1.cp+2F"
        )


class IndependentReferenceTests(unittest.TestCase):
    def test_converted_reference_obeys_half_open_interval(self) -> None:
        for binary_format in reference.FORMATS:
            for case in reference.create_reference_cases(binary_format):
                with self.subTest(format=binary_format.name, case=case.name):
                    self.assertGreaterEqual(case.rounded_quantile, case.min_value)
                    self.assertLess(case.rounded_quantile, case.max_value)
                    if case.check_width_scaled_error:
                        self.assertLess(case.min_value, 0)
                        self.assertLess(0, case.max_value)

        double_cases: tuple[reference.ReferenceCase, ...] = reference.create_reference_cases(reference.FORMATS[1])
        self.assertTrue(any(case.name.startswith("high_dynamic_range_mode_switch.kernel_mode_ratio_")
            for case in double_cases))
        self.assertTrue(any(case.name == "high_dynamic_range_mode_switch.kernel_left_anchor_switch_below"
            for case in double_cases))
        self.assertTrue(any(case.name == "high_dynamic_range_mode_switch.kernel_left_anchor_switch_above"
            for case in double_cases))
        near_minimum_cases: tuple[reference.ReferenceCase, ...] = tuple(
            case for case in double_cases if case.name.startswith("mode_near_minimum.")
        )
        self.assertTrue(near_minimum_cases)
        self.assertFalse(any(case.check_width_scaled_error for case in near_minimum_cases))

    def test_inverse_cdf_and_cdf_agree_for_both_branches(self) -> None:
        lower: Fraction = Fraction(-3)
        mode: Fraction = Fraction(1)
        upper: Fraction = Fraction(5)
        for uniform in (Fraction(1, 16), Fraction(1, 4), Fraction(3, 4), Fraction(15, 16)):
            with self.subTest(uniform=uniform):
                sample: Decimal = reference.quantile_reference(lower, mode, upper, uniform, 256)
                probability: Decimal = reference.fraction_to_decimal(uniform, 256)
                cdf: Decimal = reference.cdf_reference(lower, mode, upper, sample, 256)
                self.assertLessEqual(abs(cdf - probability), Decimal("1e-240"))

    def test_endpoint_modes_and_zero_quantile(self) -> None:
        zero: Fraction = Fraction(0)
        one: Fraction = Fraction(1)
        self.assertEqual(
            reference.quantile_reference(zero, zero, Fraction(8), Fraction(0), 128), Decimal(0)
        )
        self.assertEqual(
            reference.quantile_reference(Fraction(-8), zero, zero, Fraction(0), 128),
            Decimal(-8),
        )

    def test_reference_rounding_is_stable_across_precisions(self) -> None:
        cases: tuple[reference.ReferenceCase, ...] = reference.create_reference_cases(reference.FORMATS[0])
        self.assertGreater(len(cases), 0)
        self.assertEqual(len({case.name for case in cases}), len(cases))
        for case in cases:
            with self.subTest(case=case.name):
                first: Decimal = reference.quantile_reference(
                    case.min_value,
                    case.peak_value,
                    case.max_value,
                    case.uniform,
                    reference.REFERENCE_PRECISION_DIGITS,
                )
                second: Decimal = reference.quantile_reference(
                    case.min_value,
                    case.peak_value,
                    case.max_value,
                    case.uniform,
                    reference.CONFIRMATION_PRECISION_DIGITS,
                )
                self.assertEqual(
                    reference.quantize_decimal(first, reference.FORMATS[0]),
                    reference.quantize_decimal(second, reference.FORMATS[0]),
                )

    def test_header_rendering_is_deterministic_and_records_budgets(self) -> None:
        first: str = reference.render_header()
        second: str = reference.render_header()
        self.assertEqual(first, second)
        self.assertIn("kArithmeticRoundingUlps", first)
        self.assertIn("kMaximumUlps", first)
        self.assertIn("exact_reference", first)
        self.assertIn("kLongDouble113Cases", first)
        self.assertIn("kernel_right_anchor_switch_nearest", first)


if __name__ == "__main__":
    unittest.main()
