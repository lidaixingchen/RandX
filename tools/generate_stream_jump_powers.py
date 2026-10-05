"""Generate constexpr jump powers for the built-in stream engines."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence


PROJECT_ROOT: Path = Path(__file__).resolve().parent.parent
SOURCE_ROOT: Path = PROJECT_ROOT / "src" / "header_sources"
OUTPUT_PATH: Path = SOURCE_ROOT / "common" / "engines" / "stream_jump_powers.inc"
WORD_MASKS: dict[int, int] = {32: (1 << 32) - 1, 64: (1 << 64) - 1}
STREAM_ID_HALF_BITS: int = 32


@dataclass(frozen=True)
class EngineSpec:
    """Source locations and linear transition for one built-in engine."""

    name: str
    word_bits: int
    state_words: int
    transition_source: str
    jump_source: str
    transition: Callable[[tuple[int, ...]], tuple[int, ...]]
    transition_statements: tuple[str, ...]

    @property
    def state_bits(self) -> int:
        return self.word_bits * self.state_words


def rotate_left(value: int, count: int, word_bits: int) -> int:
    mask: int = WORD_MASKS[word_bits]
    shift: int = count % word_bits
    return ((value << shift) | (value >> (word_bits - shift))) & mask


def xoshiro256_transition(state: tuple[int, ...]) -> tuple[int, ...]:
    s0: int
    s1: int
    s2: int
    s3: int
    s0, s1, s2, s3 = state
    mask: int = WORD_MASKS[64]
    t: int = (s1 << 17) & mask
    s2 ^= s0
    s3 ^= s1
    s1 ^= s2
    s0 ^= s3
    s2 ^= t
    s3 = rotate_left(s3, 45, 64)
    return s0 & mask, s1 & mask, s2 & mask, s3 & mask


def xoroshiro128_transition(state: tuple[int, ...]) -> tuple[int, ...]:
    s0: int
    s1: int
    s0, s1 = state
    mask: int = WORD_MASKS[64]
    s1 ^= s0
    s0 = rotate_left(s0, 24, 64) ^ s1 ^ ((s1 << 16) & mask)
    s1 = rotate_left(s1, 37, 64)
    return s0 & mask, s1 & mask


def xoshiro128_transition(state: tuple[int, ...]) -> tuple[int, ...]:
    s0: int
    s1: int
    s2: int
    s3: int
    s0, s1, s2, s3 = state
    mask: int = WORD_MASKS[32]
    t: int = (s1 << 9) & mask
    s2 ^= s0
    s3 ^= s1
    s1 ^= s2
    s0 ^= s3
    s2 ^= t
    s3 = rotate_left(s3, 11, 32)
    return s0 & mask, s1 & mask, s2 & mask, s3 & mask


ENGINE_SPECS: tuple[EngineSpec, ...] = (
    EngineSpec(
        name="Xoshiro256StarStar",
        word_bits=64,
        state_words=4,
        transition_source="common/engines/xoshiro256_star_star_next_definition.inc",
        jump_source="common/engines/jump_method_definitions.inc",
        transition=xoshiro256_transition,
        transition_statements=(
            "const std::uint64_t result = detail::RotL(s_[1] * 5, 7) * 9;",
            "const std::uint64_t t = s_[1] << 17;",
            "s_[2] ^= s_[0];",
            "s_[3] ^= s_[1];",
            "s_[1] ^= s_[2];",
            "s_[0] ^= s_[3];",
            "s_[2] ^= t;",
            "s_[3] = detail::RotL(s_[3], 45);",
            "return result;",
        ),
    ),
    EngineSpec(
        name="Xoroshiro128StarStar",
        word_bits=64,
        state_words=2,
        transition_source="common/engines/xoroshiro128_star_star_next_definition.inc",
        jump_source="common/engines/xoroshiro128_jump_definitions.inc",
        transition=xoroshiro128_transition,
        transition_statements=(
            "const std::uint64_t s0 = s_[0];",
            "std::uint64_t s1 = s_[1];",
            "const std::uint64_t result = detail::RotL(s0 * 5, 7) * 9;",
            "s1 ^= s0;",
            "s_[0] = detail::RotL(s0, 24) ^ s1 ^ (s1 << 16);",
            "s_[1] = detail::RotL(s1, 37);",
            "return result;",
        ),
    ),
    EngineSpec(
        name="Xoshiro128StarStar",
        word_bits=32,
        state_words=4,
        transition_source="common/engines/xoshiro128_star_star_next_definition.inc",
        jump_source="common/engines/xoshiro128_jump_definitions.inc",
        transition=xoshiro128_transition,
        transition_statements=(
            "const std::uint32_t result = detail::RotL(s_[1] * 5, 7) * 9;",
            "const std::uint32_t t = s_[1] << 9;",
            "s_[2] ^= s_[0];",
            "s_[3] ^= s_[1];",
            "s_[1] ^= s_[2];",
            "s_[0] ^= s_[3];",
            "s_[2] ^= t;",
            "s_[3] = detail::RotL(s_[3], 11);",
            "return result;",
        ),
    ),
)


def extract_braced_body(source: str, marker: str, location: str) -> str:
    """Return a C++ function body while respecting nested braces."""
    marker_position: int = source.find(marker)
    if marker_position < 0:
        raise ValueError(f"未找到函数定义：{location}::{marker}")
    opening: int = source.find("{", marker_position)
    if opening < 0:
        raise ValueError(f"函数缺少左花括号：{location}::{marker}")
    depth: int = 0
    for position in range(opening, len(source)):
        character: str = source[position]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : position]
    raise ValueError(f"函数缺少右花括号：{location}::{marker}")


def normalize_transition_statements(body: str) -> tuple[str, ...]:
    normalized: str = re.sub(r"\s+", " ", body).strip()
    return tuple(
        f"{statement.strip()};"
        for statement in normalized.split(";")
        if statement.strip()
    )


def validate_transition_source(spec: EngineSpec) -> None:
    """Ensure the transcribed recurrence still matches the maintained source."""
    path: Path = SOURCE_ROOT / spec.transition_source
    source: str = path.read_text(encoding="utf-8")
    body: str = extract_braced_body(source, f"{spec.name}::operator()() noexcept", spec.transition_source)
    actual_statements: tuple[str, ...] = normalize_transition_statements(body)
    if actual_statements != spec.transition_statements:
        raise ValueError(
            f"线性转移与生成脚本模型的语句序列不一致：{spec.transition_source}；"
            f"期望：{spec.transition_statements}；实际：{actual_statements}"
        )


def read_jump_polynomial(spec: EngineSpec, method: str) -> int:
    """Read the production jump polynomial from its maintained definition."""
    relative_path: str = spec.jump_source
    source: str = (SOURCE_ROOT / relative_path).read_text(encoding="utf-8")
    body: str = extract_braced_body(source, f"{spec.name}::{method}() noexcept", relative_path)
    if "jumpPoly(p)" not in body:
        raise ValueError(f"jump 定义未调用共享多项式内核：{relative_path}::{spec.name}::{method}")
    match: re.Match[str] | None = re.search(
        r"constexpr\s+std::uint(?:32|64)_t\s+p\[\]\s*=\s*\{(?P<values>.*?)\}\s*;",
        body,
        flags=re.DOTALL,
    )
    if match is None:
        raise ValueError(f"未找到 jump 多项式：{relative_path}::{spec.name}::{method}")
    tokens: list[str] = re.findall(r"0[xX][0-9a-fA-F]+[uUlL]*|\b\d+[uUlL]*", match.group("values"))
    values: list[int] = [int(token.rstrip("uUlL"), 0) for token in tokens]
    if len(values) != spec.state_words:
        raise ValueError(f"jump 多项式字数应为 {spec.state_words}：{relative_path}::{spec.name}::{method}")
    word_mask: int = WORD_MASKS[spec.word_bits]
    if any(value < 0 or value > word_mask for value in values):
        raise ValueError(f"jump 多项式系数超出字宽：{relative_path}::{spec.name}::{method}")
    return sum(value << (index * spec.word_bits) for index, value in enumerate(values))


def pack_state(state: Sequence[int], word_bits: int) -> int:
    return sum(word << (index * word_bits) for index, word in enumerate(state))


def unpack_state(state: int, spec: EngineSpec) -> tuple[int, ...]:
    word_mask: int = WORD_MASKS[spec.word_bits]
    return tuple(
        (state >> (index * spec.word_bits)) & word_mask
        for index in range(spec.state_words)
    )


def make_transition_matrix(spec: EngineSpec) -> tuple[int, ...]:
    """Build a column-oriented GF(2) matrix directly from one-hot states."""
    columns: list[int] = []
    for bit in range(spec.state_bits):
        state: tuple[int, ...] = unpack_state(1 << bit, spec)
        columns.append(pack_state(spec.transition(state), spec.word_bits))
    return tuple(columns)


def apply_matrix(matrix: Sequence[int], vector: int) -> int:
    result: int = 0
    remaining: int = vector
    while remaining:
        least_bit: int = remaining & -remaining
        result ^= matrix[least_bit.bit_length() - 1]
        remaining ^= least_bit
    return result


def compose_matrices(left: Sequence[int], right: Sequence[int]) -> tuple[int, ...]:
    """Return left after right for column-oriented GF(2) matrices."""
    return tuple(apply_matrix(left, column) for column in right)


def square_matrix(matrix: Sequence[int]) -> tuple[int, ...]:
    return compose_matrices(matrix, matrix)


def square_polynomial(polynomial: int, modulus: int, degree: int) -> int:
    """Square in GF(2)[x] and reduce modulo the monic state polynomial."""
    squared: int = 0
    remaining: int = polynomial
    exponent: int = 0
    while remaining:
        if remaining & 1:
            squared |= 1 << (2 * exponent)
        remaining >>= 1
        exponent += 1
    while squared.bit_length() - 1 >= degree:
        squared ^= modulus << (squared.bit_length() - 1 - degree)
    return squared


def multiply_polynomials(left: int, right: int, modulus: int, degree: int) -> int:
    """Multiply in GF(2)[x] and reduce modulo the monic state polynomial."""
    product: int = 0
    factor: int = left
    multiplier: int = right
    while multiplier:
        if multiplier & 1:
            product ^= factor
        multiplier >>= 1
        factor <<= 1
    while product.bit_length() - 1 >= degree:
        product ^= modulus << (product.bit_length() - 1 - degree)
    return product


def find_cyclic_modulus(matrix: Sequence[int], degree: int) -> int:
    """Find the degree-n recurrence for the Krylov basis generated by e0."""
    basis: dict[int, tuple[int, int]] = {}
    vector: int = 1
    for exponent in range(degree + 1):
        reduced: int = vector
        relation: int = 1 << exponent
        while reduced:
            pivot: int = reduced.bit_length() - 1
            existing: tuple[int, int] | None = basis.get(pivot)
            if existing is None:
                basis[pivot] = reduced, relation
                break
            reduced ^= existing[0]
            relation ^= existing[1]
        if not reduced:
            if exponent != degree or relation.bit_length() - 1 != degree:
                raise ValueError("转移矩阵的循环向量最小多项式次数不足状态位宽")
            return relation
        vector = apply_matrix(matrix, vector)
    raise ValueError("无法推导状态转移特征多项式")


def polynomial_matrix(polynomial: int, modulus: int, krylov: Sequence[int]) -> tuple[int, ...]:
    """Apply a polynomial in the cyclic basis and convert back to state bits."""
    degree: int = len(krylov)
    state_bits: int = degree
    rows: list[int] = []
    for state_bit in range(state_bits):
        row: int = 0
        for krylov_bit, column in enumerate(krylov):
            if column & (1 << state_bit):
                row |= 1 << krylov_bit
        rows.append(row)
    augmented: list[int] = [
        row | (1 << (degree + row_index))
        for row_index, row in enumerate(rows)
    ]
    for column in range(degree):
        pivot: int | None = next(
            (row for row in range(column, degree) if augmented[row] & (1 << column)),
            None,
        )
        if pivot is None:
            raise ValueError("Krylov 向量不能构成完整状态基")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        for row in range(degree):
            if row != column and augmented[row] & (1 << column):
                augmented[row] ^= augmented[column]
    inverse_rows: tuple[int, ...] = tuple(row >> degree for row in augmented)
    columns: list[int] = []
    for state_bit in range(state_bits):
        coordinates: int = sum(
            ((row >> state_bit) & 1) << coordinate_bit
            for coordinate_bit, row in enumerate(inverse_rows)
        )
        transformed: int = multiply_polynomials(polynomial, coordinates, modulus, degree)
        state: int = 0
        remaining: int = transformed
        while remaining:
            least_bit: int = remaining & -remaining
            state ^= krylov[least_bit.bit_length() - 1]
            remaining ^= least_bit
        columns.append(state)
    return tuple(columns)


def direct_jump_matrix(spec: EngineSpec, transition: Sequence[int], polynomial: int) -> tuple[int, ...]:
    """Apply the legacy bit-by-bit jump definition to every basis state."""
    columns: list[int] = []
    for initial_bit in range(spec.state_bits):
        current: int = 1 << initial_bit
        accumulated: int = 0
        for exponent in range(spec.state_bits):
            if polynomial & (1 << exponent):
                accumulated ^= current
            current = apply_matrix(transition, current)
        columns.append(accumulated)
    return tuple(columns)


def derive_powers(spec: EngineSpec) -> tuple[list[int], list[int]]:
    """Derive J/L powers and check them against independent matrix powers."""
    validate_transition_source(spec)
    transition_matrix: tuple[int, ...] = make_transition_matrix(spec)
    modulus: int = find_cyclic_modulus(transition_matrix, spec.state_bits)
    jump_polynomial: int = read_jump_polynomial(spec, "jump")
    long_jump_polynomial: int = read_jump_polynomial(spec, "longJump")
    krylov: list[int] = [1]
    for _ in range(1, spec.state_bits):
        krylov.append(apply_matrix(transition_matrix, krylov[-1]))
    zero_matrix: tuple[int, ...] = tuple(0 for _ in range(spec.state_bits))
    if polynomial_matrix(modulus, modulus, krylov) != zero_matrix:
        raise ValueError(f"特征多项式未消去 {spec.name} 的线性转移")

    jump_matrix: tuple[int, ...] = direct_jump_matrix(spec, transition_matrix, jump_polynomial)
    long_jump_matrix: tuple[int, ...] = direct_jump_matrix(spec, transition_matrix, long_jump_polynomial)
    jump_powers: list[int] = []
    long_jump_powers: list[int] = []
    jump_polynomial_power: int = jump_polynomial
    long_polynomial_power: int = long_jump_polynomial
    jump_matrix_power: tuple[int, ...] = jump_matrix
    long_matrix_power: tuple[int, ...] = long_jump_matrix
    for _ in range(STREAM_ID_HALF_BITS):
        if polynomial_matrix(jump_polynomial_power, modulus, krylov) != jump_matrix_power:
            raise ValueError(f"jump 多项式幂与独立 GF(2) 矩阵结果不同：{spec.name}")
        if polynomial_matrix(long_polynomial_power, modulus, krylov) != long_matrix_power:
            raise ValueError(f"longJump 多项式幂与独立 GF(2) 矩阵结果不同：{spec.name}")
        jump_powers.append(jump_polynomial_power)
        long_jump_powers.append(long_polynomial_power)
        jump_polynomial_power = square_polynomial(jump_polynomial_power, modulus, spec.state_bits)
        long_polynomial_power = square_polynomial(long_polynomial_power, modulus, spec.state_bits)
        jump_matrix_power = square_matrix(jump_matrix_power)
        long_matrix_power = square_matrix(long_matrix_power)
    if direct_jump_matrix(spec, transition_matrix, jump_polynomial) != jump_matrix:
        raise ValueError(f"jump 转移未通过全基向量校验：{spec.name}")
    if direct_jump_matrix(spec, transition_matrix, long_jump_polynomial) != long_jump_matrix:
        raise ValueError(f"longJump 转移未通过全基向量校验：{spec.name}")
    return jump_powers, long_jump_powers


def polynomial_words(polynomial: int, spec: EngineSpec) -> list[int]:
    word_mask: int = WORD_MASKS[spec.word_bits]
    return [
        (polynomial >> (word_index * spec.word_bits)) & word_mask
        for word_index in range(spec.state_words)
    ]


def format_polynomial(polynomial: int, spec: EngineSpec) -> str:
    suffix: str = "ULL" if spec.word_bits == 64 else "U"
    values: list[str] = [f"0x{word:0{spec.word_bits // 4}X}{suffix}" for word in polynomial_words(polynomial, spec)]
    return "polynomial_type{{ " + ", ".join(values) + " }}"


def format_power_table(name: str, powers: Sequence[int], spec: EngineSpec) -> str:
    entries: list[str] = [f"\t\t\t\t{format_polynomial(power, spec)}" for power in powers]
    return (
        f"\t\t\tinline static constexpr std::array<polynomial_type, powers_per_half> {name} = {{{{\n"
        + ",\n".join(entries)
        + "\n\t\t\t}};\n"
    )


def render_source() -> str:
    sections: list[str] = [
        "// Generated by tools/generate_stream_jump_powers.py from the maintained engine recurrences and jump polynomials.\n",
        "// Coefficients are little-endian in powers of the one-step state transition.\n",
        "\tnamespace detail\n\t{\n",
        "\t\ttemplate <class Engine>\n\t\tstruct StreamJumpPowerTable\n\t\t{\n",
        "\t\t\tstatic constexpr bool enabled = false;\n\t\t};\n\n",
    ]
    for spec in ENGINE_SPECS:
        jump_powers: list[int]
        long_jump_powers: list[int]
        jump_powers, long_jump_powers = derive_powers(spec)
        sections.append(
            f"\t\ttemplate <>\n\t\tstruct StreamJumpPowerTable<{spec.name}>\n\t\t{{\n"
            f"\t\t\tstatic constexpr bool enabled = true;\n"
            f"\t\t\tusing engine_type = {spec.name};\n"
            f"\t\t\tusing word_type = typename engine_type::result_type;\n"
            f"\t\t\tusing polynomial_type = std::array<word_type, std::tuple_size<typename engine_type::state_type>::value>;\n"
            f"\t\t\tstatic constexpr std::size_t powers_per_half = std::numeric_limits<std::uint32_t>::digits;\n"
            f"\t\t\tstatic_assert(std::tuple_size<typename engine_type::state_type>::value == {spec.state_words}, \"stream jump polynomial state width\");\n"
            f"\t\t\tstatic_assert(std::numeric_limits<word_type>::digits == {spec.word_bits}, \"stream jump polynomial word width\");\n"
            + format_power_table("jump_powers", jump_powers, spec)
            + format_power_table("long_jump_powers", long_jump_powers, spec)
            + "\t\t};\n\n"
        )
    sections.append("\t}\n")
    return "".join(sections)


def main(argv: Sequence[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    action: argparse._MutuallyExclusiveGroup = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true", help="write the deterministic power table")
    action.add_argument("--check", action="store_true", help="verify the generated power table")
    args: argparse.Namespace = parser.parse_args(argv)
    try:
        expected: str = render_source()
        if args.write:
            OUTPUT_PATH.write_text(expected, encoding="utf-8", newline="\n")
            return 0
        actual: str = OUTPUT_PATH.read_text(encoding="utf-8")
        if actual != expected:
            print(f"生成材料已过期：{OUTPUT_PATH.relative_to(PROJECT_ROOT)}", file=sys.stderr)
            return 1
    except (OSError, ValueError) as error:
        print(f"流跳跃幂生成失败：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
