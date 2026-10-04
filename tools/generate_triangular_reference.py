"""生成 RandTriangular 的独立高精度逆 CDF 参考数据。"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from decimal import Decimal, localcontext
from fractions import Fraction
from pathlib import Path
from typing import Sequence


ROOT: Path = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT: Path = ROOT / "tests" / "fixtures" / "triangular_reference.hpp"
REFERENCE_PRECISION_DIGITS: int = 256
CONFIRMATION_PRECISION_DIGITS: int = 512
MAX_CONFIRMATION_PRECISION_DIGITS: int = 8192
REFERENCE_GUARD_DIGITS: int = 64
ARITHMETIC_ROUNDING_ULPS: int = 6
OUTPUT_ROUNDING_ULPS: int = 1
REFERENCE_ROUNDING_ULPS: int = 1
MAX_ULP_BUDGET: int = ARITHMETIC_ROUNDING_ULPS + OUTPUT_ROUNDING_ULPS + REFERENCE_ROUNDING_ULPS
MODE_SWITCH_STRESS_MINIMUM: Fraction = Fraction.from_float(float.fromhex("-0x1.d20cf377b24f0p+697"))
MODE_SWITCH_STRESS_PEAK: Fraction = Fraction.from_float(float.fromhex("-0x1.365bdb929e7aap+696"))
MODE_SWITCH_STRESS_MAXIMUM: Fraction = Fraction.from_float(float.fromhex("0x1.600d4fe621a90p+696"))
MODE_SWITCH_STRESS_MAX_EXPONENT: int = 697


@dataclass(frozen=True)
class BinaryFormat:
    name: str
    cpp_type: str
    precision: int
    min_exponent: int
    max_exponent: int
    suffix: str
    work_precision: int
    work_min_exponent: int
    work_max_exponent: int
    canonical_bits: int


@dataclass(frozen=True)
class ParameterShape:
    name: str
    min_value: Fraction
    peak_value: Fraction
    max_value: Fraction


@dataclass(frozen=True)
class ReferenceCase:
    name: str
    min_value: Fraction
    peak_value: Fraction
    max_value: Fraction
    uniform: Fraction
    exact_quantile: Decimal
    rounded_quantile: Fraction
    check_width_scaled_error: bool
    width_scaled_budget: Fraction


FORMATS: tuple[BinaryFormat, ...] = (
    BinaryFormat("Float", "float", 24, -126, 127, "F", 53, -1022, 1023, 24),
    BinaryFormat("Double", "double", 53, -1022, 1023, "", 53, -1022, 1023, 53),
    BinaryFormat("LongDouble53", "long double", 53, -1022, 1023, "L", 53, -1022, 1023, 53),
    BinaryFormat("LongDouble64", "long double", 64, -16382, 16383, "L", 64, -16382, 16383, 64),
    BinaryFormat("LongDouble113", "long double", 113, -16382, 16383, "L", 113, -16382, 16383, 113),
)


def power_of_two(exponent: int) -> Fraction:
    if exponent >= 0:
        return Fraction(1 << exponent, 1)
    return Fraction(1, 1 << -exponent)


def floor_log2(value: Fraction) -> int:
    if value <= 0:
        raise ValueError("floor_log2 expects a positive value")
    exponent: int = value.numerator.bit_length() - value.denominator.bit_length()
    if value < power_of_two(exponent):
        exponent -= 1
    return exponent


def round_fraction_to_integer(value: Fraction) -> int:
    quotient: int
    remainder: int
    quotient, remainder = divmod(value.numerator, value.denominator)
    doubled_remainder: int = remainder * 2
    if doubled_remainder < value.denominator:
        return quotient
    if doubled_remainder > value.denominator:
        return quotient + 1
    return quotient + (quotient & 1)


def quantize_binary(value: Fraction, binary_format: BinaryFormat) -> Fraction:
    if value == 0:
        return value
    sign: int = -1 if value < 0 else 1
    magnitude: Fraction = abs(value)
    exponent: int = floor_log2(magnitude)
    min_subnormal_exponent: int = binary_format.min_exponent - (binary_format.precision - 1)
    if exponent < binary_format.min_exponent:
        quantum_exponent: int = min_subnormal_exponent
        significand: int = round_fraction_to_integer(magnitude / power_of_two(quantum_exponent))
        if significand == 0:
            return Fraction(0, 1)
        return Fraction(sign * significand, 1) * power_of_two(quantum_exponent)

    quantum_exponent = exponent - (binary_format.precision - 1)
    significand = round_fraction_to_integer(magnitude / power_of_two(quantum_exponent))
    if significand == (1 << binary_format.precision):
        significand >>= 1
        exponent += 1
        quantum_exponent += 1
    if exponent > binary_format.max_exponent:
        raise OverflowError(f"value is outside {binary_format.name}")
    return Fraction(sign * significand, 1) * power_of_two(quantum_exponent)


def next_binary_value(value: Fraction, direction: int, binary_format: BinaryFormat) -> Fraction:
    if direction not in (-1, 1):
        raise ValueError("direction must be -1 or 1")
    if value < 0:
        raise ValueError("case grid neighbors are non-negative")
    if value == 0:
        return power_of_two(binary_format.min_exponent - (binary_format.precision - 1)) * direction
    exponent: int = floor_log2(value)
    quantum_exponent: int = exponent - (binary_format.precision - 1)
    if direction < 0 and value == power_of_two(exponent) and exponent > binary_format.min_exponent:
        quantum_exponent -= 1
    return value + direction * power_of_two(quantum_exponent)


def fraction_to_decimal(value: Fraction, precision: int = CONFIRMATION_PRECISION_DIGITS) -> Decimal:
    with localcontext() as context:
        context.prec = precision
        return Decimal(value.numerator) / Decimal(value.denominator)


def quantile_reference(
    min_value: Fraction,
    peak_value: Fraction,
    max_value: Fraction,
    uniform: Fraction,
    precision: int,
) -> Decimal:
    if uniform == 0:
        return fraction_to_decimal(min_value, precision)
    if uniform == 1:
        return fraction_to_decimal(max_value, precision)
    if uniform * (max_value - min_value) == peak_value - min_value:
        return fraction_to_decimal(peak_value, precision)
    with localcontext() as context:
        context.prec = precision
        lower: Decimal = fraction_to_decimal(min_value, precision)
        mode: Decimal = fraction_to_decimal(peak_value, precision)
        upper: Decimal = fraction_to_decimal(max_value, precision)
        probability: Decimal = fraction_to_decimal(uniform, precision)
        width: Decimal = upper - lower
        left_width: Decimal = mode - lower
        right_width: Decimal = upper - mode
        mode_ratio: Decimal = left_width / width
        if probability == mode_ratio:
            return mode
        if lower == mode:
            return lower + width * probability / (Decimal(1) + (Decimal(1) - probability).sqrt())
        if mode == upper:
            distance: Decimal = (probability * width * left_width).sqrt()
            ratio: Decimal = distance / width
            if ratio <= Decimal("0.5"):
                return lower + distance
            distance_from_upper: Decimal = (right_width + (Decimal(1) - probability) * left_width) / (
                Decimal(1) + ratio
            )
            return upper - distance_from_upper
        if probability <= mode_ratio:
            distance = (probability * width * left_width).sqrt()
            ratio = distance / width
            if ratio <= Decimal("0.5"):
                return lower + distance
            distance_from_upper = (right_width + (Decimal(1) - probability) * left_width) / (
                Decimal(1) + ratio
            )
            return upper - distance_from_upper
        distance = ((Decimal(1) - probability) * width * right_width).sqrt()
        ratio = distance / width
        if ratio <= Decimal("0.5"):
            return upper - distance
        distance_from_lower: Decimal = (left_width + probability * right_width) / (Decimal(1) + ratio)
        return lower + distance_from_lower


def cdf_reference(
    min_value: Fraction, peak_value: Fraction, max_value: Fraction, sample: Decimal, precision: int
) -> Decimal:
    with localcontext() as context:
        context.prec = precision
        lower: Decimal = fraction_to_decimal(min_value, precision)
        mode: Decimal = fraction_to_decimal(peak_value, precision)
        upper: Decimal = fraction_to_decimal(max_value, precision)
        width: Decimal = upper - lower
        if sample <= lower:
            return Decimal(0)
        if sample >= upper:
            return Decimal(1)
        normalized: Decimal = (sample - lower) / width
        mode_ratio: Decimal = (mode - lower) / width
        if sample <= mode:
            if mode == lower:
                return Decimal(0)
            return normalized * normalized / mode_ratio
        if mode == upper:
            return Decimal(1)
        distance_above_mode: Decimal = normalized - mode_ratio
        return mode_ratio + Decimal(2) * distance_above_mode - distance_above_mode * distance_above_mode / (
            Decimal(1) - mode_ratio
        )


def decimal_rounding_error_bound(precision: int) -> Decimal:
    return Decimal(10) ** (-(precision - REFERENCE_GUARD_DIGITS))


def parameter_shapes(binary_format: BinaryFormat) -> tuple[ParameterShape, ...]:
    minimum_subnormal: Fraction = power_of_two(
        binary_format.min_exponent - (binary_format.precision - 1)
    )
    largest_finite: Fraction = (
        Fraction((1 << binary_format.precision) - 1, 1)
        * power_of_two(binary_format.max_exponent - (binary_format.precision - 1))
    )
    half_largest: Fraction = quantize_binary(largest_finite / 2, binary_format)
    one: Fraction = Fraction(1, 1)
    next_after_one: Fraction = one + power_of_two(-(binary_format.precision - 1))
    switch_scale_exponent: int = min(0, binary_format.max_exponent - MODE_SWITCH_STRESS_MAX_EXPONENT - 1)
    switch_scale: Fraction = power_of_two(switch_scale_exponent)
    return (
        ParameterShape("asymmetric_cross_zero", Fraction(-3), Fraction(1), Fraction(5)),
        ParameterShape("right_skew_cross_zero", Fraction(-5), Fraction(-1), Fraction(7)),
        ParameterShape("mode_at_minimum", Fraction(0), Fraction(0), Fraction(8)),
        ParameterShape("mode_at_maximum", Fraction(-8), Fraction(8), Fraction(8)),
        ParameterShape("near_zero_mode", Fraction(-1), Fraction(0), Fraction(1)),
        ParameterShape(
            "subnormal_centered",
            -minimum_subnormal * 2,
            Fraction(0),
            minimum_subnormal * 2,
        ),
        ParameterShape(
            "subnormal_width",
            minimum_subnormal,
            minimum_subnormal * 2,
            minimum_subnormal * 5,
        ),
        ParameterShape("maximum_finite_width", -half_largest, Fraction(0), half_largest),
        ParameterShape("same_sign_large", half_largest, half_largest * 3 / 2, largest_finite),
        ParameterShape(
            "high_dynamic_range_mode_switch",
            MODE_SWITCH_STRESS_MINIMUM * switch_scale,
            MODE_SWITCH_STRESS_PEAK * switch_scale,
            MODE_SWITCH_STRESS_MAXIMUM * switch_scale,
        ),
        ParameterShape("mode_near_minimum", Fraction(0), minimum_subnormal, largest_finite),
        ParameterShape("mode_near_maximum", -largest_finite, -minimum_subnormal, Fraction(0)),
        ParameterShape("adjacent_interval", one, one, next_after_one),
    )


def uniform_grid(binary_format: BinaryFormat, shape: ParameterShape) -> tuple[tuple[str, Fraction], ...]:
    lower: Fraction = quantize_binary(shape.min_value, binary_format)
    mode: Fraction = quantize_binary(shape.peak_value, binary_format)
    upper: Fraction = quantize_binary(shape.max_value, binary_format)
    width: Fraction = upper - lower
    if width <= 0:
        raise ValueError(f"invalid generated interval: {shape.name} / {binary_format.name}")
    mode_ratio: Fraction = (mode - lower) / width
    work_format: BinaryFormat = BinaryFormat(
        binary_format.name + "Work",
        "double" if binary_format.cpp_type != "long double" or binary_format.precision == 24 else "long double",
        binary_format.work_precision,
        binary_format.work_min_exponent,
        binary_format.work_max_exponent,
        "" if binary_format.cpp_type != "long double" or binary_format.precision == 24 else "L",
        binary_format.work_precision,
        binary_format.work_min_exponent,
        binary_format.work_max_exponent,
        binary_format.canonical_bits,
    )
    mode_grid_value: Fraction = quantize_binary(mode_ratio, work_format)
    kernel_width: Fraction = quantize_binary(width, work_format)
    kernel_left_width: Fraction = quantize_binary(mode - lower, work_format)
    kernel_right_width: Fraction = quantize_binary(upper - mode, work_format)
    kernel_mode_ratio: Fraction = quantize_binary(kernel_left_width / kernel_width, work_format)
    kernel_right_ratio: Fraction = quantize_binary(kernel_right_width / kernel_width, work_format)
    fixed: tuple[tuple[str, Fraction], ...] = (
        ("zero", Fraction(0)),
        ("quarter", Fraction(1, 4)),
        ("half", Fraction(1, 2)),
        ("canonical_max", Fraction((1 << binary_format.canonical_bits) - 1, 1 << binary_format.canonical_bits)),
        (
            "work_min_subnormal",
            power_of_two(work_format.min_exponent - (work_format.precision - 1)),
        ),
    )
    generated: list[tuple[str, Fraction]] = list(fixed)
    generated.extend(
        (
            ("mode_ratio_below", next_binary_value(mode_grid_value, -1, work_format)),
            ("mode_ratio_nearest", mode_grid_value),
            ("mode_ratio_above", next_binary_value(mode_grid_value, 1, work_format)),
            ("kernel_mode_ratio_below", next_binary_value(kernel_mode_ratio, -1, work_format)),
            ("kernel_mode_ratio_nearest", kernel_mode_ratio),
            ("kernel_mode_ratio_above", next_binary_value(kernel_mode_ratio, 1, work_format)),
        )
    )
    if mode_ratio > 0:
        left_switch: Fraction = mode_ratio / 4
        if 0 < left_switch < mode_ratio:
            left_grid_value: Fraction = quantize_binary(left_switch, work_format)
            generated.extend(
                (
                    ("left_anchor_switch_below", next_binary_value(left_grid_value, -1, work_format)),
                    ("left_anchor_switch_nearest", left_grid_value),
                    ("left_anchor_switch_above", next_binary_value(left_grid_value, 1, work_format)),
                )
            )
    if kernel_mode_ratio > 0:
        kernel_left_switch: Fraction = quantize_binary(kernel_mode_ratio / 4, work_format)
        if 0 < kernel_left_switch < kernel_mode_ratio:
            generated.extend(
                (
                    ("kernel_left_anchor_switch_below", next_binary_value(kernel_left_switch, -1, work_format)),
                    ("kernel_left_anchor_switch_nearest", kernel_left_switch),
                    ("kernel_left_anchor_switch_above", next_binary_value(kernel_left_switch, 1, work_format)),
                )
            )
    right_ratio: Fraction = (upper - mode) / width
    if right_ratio > 0:
        right_switch: Fraction = Fraction(1) - right_ratio / 4
        if mode_ratio < right_switch < 1:
            right_grid_value: Fraction = quantize_binary(right_switch, work_format)
            generated.extend(
                (
                    ("right_anchor_switch_below", next_binary_value(right_grid_value, -1, work_format)),
                    ("right_anchor_switch_nearest", right_grid_value),
                    ("right_anchor_switch_above", next_binary_value(right_grid_value, 1, work_format)),
                )
            )
    if kernel_right_ratio > 0:
        kernel_right_switch: Fraction = quantize_binary(
            Fraction(1) - kernel_right_ratio / 4, work_format
        )
        if kernel_mode_ratio < kernel_right_switch < 1:
            generated.extend(
                (
                    ("kernel_right_anchor_switch_below", next_binary_value(kernel_right_switch, -1, work_format)),
                    ("kernel_right_anchor_switch_nearest", kernel_right_switch),
                    ("kernel_right_anchor_switch_above", next_binary_value(kernel_right_switch, 1, work_format)),
                )
            )
    deduplicated: dict[tuple[str, Fraction], None] = {}
    for name, value in generated:
        if 0 <= value < 1:
            deduplicated[(name, value)] = None
    return tuple(deduplicated)


def create_reference_cases(binary_format: BinaryFormat) -> tuple[ReferenceCase, ...]:
    cases: list[ReferenceCase] = []
    width_scaled_budget: Fraction = Fraction(MAX_ULP_BUDGET, 1) * power_of_two(
        1 - binary_format.precision
    )
    for shape in parameter_shapes(binary_format):
        lower: Fraction = quantize_binary(shape.min_value, binary_format)
        mode: Fraction = quantize_binary(shape.peak_value, binary_format)
        upper: Fraction = quantize_binary(shape.max_value, binary_format)
        for uniform_name, raw_uniform in uniform_grid(binary_format, shape):
            uniform: Fraction = quantize_binary(raw_uniform, output_format_for_work(binary_format))
            if uniform >= 1:
                continue
            reference_scale: Decimal = max(
                abs(fraction_to_decimal(lower, CONFIRMATION_PRECISION_DIGITS)),
                abs(fraction_to_decimal(mode, CONFIRMATION_PRECISION_DIGITS)),
                abs(fraction_to_decimal(upper, CONFIRMATION_PRECISION_DIGITS)),
            )
            previous_precision: int = REFERENCE_PRECISION_DIGITS
            previous_decimal: Decimal = quantile_reference(
                lower, mode, upper, uniform, previous_precision
            )
            previous_rounded: Fraction = quantize_decimal(previous_decimal, binary_format)
            confirmation_precision: int = CONFIRMATION_PRECISION_DIGITS
            confirmed_decimal: Decimal | None = None
            while confirmation_precision <= MAX_CONFIRMATION_PRECISION_DIGITS:
                candidate: Decimal = quantile_reference(
                    lower, mode, upper, uniform, confirmation_precision
                )
                candidate_rounded: Fraction = quantize_decimal(candidate, binary_format)
                probability_decimal: Decimal = fraction_to_decimal(uniform, confirmation_precision)
                probability_scale: Decimal = min(
                    abs(probability_decimal), abs(Decimal(1) - probability_decimal)
                )
                if probability_scale == 0:
                    probability_scale = Decimal(1)
                try:
                    cdf_value: Decimal = cdf_reference(
                        lower, mode, upper, candidate, confirmation_precision
                    )
                    cdf_matches: bool = abs(cdf_value - probability_decimal) <= probability_scale * Decimal(
                        10
                    ) ** (-REFERENCE_GUARD_DIGITS)
                except ArithmeticError:
                    cdf_matches = False
                target_rounding_stable: bool = candidate_rounded == previous_rounded
                if (
                    target_rounding_stable
                    and cdf_matches
                    and abs(previous_decimal - candidate)
                    <= reference_scale * decimal_rounding_error_bound(previous_precision)
                ):
                    confirmed_decimal = candidate
                    break
                previous_precision = confirmation_precision
                previous_decimal = candidate
                previous_rounded = candidate_rounded
                confirmation_precision *= 2
            if confirmed_decimal is None:
                raise ValueError(
                    f"reference did not stabilize: {binary_format.name} / {shape.name} / {uniform_name}"
                )
            rounded: Fraction = quantize_decimal(confirmed_decimal, binary_format)
            if rounded >= upper:
                exponent = floor_log2(abs(upper)) if upper else binary_format.min_exponent
                quantum_exponent = max(exponent - (binary_format.precision - 1),
                    binary_format.min_exponent - (binary_format.precision - 1))
                if upper > 0 and upper == power_of_two(exponent) and exponent > binary_format.min_exponent:
                    quantum_exponent -= 1
                rounded = upper - power_of_two(quantum_exponent)
            rounded = max(lower, rounded)
            cases.append(
                ReferenceCase(
                    name=f"{shape.name}.{uniform_name}",
                    min_value=lower,
                    peak_value=mode,
                    max_value=upper,
                    uniform=uniform,
                    exact_quantile=confirmed_decimal,
                    rounded_quantile=rounded,
                    check_width_scaled_error=lower < 0 < upper
                        and upper - lower >= power_of_two(binary_format.min_exponent),
                    width_scaled_budget=width_scaled_budget,
                )
            )
    return tuple(cases)


def quantize_decimal(value: Decimal, binary_format: BinaryFormat) -> Fraction:
    with localcontext() as context:
        context.prec = CONFIRMATION_PRECISION_DIGITS
        numerator, denominator = value.as_integer_ratio()
    return quantize_binary(Fraction(numerator, denominator), binary_format)


def fraction_to_hex(value: Fraction, binary_format: BinaryFormat) -> str:
    if value == 0:
        return "0x0p+0" + binary_format.suffix
    sign: str = "-" if value < 0 else ""
    magnitude: Fraction = abs(value)
    exponent: int = floor_log2(magnitude)
    quantum_exponent: int = exponent - (binary_format.precision - 1)
    significand: int = round_fraction_to_integer(magnitude / power_of_two(quantum_exponent))
    if significand == (1 << binary_format.precision):
        significand >>= 1
        exponent += 1
    if exponent < binary_format.min_exponent:
        fractional_bits: int = binary_format.precision - 1
        fraction_value: int = round_fraction_to_integer(
            magnitude / power_of_two(binary_format.min_exponent - fractional_bits)
        )
        digits: int = (fractional_bits + 3) // 4
        fraction_hex: str = format(fraction_value << (digits * 4 - fractional_bits), f"0{digits}x")
        return f"{sign}0x0.{fraction_hex}p{binary_format.min_exponent:+d}{binary_format.suffix}"
    fractional_bits = binary_format.precision - 1
    fractional_value: int = significand - (1 << fractional_bits)
    digits = (fractional_bits + 3) // 4
    fraction_hex = format(fractional_value << (digits * 4 - fractional_bits), f"0{digits}x")
    return f"{sign}0x1.{fraction_hex}p{exponent:+d}{binary_format.suffix}"


def decimal_reference_text(value: Decimal, significant_digits: int = 48) -> str:
    with localcontext() as context:
        context.prec = significant_digits
        return f"{+value:.{significant_digits - 1}E}"


def output_format_for_work(binary_format: BinaryFormat) -> BinaryFormat:
    if binary_format.cpp_type == "float":
        return BinaryFormat("Work", "double", 53, -1022, 1023, "", 53, -1022, 1023, 53)
    if binary_format.cpp_type == "double" or binary_format.precision == 53:
        return BinaryFormat("Work", "double", 53, -1022, 1023, "", 53, -1022, 1023, 53)
    return BinaryFormat(
        "Work",
        "long double",
        binary_format.precision,
        binary_format.min_exponent,
        binary_format.max_exponent,
        "L",
        binary_format.precision,
        binary_format.min_exponent,
        binary_format.max_exponent,
        binary_format.precision,
    )


def render_case(case: ReferenceCase, binary_format: BinaryFormat) -> str:
    work_format: BinaryFormat = output_format_for_work(binary_format)
    target_format: BinaryFormat = binary_format
    width_budget: str = fraction_to_hex(case.width_scaled_budget, BinaryFormat(
        "Budget", "long double", 113, -16382, 16383, "L", 113, -16382, 16383, 113
    ))
    return (
        "        {\""
        + case.name
        + "\", "
        + fraction_to_hex(case.min_value, target_format)
        + ", "
        + fraction_to_hex(case.peak_value, target_format)
        + ", "
        + fraction_to_hex(case.max_value, target_format)
        + ", "
        + fraction_to_hex(case.uniform, work_format)
        + ", "
        + fraction_to_hex(case.rounded_quantile, target_format)
        + ", "
        + width_budget
        + ", "
        + ("true" if case.check_width_scaled_error else "false")
        + ", \""
        + decimal_reference_text(case.exact_quantile)
        + "\"},"
    )


def format_groups() -> tuple[tuple[str, BinaryFormat], ...]:
    return (
        ("Float", FORMATS[0]),
        ("Double", FORMATS[1]),
        ("LongDouble53", FORMATS[2]),
        ("LongDouble64", FORMATS[3]),
        ("LongDouble113", FORMATS[4]),
    )


def render_group(group_name: str, binary_format: BinaryFormat) -> str:
    cases: tuple[ReferenceCase, ...] = create_reference_cases(binary_format)
    case_type: str = "QuantileCase<float, double>" if group_name == "Float" else (
        "QuantileCase<double, double>" if group_name == "Double" else "QuantileCase<long double, long double>"
    )
    body: list[str] = [
        f"inline constexpr std::array<{case_type}, {len(cases)}> k{group_name}Cases{{{{"
    ]
    body.extend(render_case(case, binary_format) for case in cases)
    body.append("}};")
    return "\n".join(body)


def render_header() -> str:
    groups: list[str] = [render_group(group_name, binary_format) for group_name, binary_format in format_groups()]
    lines: list[str] = [
        "#ifndef RANDX_TESTS_FIXTURES_TRIANGULAR_REFERENCE_HPP",
        "#define RANDX_TESTS_FIXTURES_TRIANGULAR_REFERENCE_HPP",
        "",
        "#include <array>",
        "#include <cfloat>",
        "#include <cstddef>",
        "",
        "namespace RandXTest::TriangularReference",
        "{",
        "template <class T, class WorkT>",
        "struct QuantileCase",
        "{",
        "    const char* name;",
        "    T min_value;",
        "    T peak_value;",
        "    T max_value;",
        "    WorkT uniform;",
        "    T expected;",
        "    long double width_scaled_budget;",
        "    bool check_width_scaled_error;",
        "    const char* exact_reference;",
        "};",
        "",
        f"inline constexpr int kReferencePrecisionDigits = {REFERENCE_PRECISION_DIGITS};",
        f"inline constexpr int kConfirmationPrecisionDigits = {CONFIRMATION_PRECISION_DIGITS};",
        f"inline constexpr int kMaximumConfirmationPrecisionDigits = {MAX_CONFIRMATION_PRECISION_DIGITS};",
        f"inline constexpr int kReferenceGuardDigits = {REFERENCE_GUARD_DIGITS};",
        f"inline constexpr unsigned kArithmeticRoundingUlps = {ARITHMETIC_ROUNDING_ULPS};",
        f"inline constexpr unsigned kOutputRoundingUlps = {OUTPUT_ROUNDING_ULPS};",
        f"inline constexpr unsigned kReferenceRoundingUlps = {REFERENCE_ROUNDING_ULPS};",
        f"inline constexpr unsigned kMaximumUlps = {MAX_ULP_BUDGET};",
        "",
    ]
    lines.extend(groups[0].splitlines())
    lines.append("")
    lines.extend(groups[1].splitlines())
    for group, condition in zip(
        groups[2:],
        (
            "LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024",
            "LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384",
            "LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384",
        ),
    ):
        lines.extend(["", f"#if {condition}"])
        lines.extend(group.splitlines())
        lines.append("#endif")
    lines.extend(["", "}", "", "#endif", ""])
    return "\n".join(lines)


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="参考头文件路径")
    parser.add_argument("--check", action="store_true", help="校验现有文件与生成结果一致")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments: argparse.Namespace = parse_arguments(argv)
    output: Path = arguments.output.expanduser().resolve()
    generated: str = render_header()
    if arguments.check:
        if not output.is_file() or output.read_text(encoding="utf-8") != generated:
            print(f"参考文件与生成结果不一致：{output}")
            return 1
        print(f"参考文件已冻结：{output}")
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generated, encoding="utf-8", newline="\n")
    print(f"已生成三角分布参考：{output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
