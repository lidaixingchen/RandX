#!/usr/bin/env python3
"""审计最终优化链接产物中 RandX 敏感材料的完整擦除。"""

from __future__ import annotations

import argparse
import dataclasses
import enum
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
from collections.abc import Iterable, Sequence


EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_UNDETERMINED = 2
SUPPORTED_MINGW_GCC_VERSIONS: tuple[str, ...] = ("16.1.0", "16.2.0")
CHACHA_STATE_BYTES = 12 * 4
CHACHA_BLOCK_BYTES = 16 * 4
CHACHA_SEED_BYTES = 32 + 12
DIRECT_MATERIAL_BYTES = 37
SCOPED_MATERIAL_BYTES = 43
EXCEPTION_MATERIAL_BYTES = 41

PROBE_SOURCE = pathlib.Path("tests/security/secure_wipe_codegen.cpp")
HEADER_NAMES = {
    "cpp17": "RandX_Cpp17.hpp",
    "cpp23": "RandX.hpp",
}
MUTATION_NAMES = (
    "global",
    "direct",
    "scoped",
    "exception",
    "generate_state",
    "generate_working",
    "reseed_seed",
    "reseed_old_cache",
    "move_construct_source_state",
    "move_construct_source_cache",
    "move_assign_destination_state",
    "move_assign_destination_cache",
    "move_assign_source_state",
    "move_assign_source_cache",
    "destructor_state",
    "destructor_cache",
)
MUTATION_FAILURE_CHECKS = {
    "global": ("direct", "scoped"),
    "direct": ("direct",),
    "scoped": ("scoped",),
    "exception": ("exception",),
    "generate_state": ("generate_state",),
    "generate_working": ("generate_working",),
    "reseed_seed": ("reseed_seed",),
    "reseed_old_cache": ("reseed_old_cache",),
    "move_construct_source_state": ("move_construct_source_state",),
    "move_construct_source_cache": ("move_construct_source_cache",),
    "move_assign_destination_state": ("move_assign_destination_state",),
    "move_assign_destination_cache": ("move_assign_destination_cache",),
    "move_assign_source_state": ("move_assign_source_state",),
    "move_assign_source_cache": ("move_assign_source_cache",),
    "destructor_state": ("destructor_state",),
    "destructor_cache": ("destructor_cache",),
}


class Status(enum.Enum):
    PASS = "pass"
    FAIL = "fail"
    UNDETERMINED = "undetermined"


@dataclasses.dataclass(frozen=True)
class Address:
    base: str
    offset: int = 0

    def add(self, amount: int) -> Address:
        return Address(self.base, self.offset + amount)


@dataclasses.dataclass(frozen=True)
class Constant:
    value: int


@dataclasses.dataclass(frozen=True)
class Instruction:
    address: int
    mnemonic: str
    operands: str
    source_path: str | None
    source_line: int | None


@dataclasses.dataclass
class FunctionCode:
    name: str
    start: int
    instructions: list[Instruction]
    exception_sites: list[tuple[int, int, int]] | None = None
    local_materials: dict[str, Address] | None = None


@dataclasses.dataclass(frozen=True)
class WipeRange:
    start: Address
    size: int
    first_instruction: int
    last_instruction: int
    form: str


@dataclasses.dataclass
class CaseResult:
    status: Status
    reasons: list[str]
    evidence: dict[str, object]


def _decode(value: bytes) -> str:
    return value.decode("utf-8", errors="replace")


def _run(command: Sequence[str], cwd: pathlib.Path) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def _capture(command: Sequence[str], cwd: pathlib.Path) -> tuple[int, str, str]:
    result = _run(command, cwd)
    return result.returncode, _decode(result.stdout), _decode(result.stderr)


def parse_objdump(text: str) -> list[FunctionCode]:
    functions: list[FunctionCode] = []
    current: FunctionCode | None = None
    source_path: str | None = None
    source_line: int | None = None
    function_pattern = re.compile(r"^\s*([0-9a-fA-F]+)\s+<(.+)>:\s*$")
    instruction_pattern = re.compile(
        r"^\s*([0-9a-fA-F]+):\s+(?:(?:[0-9a-fA-F]{2})\s+)+"
        r"([A-Za-z][A-Za-z0-9_.]*)\s*(.*?)\s*$"
    )
    source_pattern = re.compile(r"^(.*):(\d+)(?::\d+)?\s*$")

    for line in text.splitlines():
        function_match = function_pattern.match(line)
        if function_match:
            current = FunctionCode(function_match.group(2), int(function_match.group(1), 16), [])
            functions.append(current)
            source_path = None
            source_line = None
            continue

        source_match = source_pattern.match(line.strip())
        if source_match and not re.match(r"^[0-9a-fA-F]+:\s", line.strip()):
            source_path = source_match.group(1)
            source_line = int(source_match.group(2))
            continue

        instruction_match = instruction_pattern.match(line)
        if instruction_match and current is not None:
            current.instructions.append(
                Instruction(
                    address=int(instruction_match.group(1), 16),
                    mnemonic=instruction_match.group(2).lower(),
                    operands=instruction_match.group(3).strip(),
                    source_path=source_path,
                    source_line=source_line,
                )
            )
    return functions


def _register(value: str) -> str | None:
    match = re.fullmatch(r"%([a-z][a-z0-9]*)", value.strip(), flags=re.IGNORECASE)
    if not match:
        return None
    name = match.group(1).lower()
    aliases = {"eax": "rax", "ax": "rax", "al": "rax", "ecx": "rcx", "cl": "rcx", "edx": "rdx", "dl": "rdx", "edi": "rdi", "esi": "rsi", "ebx": "rbx"}
    if name in aliases:
        return aliases[name]
    return re.sub(r"^(r\d+)[dwb]$", r"\1", name)


def _immediate(value: str) -> int | None:
    match = re.fullmatch(r"\$(-?(?:0x[0-9a-f]+|[0-9]+))", value.strip(), flags=re.IGNORECASE)
    if not match:
        return None
    return int(match.group(1), 0)


def _memory(value: str) -> tuple[int, str, str | None, int] | None:
    match = re.fullmatch(
        r"(-?(?:0x[0-9a-f]+|[0-9]+))?\(%([a-z][a-z0-9]*)"
        r"(?:,%([a-z][a-z0-9]*)(?:,([1248]))?)?\)",
        value.strip(),
        flags=re.IGNORECASE,
    )
    if not match:
        return None
    displacement = int(match.group(1), 0) if match.group(1) else 0
    scale = int(match.group(4) or "1")
    return displacement, match.group(2).lower(), match.group(3).lower() if match.group(3) else None, scale


def _split_operands(operands: str) -> tuple[str, ...]:
    items: list[str] = []
    depth = 0
    start = 0
    for index, character in enumerate(operands):
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == "," and depth == 0:
            items.append(operands[start:index].strip())
            start = index + 1
    if operands[start:].strip():
        items.append(operands[start:].strip())
    return tuple(items)


def _normalize_symbol(value: object) -> str:
    return type(value).__name__


def _execute_register_instruction(
    instruction: Instruction,
    registers: dict[str, Address | Constant],
) -> None:
    mnemonic = instruction.mnemonic
    operands = _split_operands(instruction.operands)
    if mnemonic == "call":
        clobbered = ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11")
        if "consumeBytes" in instruction.operands:
            clobbered = ("rax", "rcx", "rdx", "r8")
        for register in clobbered:
            registers.pop(register, None)
        return
    if mnemonic in {"lea", "leaq", "leal"} and len(operands) == 2:
        memory = _memory(operands[0])
        destination = _register(operands[1])
        if destination is None or memory is None or memory[2] is not None:
            if destination is not None:
                registers.pop(destination, None)
            return
        displacement, base_register, _, _ = memory
        base_value = registers.get(base_register)
        if base_register in {"rsp", "rbp"}:
            registers[destination] = Address(base_register, displacement)
        elif isinstance(base_value, Address):
            registers[destination] = base_value.add(displacement)
        else:
            registers.pop(destination, None)
        return
    if mnemonic.startswith("mov") and len(operands) == 2:
        destination = _register(operands[1])
        if destination is None:
            memory = _memory(operands[1])
            source = registers.get(_register(operands[0]) or "")
            if memory is not None and memory[1] in {"rsp", "rbp"} and memory[2] is None:
                key = f"memory:{memory[1]}:{memory[0]}"
                if source is not None:
                    registers[key] = source
                else:
                    registers.pop(key, None)
            return
        source_register = _register(operands[0])
        source_value = _immediate(operands[0])
        if source_register is not None and source_register in registers:
            registers[destination] = registers[source_register]
        elif source_value is not None:
            registers[destination] = Constant(source_value)
        else:
            memory = _memory(operands[0])
            saved = registers.get(f"memory:{memory[1]}:{memory[0]}") if memory is not None and memory[2] is None else None
            if saved is not None:
                registers[destination] = saved
            else:
                registers.pop(destination, None)
        return
    if mnemonic == "xchg" and len(operands) == 2 and operands[0] == operands[1]:
        return
    if mnemonic in {"add", "addq", "sub", "subq", "inc", "incq"} and operands:
        destination = _register(operands[-1])
        if destination is None:
            return
        amount = 1 if mnemonic.startswith("inc") else _immediate(operands[0])
        source_register = _register(operands[0])
        source_value = registers.get(source_register or "")
        old_value = registers.get(destination)
        if mnemonic.startswith("sub") and isinstance(old_value, Address) and isinstance(source_value, Address) and old_value.base == source_value.base:
            registers[destination] = Constant(old_value.offset - source_value.offset)
            return
        if amount is None and isinstance(source_value, Constant):
            amount = source_value.value
        if amount is None:
            registers.pop(destination, None)
            return
        if mnemonic.startswith("sub"):
            amount = -amount
        old_value = registers.get(destination)
        if isinstance(old_value, Address):
            registers[destination] = old_value.add(amount)
        elif isinstance(old_value, Constant):
            registers[destination] = Constant(old_value.value + amount)
        else:
            registers.pop(destination, None)
        return
    if mnemonic.startswith("and") and len(operands) == 2:
        destination = _register(operands[1])
        mask = _immediate(operands[0])
        value = registers.get(destination or "")
        if destination is not None and mask is not None and isinstance(value, Constant):
            registers[destination] = Constant(value.value & mask)
            return
    if mnemonic.startswith("xor") and len(operands) == 2:
        left = _register(operands[0])
        right = _register(operands[1])
        if left is not None and left == right:
            registers[right] = Constant(0)
            return
    if mnemonic.startswith("cmp") or mnemonic.startswith("test") or mnemonic.startswith("j") or mnemonic.startswith("nop"):
        return
    if operands:
        destination = _register(operands[-1])
        if destination is not None:
            registers.pop(destination, None)


def _loop_back_target(instruction: Instruction) -> int | None:
    if instruction.mnemonic not in {"jne", "jnz"}:
        return None
    match = re.match(r"(?:0x)?([0-9a-fA-F]+)\s", instruction.operands)
    return int(match.group(1), 16) if match else None


def find_byte_zero_loops(function: FunctionCode) -> list[WipeRange]:
    """识别已登记 GCC x86-64 逐字节 volatile 擦除循环。"""
    registers: dict[str, Address | Constant] = {"rcx": Address("arg0"), "rdx": Address("arg1")}
    states: list[dict[str, Address | Constant]] = []
    for instruction in function.instructions:
        states.append(dict(registers))
        _execute_register_instruction(instruction, registers)

    wipes: list[WipeRange] = []
    for index, instruction in enumerate(function.instructions):
        if instruction.mnemonic not in {"movb", "mov"} or index < 3 or index + 1 >= len(function.instructions):
            continue
        operands = _split_operands(instruction.operands)
        if len(operands) != 2 or _immediate(operands[0]) != 0:
            continue
        target_memory = _memory(operands[1])
        if target_memory is None or target_memory[2] is not None:
            continue
        backedge = _loop_back_target(function.instructions[index + 1])
        copy_ins = function.instructions[index - 3]
        add_ins = function.instructions[index - 2]
        compare_ins = function.instructions[index - 1]
        if backedge != copy_ins.address or copy_ins.mnemonic not in {"mov", "movq"}:
            continue
        copy_operands = _split_operands(copy_ins.operands)
        add_operands = _split_operands(add_ins.operands)
        compare_operands = _split_operands(compare_ins.operands)
        if len(copy_operands) != 2 or len(add_operands) != 2 or len(compare_operands) != 2:
            continue
        source_pointer = _register(copy_operands[0])
        target_pointer = _register(copy_operands[1])
        incremented_pointer = _register(add_operands[1])
        stored_pointer = target_memory[1]
        if source_pointer is None or source_pointer != incremented_pointer or target_pointer != stored_pointer:
            continue
        if _immediate(add_operands[0]) != 1 or _register(compare_operands[1]) != source_pointer:
            continue
        start = states[index - 3].get(source_pointer)
        end_register = _register(compare_operands[0])
        end = states[index - 1].get(end_register or "")
        if not isinstance(start, Address) or not isinstance(end, Address) or start.base != end.base:
            continue
        size = end.offset - start.offset
        if size <= 0:
            continue
        wipes.append(WipeRange(start, size, index - 3, index + 1, "byte-loop"))
    return wipes + find_reviewed_zero_ranges(function)


def find_reviewed_zero_ranges(function: FunctionCode) -> list[WipeRange]:
    """识别 Windows x64 的 rep stos 与逐字节展开循环。"""
    registers: dict[str, Address | Constant] = {"rcx": Address("arg0"), "rdx": Address("arg1")}
    snapshots: list[dict[str, Address | Constant]] = []
    stores: list[tuple[int, Address]] = []
    ranges: list[WipeRange] = []
    addresses = {instruction.address: index for index, instruction in enumerate(function.instructions)}
    skip_until = -1
    zero_flag: bool | None = None
    for index, instruction in enumerate(function.instructions):
        snapshots.append(dict(registers))
        if index < skip_until:
            continue
        operands = _split_operands(instruction.operands)
        if instruction.mnemonic in {"je", "jz", "jne", "jnz"} and zero_flag is not None:
            target_match = re.match(r"([0-9a-fA-F]+)\s", instruction.operands)
            destination = addresses.get(int(target_match.group(1), 16)) if target_match else None
            taken = zero_flag if instruction.mnemonic in {"je", "jz"} else not zero_flag
            if taken and destination is not None and destination > index:
                skip_until = destination
                continue
        if instruction.mnemonic.startswith("cmp") or instruction.mnemonic.startswith("test"):
            values = [Constant(_immediate(op)) if _immediate(op) is not None else registers.get(_register(op) or "") for op in operands]
            zero_flag = None
            if len(values) == 2 and all(isinstance(value, Constant) for value in values):
                zero_flag = (values[0].value & values[1].value) == 0 if instruction.mnemonic.startswith("test") else values[0] == values[1]
            elif len(values) == 2 and all(isinstance(value, Address) for value in values) and values[0].base == values[1].base:
                zero_flag = values[0] == values[1] if instruction.mnemonic.startswith("cmp") else None
        elif instruction.mnemonic.startswith("and") and len(operands) == 2:
            value, mask = registers.get(_register(operands[1]) or ""), _immediate(operands[0])
            zero_flag = (value.value & mask) == 0 if isinstance(value, Constant) and mask is not None else None
        elif not (instruction.mnemonic.startswith("mov") or instruction.mnemonic.startswith("lea") or instruction.mnemonic.startswith("j") or instruction.mnemonic.startswith("nop")):
            zero_flag = None
        if instruction.mnemonic == "rep" and instruction.operands.startswith("stos "):
            value = registers.get("rax")
            pointer = registers.get("rdi")
            count = registers.get("rcx")
            width = 1 if "%al" in instruction.operands else (4 if "%eax" in instruction.operands else 8)
            if isinstance(value, Constant) and value.value == 0 and isinstance(pointer, Address) and isinstance(count, Constant) and count.value > 0:
                size = count.value * width
                ranges.append(WipeRange(pointer, size, index, index, "rep-stos"))
                registers["rdi"] = pointer.add(size)
                registers["rcx"] = Constant(0)
            continue
        if instruction.mnemonic == "movb" and len(operands) == 2 and _immediate(operands[0]) == 0:
            memory = _memory(operands[1])
            if memory is not None:
                displacement, base, index_register, scale = memory
                indexed = registers.get(index_register or "")
                if index_register is not None and not isinstance(indexed, Constant):
                    continue
                if isinstance(indexed, Constant):
                    displacement += indexed.value * scale
                pointer = Address(base, displacement) if base in {"rsp", "rbp"} else registers.get(base)
                if isinstance(pointer, Address):
                    pointer = pointer if base in {"rsp", "rbp"} else pointer.add(displacement)
                    stores.append((index, pointer))
                    ranges.append(WipeRange(pointer, 1, index, index, "byte-store"))
        target = _loop_back_target(instruction)
        first = addresses.get(target or -1)
        if first is not None and first < index and index > 0:
            comparison = _split_operands(function.instructions[index - 1].operands)
            if len(comparison) == 2:
                left = _register(comparison[0])
                right = _register(comparison[1])
                left_value = registers.get(left or "")
                right_value = registers.get(right or "")
                immediate = _immediate(comparison[0])
                if immediate is not None and right is not None:
                    start_counter = snapshots[first].get(right)
                    finish_counter = registers.get(right)
                    loop_stores = [(position, pointer) for position, pointer in stores if first <= position < index]
                    if isinstance(start_counter, Constant) and isinstance(finish_counter, Constant) and loop_stores:
                        step = finish_counter.value - start_counter.value
                        origin = loop_stores[0][1]
                        offsets = {pointer.offset - origin.offset for _, pointer in loop_stores if pointer.base == origin.base}
                        span = immediate - start_counter.value
                        if step > 0 and span > 0 and span % step == 0 and offsets == set(range(step)):
                            ranges.append(WipeRange(origin, span, first, index, "indexed-byte-loop"))
                    registers[right] = Constant(immediate)
                elif isinstance(left_value, Address) and isinstance(right_value, Address):
                    for pointer_register, end in ((left, right_value), (right, left_value)):
                        start = snapshots[first].get(pointer_register or "")
                        finish = registers.get(pointer_register or "")
                        if not isinstance(start, Address) or not isinstance(finish, Address) or start.base != end.base:
                            continue
                        stride = finish.offset - start.offset
                        span = end.offset - start.offset
                        offsets = {pointer.offset - start.offset for position, pointer in stores if first <= position < index and pointer.base == start.base}
                        if stride > 0 and span > 0 and span % stride == 0 and offsets == set(range(stride)):
                            ranges.append(WipeRange(start, span, first, index, "byte-loop"))
                            registers[pointer_register] = end
        _execute_register_instruction(instruction, registers)
    merged: list[WipeRange] = []
    for item in ranges:
        if merged and merged[-1].start.base == item.start.base and merged[-1].start.offset + merged[-1].size == item.start.offset:
            previous = merged[-1]
            merged[-1] = WipeRange(previous.start, previous.size + item.size, previous.first_instruction, item.last_instruction, "contiguous-zero-writes")
        else:
            merged.append(item)
    return ranges + merged


def _function(functions: Iterable[FunctionCode], name: str) -> FunctionCode | None:
    return next((candidate for candidate in functions if candidate.name == name), None)


def _memory_accesses(function: FunctionCode) -> list[tuple[int, Address, bool]]:
    registers: dict[str, Address | Constant] = {"rcx": Address("arg0"), "rdx": Address("arg1")}
    accesses: list[tuple[int, Address, bool]] = []
    for index, instruction in enumerate(function.instructions):
        operands = _split_operands(instruction.operands)
        if instruction.mnemonic.startswith("mov") and len(operands) == 2:
            for position, operand in enumerate(operands):
                memory = _memory(operand)
                if memory is None or memory[2] is not None:
                    continue
                displacement, base, _, _ = memory
                pointer = Address(base, displacement) if base in {"rsp", "rbp"} else registers.get(base)
                if isinstance(pointer, Address):
                    address = pointer if base in {"rsp", "rbp"} else pointer.add(displacement)
                    accesses.append((index, address, position == 1))
        _execute_register_instruction(instruction, registers)
    return accesses


def attach_local_materials(functions: list[FunctionCode], text: str) -> None:
    """从已注册 GCC DWARF 的固定 CFA 栈位置绑定具名敏感数组。"""
    entry = re.compile(r"^\s*<(\d+)><[0-9a-f]+>: Abbrev Number: \d+(?: \((DW_TAG_\w+)\))?")
    attribute = re.compile(r"DW_AT_(\w+)\s*:\s*(.*)")
    nodes: list[tuple[int, str, dict[str, str]]] = []
    for line in text.splitlines():
        match = entry.match(line)
        if match:
            nodes.append((int(match[1]), match[2] or "", {}))
        elif nodes and (match := attribute.search(line)):
            nodes[-1][2][match[1]] = match[2]
    by_start = {function.start: function for function in functions}
    current: FunctionCode | None = None
    parent_depth = -1
    cfa_offset = 0
    for depth, tag, attributes in nodes:
        if current is not None and depth <= parent_depth:
            current = None
        if tag == "DW_TAG_subprogram" and "low_pc" in attributes:
            match = re.fullmatch(r"0x([0-9a-f]+)", attributes["low_pc"])
            current = by_start.get(int(match[1], 16)) if match else None
            if current is None or "DW_OP_call_frame_cfa" not in attributes.get("frame_base", ""):
                current = None
                continue
            current.local_materials = {}
            parent_depth = depth
            cfa_offset = 8  # Windows x64 返回地址槽。
            for instruction in current.instructions:
                if instruction.mnemonic == "push":
                    cfa_offset += 8
                elif instruction.mnemonic == "sub" and instruction.operands.endswith(",%rsp"):
                    amount = _immediate(_split_operands(instruction.operands)[0])
                    if amount is not None:
                        cfa_offset += amount
                    break
                elif instruction.mnemonic not in {"endbr64", "nop", "nopl"}:
                    break
        elif current is not None and depth == parent_depth + 1 and tag == "DW_TAG_variable":
            name = attributes.get("name", "").rsplit(": ", 1)[-1]
            if name not in {"state", "working", "seed"}:
                continue
            match = re.search(r"\(DW_OP_fbreg: (-?\d+)\)", attributes.get("location", ""))
            if match:
                current.local_materials[name] = Address("rsp", cfa_offset + int(match[1]))


def _branch_target(instruction: Instruction) -> int | None:
    match = re.match(r"\s*(?:0x)?([0-9a-fA-F]+)(?:\s|$)", instruction.operands)
    return int(match.group(1), 16) if match else None


def attach_exception_tables(functions: list[FunctionCode], text: str) -> None:
    """读取最终 PE 的 GCC LSDA，关联受保护调用区间及异常落地点。"""
    dwarf_omit = 0xFF
    dwarf_uleb128 = 0x01
    lookup = {function.start: function for function in functions}
    header = re.compile(r"^\s*[0-9a-f]+ \(rva: [0-9a-f]+\): ([0-9a-f]+) - ([0-9a-f]+)", re.I)
    current: FunctionCode | None = None
    data = bytearray()
    has_handler = False

    def finish() -> None:
        if current is None:
            return
        if not has_handler:
            current.exception_sites = []
            return
        if not data or data[0] != dwarf_omit:
            return
        offset = 1

        def uleb() -> int:
            nonlocal offset
            value = 0
            shift = 0
            while True:
                byte = data[offset]
                offset += 1
                value |= (byte & 0x7F) << shift
                if not byte & 0x80:
                    return value
                shift += 7

        try:
            type_encoding = data[offset]
            offset += 1
            if type_encoding != dwarf_omit:
                uleb()
            encoding = data[offset]
            offset += 1
            if encoding != dwarf_uleb128:
                return
            length = uleb()
            end = offset + length
            sites = []
            while offset < end:
                begin, size, landing = uleb(), uleb(), uleb()
                uleb()
                sites.append((current.start + begin, current.start + begin + size,
                              current.start + landing if landing else 0))
            if offset == end:
                current.exception_sites = sites
        except IndexError:
            return

    for line in text.splitlines():
        match = header.match(line)
        if match:
            finish()
            current = lookup.get(int(match.group(1), 16))
            data = bytearray()
            has_handler = False
        elif current is not None:
            has_handler |= "UNW_FLAG_EHANDLER" in line
            match = re.match(r"^\s+[0-9a-f]{3}:\s+((?:[0-9a-f]{2}(?:\s+|$))+)", line, re.I)
            if match:
                data.extend(bytes.fromhex(match.group(1)))
    finish()


def _control_edges(
    function: FunctionCode,
    index: int,
    include_exceptions: bool = False,
) -> tuple[list[tuple[int, str]], str | None, str | None]:
    instruction = function.instructions[index]
    mnemonic = instruction.mnemonic
    addresses = {item.address: position for position, item in enumerate(function.instructions)}

    if mnemonic.startswith("ret"):
        return [], "normal", None
    if mnemonic in {"ud2", "int3", "hlt"}:
        return [], "abnormal", None

    symbol_match = re.search(r"<([^>]+)>", instruction.operands)
    symbol = symbol_match.group(1).lower() if symbol_match else ""
    if mnemonic.startswith("call"):
        exception_edges = []
        if include_exceptions and function.exception_sites is not None:
            for begin, end, landing in function.exception_sites:
                if begin <= instruction.address < end and landing:
                    if landing not in addresses:
                        return [], None, "异常落地点不在可解析的函数范围内"
                    exception_edges.append((addresses[landing], "unwind"))
        if any(name in symbol for name in ("_unwind_resume", "__cxa_throw", "__cxa_rethrow", "__cxxthrowexception", "throwaudit")):
            if exception_edges:
                return exception_edges, None, None
            return [], "exception", None
        if any(name in symbol for name in ("std::terminate", "abort", "exit", "_exit", "exitprocess")):
            return [], "abnormal", None
        if index + 1 >= len(function.instructions):
            return [], None, "函数末尾的调用缺少可解析返回边"
        return [(index + 1, "fallthrough"), *exception_edges], None, None

    if mnemonic in {"jmp", "jmpq", "ljmp"}:
        target = _branch_target(instruction)
        if target is None:
            return [], None, f"无法解析无条件跳转目标：{instruction.operands}"
        destination = addresses.get(target)
        if destination is None:
            return [], None, f"无条件跳转目标不在函数指令范围内：{target:#x}"
        return [(destination, "jump")], None, None

    conditional = mnemonic.startswith("j") or mnemonic.startswith("loop")
    if conditional:
        target = _branch_target(instruction)
        if target is None:
            return [], None, f"无法解析条件分支目标：{instruction.operands}"
        destination = addresses.get(target)
        if destination is None:
            return [], None, f"条件分支目标不在函数指令范围内：{target:#x}"
        if index + 1 >= len(function.instructions):
            return [], None, "条件分支缺少顺序后继指令"
        return [(destination, "taken"), (index + 1, "fallthrough")], None, None

    if index + 1 >= len(function.instructions):
        return [], None, "函数末尾没有可解析的退出或后继指令"
    return [(index + 1, "fallthrough")], None, None


def _range_mask(address: Address, size: int, wipe: WipeRange) -> int:
    start = max(address.offset, wipe.start.offset)
    end = min(address.offset + size, wipe.start.offset + wipe.size)
    if end <= start:
        return 0
    return ((1 << (end - start)) - 1) << (start - address.offset)


def _zero_flag(instruction: Instruction, registers: dict[str, Address | Constant], previous: bool | None) -> bool | None:
    operands = _split_operands(instruction.operands)
    if instruction.mnemonic.startswith(("cmp", "test")) and len(operands) == 2:
        values = [Constant(_immediate(op)) if _immediate(op) is not None else registers.get(_register(op) or "") for op in operands]
        if all(isinstance(value, Constant) for value in values):
            return (values[0].value & values[1].value) == 0 if instruction.mnemonic.startswith("test") else values[0] == values[1]
        if instruction.mnemonic.startswith("cmp") and all(isinstance(value, Address) for value in values) and values[0].base == values[1].base:
            return values[0] == values[1]
        return None
    if instruction.mnemonic.startswith("and") and len(operands) == 2:
        value, mask = registers.get(_register(operands[1]) or ""), _immediate(operands[0])
        return (value.value & mask) == 0 if isinstance(value, Constant) and mask is not None else None
    if instruction.mnemonic.startswith("xor") and len(operands) == 2 and operands[0] == operands[1]:
        return True
    if instruction.mnemonic.startswith(("mov", "lea", "j", "nop")):
        return previous
    return None


def _actual_zero_range(instruction: Instruction, registers: dict[str, Address | Constant], index: int) -> WipeRange | None:
    if instruction.mnemonic == "rep" and instruction.operands.startswith("stos "):
        value, pointer, count = registers.get("rax"), registers.get("rdi"), registers.get("rcx")
        width = 1 if "%al" in instruction.operands else (4 if "%eax" in instruction.operands else 8)
        if value == Constant(0) and isinstance(pointer, Address) and isinstance(count, Constant) and count.value > 0:
            return WipeRange(pointer, count.value * width, index, index, "rep-stos")
    operands = _split_operands(instruction.operands)
    if instruction.mnemonic != "movb" or len(operands) != 2 or _immediate(operands[0]) != 0:
        return None
    memory = _memory(operands[1])
    if memory is None:
        return None
    displacement, base, indexed, scale = memory
    if indexed:
        value = registers.get(indexed)
        if not isinstance(value, Constant):
            return None
        displacement += value.value * scale
    pointer = Address(base, displacement) if base in {"rsp", "rbp"} else registers.get(base)
    if isinstance(pointer, Address):
        return WipeRange(pointer if base in {"rsp", "rbp"} else pointer.add(displacement), 1, index, index, "byte-store")
    return None


def _loop_entry_matches(function: FunctionCode, wipe: WipeRange, registers: dict[str, Address | Constant]) -> bool:
    initial = dict(registers)
    actual: WipeRange | None = None
    for index in range(wipe.first_instruction, wipe.last_instruction):
        instruction = function.instructions[index]
        actual = _actual_zero_range(instruction, initial, index)
        if actual is not None:
            break
        _execute_register_instruction(instruction, initial)
    if actual is None or actual.start != wipe.start:
        return False
    final = dict(registers)
    for instruction in function.instructions[wipe.first_instruction:wipe.last_instruction]:
        _execute_register_instruction(instruction, final)
    comparison = _split_operands(function.instructions[wipe.last_instruction - 1].operands)
    if len(comparison) != 2:
        return False
    for position in (0, 1):
        counter = _register(comparison[position])
        start = registers.get(counter or "")
        other = comparison[1 - position]
        limit = Constant(_immediate(other)) if _immediate(other) is not None else final.get(_register(other) or "")
        modified = any(ins.mnemonic.startswith(("add", "inc")) and _register(_split_operands(ins.operands)[-1]) == counter for ins in function.instructions[wipe.first_instruction:wipe.last_instruction])
        if not modified:
            continue
        if isinstance(start, Address) and isinstance(limit, Address) and start.base == limit.base and limit.offset - start.offset == wipe.size:
            return True
        if isinstance(start, Constant) and isinstance(limit, Constant) and limit.value - start.value == wipe.size:
            return True
    return False


def _path_coverage(
    function: FunctionCode,
    address: Address,
    size: int,
    *,
    start: int = 0,
    after: int = -1,
    before: int | None = None,
    triggers: frozenset[int] = frozenset(),
    require_trigger: bool = False,
    exit_kind: str = "normal",
) -> tuple[Status, str]:
    if size <= 0 or not function.instructions:
        return Status.UNDETERMINED, "目标范围或指令流为空"
    if exit_kind == "exception" and function.exception_sites is None:
        return Status.UNDETERMINED, "缺少最终链接 PE 的异常调用点与落地点表"

    ranges = [wipe for wipe in find_byte_zero_loops(function)
        if wipe.form in {"byte-loop", "indexed-byte-loop"} and wipe.size > 0]
    loop_specs: list[tuple[int, int, int, int]] = []
    loop_ranges: dict[int, WipeRange] = {}
    loop_members: set[int] = set()
    unresolved_ranges: list[str] = []
    for wipe in ranges:
        mask = _range_mask(address, size, wipe) if wipe.start.base == address.base and wipe.first_instruction > after and (before is None or wipe.last_instruction < before) else 0
        if any(first == wipe.first_instruction and last == wipe.last_instruction for first, last, _, _ in loop_specs):
            continue
        if wipe.form in {"byte-loop", "indexed-byte-loop"}:
            if not (0 <= wipe.first_instruction < len(function.instructions)
                    and 0 <= wipe.last_instruction < len(function.instructions)):
                unresolved_ranges.append("擦除循环指令范围越界")
                continue
            close = function.instructions[wipe.last_instruction]
            if any(item.mnemonic.startswith(("j", "call", "ret"))
                   for item in function.instructions[wipe.first_instruction:wipe.last_instruction]):
                unresolved_ranges.append("擦除循环包含尚未证明的内部控制流")
                continue
            target = _branch_target(close)
            if (not (close.mnemonic.startswith("j") or close.mnemonic.startswith("loop"))
                    or target != function.instructions[wipe.first_instruction].address):
                unresolved_ranges.append("擦除循环缺少可核对的回边")
                continue
            loop_id = len(loop_specs)
            loop_ranges[loop_id] = wipe
            loop_specs.append((wipe.first_instruction, wipe.last_instruction, target, mask))
            loop_members.update(range(wipe.first_instruction, wipe.last_instruction + 1))

    loop_starts: dict[int, list[int]] = {}
    loop_closes: dict[int, list[int]] = {}
    for loop_id, (first, last, _, _) in enumerate(loop_specs):
        loop_starts.setdefault(first, []).append(loop_id)
        loop_closes.setdefault(last, []).append(loop_id)

    full_mask = (1 << size) - 1
    initial_active = not require_trigger
    pending = [(start, 0, initial_active, 0, {"rcx": Address("arg0"), "rdx": Address("arg1")}, None)]
    visited: dict[tuple[int, int, bool, int], tuple[dict[str, Address | Constant], bool | None]] = {}
    errors = unresolved_ranges.copy()
    active_exits = 0
    boundaries = 0
    uncovered = False

    while pending:
        index, covered, active, active_loops, registers, zero_flag = pending.pop()
        state = (index, covered, active, active_loops)
        previous = visited.get(state)
        if previous is not None:
            registers = {name: value for name, value in registers.items() if previous[0].get(name) == value}
            zero_flag = zero_flag if previous[1] == zero_flag else None
            if previous == (registers, zero_flag):
                continue
        visited[state] = (dict(registers), zero_flag)
        if before is not None and index == before:
            boundaries += 1
            if active and covered != full_mask:
                uncovered = True
            continue

        if index in triggers:
            active = True
            covered = 0
        for loop_id in loop_starts.get(index, ()):
            if _loop_entry_matches(function, loop_ranges[loop_id], registers):
                active_loops |= 1 << loop_id
            else:
                errors.append("擦除循环入口的实际目标或结束地址尚未得到证明")
        instruction = function.instructions[index]
        actual = _actual_zero_range(instruction, registers, index)
        if active and actual is not None and index > after and index not in loop_members and actual.start.base == address.base:
            covered |= _range_mask(address, size, actual)
        zero_flag = _zero_flag(instruction, registers, zero_flag)
        _execute_register_instruction(instruction, registers)
        if actual is not None and actual.form == "rep-stos":
            registers["rdi"] = actual.start.add(actual.size)
            registers["rcx"] = Constant(0)

        edges, terminal, error = _control_edges(function, index, exit_kind == "exception")
        if instruction.mnemonic in {"je", "jz", "jne", "jnz"} and zero_flag is not None and index not in loop_closes:
            taken = zero_flag if instruction.mnemonic in {"je", "jz"} else not zero_flag
            edges = [(target, kind) for target, kind in edges if kind == ("taken" if taken else "fallthrough")]
        if index in loop_closes:
            edges = [(target, kind) for target, kind in edges if kind == "fallthrough"]
        if error:
            errors.append(error)
            continue
        if terminal:
            if before is None and terminal == exit_kind and active:
                active_exits += 1
                if covered != full_mask:
                    uncovered = True
            continue

        for destination, edge_kind in edges:
            edge_coverage = covered
            edge_loops = active_loops
            for loop_id in loop_closes.get(index, ()):
                first, _, target, mask = loop_specs[loop_id]
                if destination != target and edge_loops & (1 << loop_id):
                    if active:
                        edge_coverage |= mask
                    edge_loops &= ~(1 << loop_id)
            next_registers = dict(registers)
            equal_edge = (instruction.mnemonic in {"je", "jz"} and edge_kind == "taken") or (instruction.mnemonic in {"jne", "jnz"} and edge_kind == "fallthrough")
            if equal_edge and index not in loop_closes and index > 0 and function.instructions[index - 1].mnemonic.startswith("cmp"):
                comparison = _split_operands(function.instructions[index - 1].operands)
                if len(comparison) == 2:
                    for position in (0, 1):
                        operand = comparison[position]
                        if not re.fullmatch(r"%r(?:[abcd]x|[sd]i|[bs]p|\d+)", operand):
                            continue
                        counter = _register(operand)
                        other = comparison[1 - position]
                        value = Constant(_immediate(other)) if _immediate(other) is not None else registers.get(_register(other) or "")
                        if counter and isinstance(value, (Address, Constant)):
                            next_registers[counter] = value
            for loop_id in loop_closes.get(index, ()):
                first, _, _, _ = loop_specs[loop_id]
                comparison = _split_operands(function.instructions[index - 1].operands)
                if len(comparison) == 2:
                    for counter_position in (0, 1):
                        counter = _register(comparison[counter_position])
                        other = comparison[1 - counter_position]
                        limit = Constant(_immediate(other)) if _immediate(other) is not None else registers.get(_register(other) or "")
                        modifies_counter = any(item.mnemonic.startswith(("add", "inc")) and _register(_split_operands(item.operands)[-1]) == counter for item in function.instructions[first:index])
                        if counter and modifies_counter and isinstance(limit, (Address, Constant)):
                            next_registers[counter] = limit
            pending.append((destination, edge_coverage, active, edge_loops, next_registers, zero_flag))

    if errors:
        return Status.UNDETERMINED, "; ".join(dict.fromkeys(errors))
    if uncovered:
        return Status.FAIL, "存在到达退出或目标写入点的路径未在同一路径上擦除完整范围"
    if before is not None:
        if boundaries:
            return Status.PASS, "所有到达目标写入点的路径此前均完成擦除"
        return Status.UNDETERMINED, "没有找到可达的目标写入点"
    if active_exits:
        return Status.PASS, f"每条 {exit_kind} 退出路径均完成目标范围擦除"
    return Status.FAIL, f"没有找到完成擦除的 {exit_kind} 退出路径"


def _covers(function: FunctionCode, address: Address, size: int, after: int = -1, before: int | None = None) -> bool:
    return _path_coverage(function, address, size, after=after, before=before)[0] is Status.PASS


def _lifecycle_checks(functions: list[FunctionCode]) -> dict[str, dict[str, object]]:
    checks: dict[str, dict[str, object]] = {}

    def record(name: str, result: tuple[Status, str]) -> None:
        status, detail = result
        checks[name] = {"passed": status is Status.PASS, "status": status.value, "detail": detail}

    for operation, argument in (("move_construct", "arg1"), ("move_assign", "arg1"), ("destructor", "arg0")):
        function = _function(functions, f"randx_audit_probe_{operation}")
        for member, offset, size in (("state", 0, CHACHA_STATE_BYTES), ("cache", CHACHA_STATE_BYTES, CHACHA_BLOCK_BYTES)):
            key = f"{operation}_source_{member}" if operation != "destructor" else f"destructor_{member}"
            if function is None:
                record(key, (Status.UNDETERMINED, "缺少生命周期探针"))
                continue
            accesses = _memory_accesses(function)
            reads = frozenset(index for index, address, write in accesses if not write and address.base == argument
                and 0 <= address.offset < CHACHA_STATE_BYTES + CHACHA_BLOCK_BYTES)
            record(key, _path_coverage(function, Address(argument, offset), size,
                after=max(reads, default=-1), triggers=reads, require_trigger=operation != "destructor"))
            if operation == "move_assign":
                wiping = {i for wipe in find_reviewed_zero_ranges(function)
                          for i in range(wipe.first_instruction, wipe.last_instruction + 1)}
                writes = [index for index, address, write in accesses if write and address.base == "arg0"
                    and 0 <= address.offset < CHACHA_STATE_BYTES + CHACHA_BLOCK_BYTES
                    and index not in wiping and function.instructions[index].mnemonic != "movb"
                    and _immediate(_split_operands(function.instructions[index].operands)[0]) != 0]
                record(f"move_assign_destination_{member}", _path_coverage(function, Address("arg0", offset), size,
                    before=min(writes, default=0)))

    generate = _function(functions, "RandX::ChaCha20::generateBlock()")
    for member in ("state", "working"):
        if generate is None:
            record(f"generate_{member}", (Status.UNDETERMINED, "缺少 generateBlock 机器码范围"))
            continue
        accesses = _memory_accesses(generate)
        if generate.local_materials is None:
            record(f"generate_{member}", (Status.UNDETERMINED, "缺少最终链接产物的 DWARF 数组存储位置"))
            continue
        address = generate.local_materials.get(member)
        if address is None:
            record(f"generate_{member}", (Status.FAIL, "具名敏感数组缺少可核对的固定栈存储与完整擦除"))
            continue
        reads = frozenset(index for index, target, write in accesses if not write and target.base == address.base
            and address.offset <= target.offset < address.offset + CHACHA_BLOCK_BYTES)
        outputs = frozenset(index for index, target, write in accesses if write and target.base == "arg0"
            and CHACHA_STATE_BYTES <= target.offset < CHACHA_STATE_BYTES + CHACHA_BLOCK_BYTES)
        record(f"generate_{member}", _path_coverage(generate, address, CHACHA_BLOCK_BYTES,
            after=max(reads | outputs, default=-1), triggers=outputs, require_trigger=True))
        checks[f"generate_{member}"]["address"] = dataclasses.asdict(address)

    reseed = _function(functions, "RandX::ChaCha20::reseed()")
    if reseed is None:
        for key in ("reseed_seed", "reseed_seed_exception", "reseed_old_cache"):
            record(key, (Status.UNDETERMINED, "缺少 reseed 机器码范围"))
    else:
        accesses = _memory_accesses(reseed)
        commits = frozenset(index for index, address, write in accesses if write and address.base == "arg0"
            and 0 <= address.offset < CHACHA_STATE_BYTES)
        seed_addresses = {wipe.start for wipe in find_reviewed_zero_ranges(reseed)
            if wipe.start.base in {"rsp", "rbp"} and wipe.size == CHACHA_SEED_BYTES}
        if len(seed_addresses) == 1:
            address = next(iter(seed_addresses))
            record("reseed_seed", _path_coverage(reseed, address, CHACHA_SEED_BYTES,
                after=max(commits, default=-1), triggers=commits, require_trigger=True))
            record("reseed_seed_exception", _path_coverage(reseed, address, CHACHA_SEED_BYTES, exit_kind="exception"))
        else:
            for key in ("reseed_seed", "reseed_seed_exception"):
                record(key, (Status.FAIL, "未识别到唯一完整种子擦除范围"))
        record("reseed_old_cache", _path_coverage(reseed, Address("arg0", CHACHA_STATE_BYTES), CHACHA_BLOCK_BYTES,
            after=max(commits, default=-1), triggers=commits, require_trigger=True))
    return checks


def _initialization_checks(functions: list[FunctionCode]) -> dict[str, dict[str, object]]:
    lookup = {function.name: function for function in functions}
    checks: dict[str, dict[str, object]] = {}
    for kind in ("invalid", "degenerate", "valid"):
        pending = [f"randx_audit_triangular_{kind}"]
        visited: set[str] = set()
        initializes = False
        exists = pending[0] in lookup
        while pending:
            name = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            function = lookup.get(name)
            if function is None:
                continue
            for instruction in function.instructions:
                initializes |= any(marker in instruction.operands for marker in
                    ("CreateDefaultEngine", "DefaultEngine()", "__emutls_get_address"))
                if instruction.mnemonic in {"call", "jmp"}:
                    match = re.search(r"<(.+)>", instruction.operands)
                    if match:
                        pending.append(re.sub(r"\+0x[0-9a-f]+$", "", match.group(1)))
        checks[f"triangular_first_{kind}"] = {"passed": exists and initializes == (kind == "valid"),
            "detail": "生产配置首次调用的可达机器码与有效调用初始化正对照",
            "initializes_default_engine": initializes}
    return checks


def _post_consume_check(function: FunctionCode, expected_size: int, exit_kind: str = "normal") -> tuple[Status, str]:
    if not function.instructions:
        return Status.UNDETERMINED, "探针函数没有可解析的最终机器码"
    registers: dict[str, Address | Constant] = {"rcx": Address("arg0"), "rdx": Address("arg1")}
    consumers: list[tuple[int, Address, int]] = []
    for index, instruction in enumerate(function.instructions):
        if _loop_back_target(instruction) is not None and index > 0:
            comparison = _split_operands(function.instructions[index - 1].operands)
            if len(comparison) == 2:
                limit, counter = _immediate(comparison[0]), _register(comparison[1])
                if limit is not None and counter is not None:
                    registers[counter] = Constant(limit)
        if instruction.mnemonic == "call" and "consumeBytes" in instruction.operands:
            pointer = registers.get("rcx")
            length = registers.get("rdx")
            if isinstance(pointer, Address) and isinstance(length, Constant):
                consumers.append((index, pointer, length.value))
        _execute_register_instruction(instruction, registers)
    for consume_index, pointer, length in consumers:
        if length == expected_size:
            return _path_coverage(function, pointer, length, after=consume_index,
                triggers=frozenset({consume_index}), require_trigger=True, exit_kind=exit_kind)
    return Status.FAIL, "缺少匹配大小的材料消费调用"


def _has_exact_post_consume_wipe(function: FunctionCode, expected_size: int) -> tuple[bool, str]:
    status, detail = _post_consume_check(function, expected_size)
    return status is Status.PASS, detail


def audit_disassembly(text: str, unwind_text: str = "", triangular: bool = True, debug_text: str = "") -> CaseResult:
    functions = parse_objdump(text)
    if not functions or not any(function.instructions for function in functions):
        return CaseResult(Status.UNDETERMINED, ["反汇编格式或目标架构尚无已验证规则"], {})

    attach_exception_tables(functions, unwind_text)
    attach_local_materials(functions, debug_text)
    checks = {
        "direct": ("randx_audit_probe_direct", DIRECT_MATERIAL_BYTES),
        "scoped": ("randx_audit_probe_scoped", SCOPED_MATERIAL_BYTES),
        "exception": ("randx_audit_probe_exception", EXCEPTION_MATERIAL_BYTES),
    }
    reasons: list[str] = []
    evidence: dict[str, object] = {"checks": {}, "audit_completed": True}
    statuses: list[Status] = []
    for check_name, (function_name, expected_size) in checks.items():
        function = _function(functions, function_name)
        if function is None:
            statuses.append(Status.UNDETERMINED)
            reasons.append(f"缺少探针机器码范围 {function_name}")
            continue
        status, detail = _post_consume_check(function, expected_size, "exception" if check_name == "exception" else "normal")
        evidence["checks"][check_name] = {"passed": status is Status.PASS, "status": status.value, "detail": detail}
        statuses.append(status)
        if status is not Status.PASS:
            reasons.append(f"{check_name}: {detail}")

    initializer = _function(functions, "randx_audit_probe_initializer_only")
    if initializer is None:
        statuses.append(Status.UNDETERMINED)
        reasons.append("缺少初始化负对照探针")
    else:
        registers = {"rcx": Address("arg0"), "rdx": Address("arg1")}
        consumers: list[tuple[int, Address, int]] = []
        for index, instruction in enumerate(initializer.instructions):
            if instruction.mnemonic == "call" and "consumeBytes" in instruction.operands:
                pointer = registers.get("rcx")
                length = registers.get("rdx")
                if isinstance(pointer, Address) and isinstance(length, Constant):
                    consumers.append((index, pointer, length.value))
            _execute_register_instruction(instruction, registers)
        ranges = find_byte_zero_loops(initializer)
        trailing = [
            wipe for wipe in ranges
            if any(wipe.first_instruction > index and wipe.start == pointer and wipe.size == size
                   for index, pointer, size in consumers)
        ]
        evidence["checks"]["initializer_only"] = {
            "passed": not trailing,
            "detail": "初始化本身未被计为消费后的擦除" if not trailing else "初始化零写被误判为消费后擦除",
        }
        statuses.append(Status.PASS if not trailing else Status.FAIL)
        if trailing:
            reasons.append("初始化负对照被错误判定为擦除")

    lifecycle = {**_lifecycle_checks(functions), **(_initialization_checks(functions) if triangular else {})}
    evidence["checks"].update(lifecycle)
    for name, check in lifecycle.items():
        statuses.append(Status(check.get("status", "pass" if check["passed"] else "fail")))
        if not check["passed"]:
            reasons.append(f"{name}: {check['detail']}")
    if Status.FAIL in statuses:
        status = Status.FAIL
    elif Status.UNDETERMINED in statuses:
        status = Status.UNDETERMINED
    else:
        status = Status.PASS
    return CaseResult(status, reasons, evidence)


def _replace_once(text: str, before: str, after: str, site: str) -> str:
    count = text.count(before)
    if count != 1:
        raise ValueError(f"变异点 {site} 期望唯一源码匹配，实际为 {count}")
    return text.replace(before, after, 1)


def _replace_function_statement(text: str, signature: str, before: str, after: str, site: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise ValueError(f"无法定位变异点函数：{site}")
    opening = text.find("{", start)
    if opening < 0:
        raise ValueError(f"函数体缺失：{site}")
    depth = 0
    closing = -1
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                closing = index
                break
    if closing < 0:
        raise ValueError(f"函数体未闭合：{site}")
    body = text[opening:closing + 1]
    replacement = _replace_once(body, before, after, site)
    return text[:opening] + replacement + text[closing + 1:]


def mutate_header(text: str, mutation: str) -> str:
    if mutation == "global":
        signature = "inline void SecureWipe(void* ptr, std::size_t len) noexcept"
        return _replace_function_statement(text, signature, "SecureWipeWithSelectedBackend(ptr, len);", "(void)ptr; (void)len;", mutation)

    if mutation in {"direct", "scoped", "exception"}:
        return text

    guard_sites = {
        "generate_state": ("inline void ChaCha20::generateBlock()", "detail::ScopedWiper stateWiper(state.data(), sizeof(state));"),
        "generate_working": ("inline void ChaCha20::generateBlock()", "detail::ScopedWiper workingWiper(working.data(), sizeof(working));"),
        "reseed_seed": ("inline void ChaCha20::reseed()", "detail::ScopedWiper wiper(seed.data(), seed.size());"),
    }
    if mutation in guard_sites:
        signature, statement = guard_sites[mutation]
        return _replace_function_statement(text, signature, statement, "", mutation)

    function_signature = ""
    statement = ""
    if mutation.startswith("move_construct_source_"):
        function_signature = "inline ChaCha20::ChaCha20(ChaCha20&& other) noexcept"
        member = "m_state" if mutation.endswith("state") else "m_buffer"
        statement = f"detail::SecureWipe(other.{member}.data(), sizeof(other.{member}));"
    elif mutation.startswith("move_assign_"):
        function_signature = "inline ChaCha20& ChaCha20::operator=(ChaCha20&& other) noexcept"
        target = "other" if mutation.startswith("move_assign_source_") else "this"
        member = "m_state" if mutation.endswith("state") else "m_buffer"
        expression = f"{member}.data()" if target == "this" else f"other.{member}.data()"
        extent = f"sizeof({member})" if target == "this" else f"sizeof(other.{member})"
        statement = f"detail::SecureWipe({expression}, {extent});"
    elif mutation.startswith("destructor_"):
        function_signature = "inline ChaCha20::~ChaCha20() noexcept"
        member = "m_state" if mutation.endswith("state") else "m_buffer"
        statement = f"detail::SecureWipe({member}.data(), sizeof({member}));"
    elif mutation == "reseed_old_cache":
        function_signature = "inline void ChaCha20::reseed()"
        statement = "detail::SecureWipe(m_buffer.data(), m_buffer.size());"
    else:
        raise ValueError(f"未知变异点：{mutation}")
    return _replace_function_statement(text, function_signature, statement, "", mutation)


def _compiler_kind(compiler: pathlib.Path) -> str:
    name = compiler.name.lower()
    if name in {"cl", "cl.exe"}:
        return "msvc"
    if "clang" in name:
        return "clang"
    return "gcc"


def _compiler_info(compiler: pathlib.Path, cwd: pathlib.Path) -> dict[str, object]:
    version_code, version_out, version_err = _capture([str(compiler), "--version"], cwd)
    version = (version_out or version_err).splitlines()[0] if (version_out or version_err) else ""
    info: dict[str, object] = {"path": str(compiler), "version": version, "kind": _compiler_kind(compiler)}
    if info["kind"] != "msvc":
        target_code, target_out, target_err = _capture([str(compiler), "-dumpmachine"], cwd)
        info["target"] = target_out.strip() if target_code == 0 else target_err.strip()
    if version_code != 0:
        info["error"] = version_err or version_out
    return info


def _find_tool(explicit: str | None, names: Sequence[str]) -> pathlib.Path | None:
    if explicit:
        candidate = pathlib.Path(explicit)
        if candidate.is_file():
            return candidate.resolve()
        discovered = shutil.which(explicit)
        return pathlib.Path(discovered).resolve() if discovered else None
    for name in names:
        discovered = shutil.which(name)
        if discovered:
            return pathlib.Path(discovered).resolve()
    return None


def _case_label(standard: str, optimization: str, backend: str) -> str:
    return f"{standard}-{optimization}-{backend}"


def _compiler_command(
    kind: str,
    compiler: pathlib.Path,
    source: pathlib.Path,
    include_dir: pathlib.Path,
    executable: pathlib.Path,
    standard: str,
    optimization: str,
    backend: str,
    mutation: str | None,
) -> list[str]:
    cpp_standard = "c++17" if standard == "cpp17" else "c++23"
    definitions: list[str] = []
    if standard == "cpp17":
        definitions.append("RANDX_AUDIT_CPP17=1")
    if backend == "portable":
        definitions.append("RANDX_USE_PORTABLE_SECURE_WIPE=1")
    if mutation == "direct":
        definitions.append("RANDX_AUDIT_MUTATE_DIRECT=1")
    if mutation == "scoped":
        definitions.append("RANDX_AUDIT_MUTATE_SCOPED=1")
    if mutation == "exception":
        definitions.append("RANDX_AUDIT_MUTATE_EXCEPTION=1")
    if kind in {"gcc", "clang"}:
        command = [str(compiler), f"-std={cpp_standard}", "-O2", "-DNDEBUG", "-g", "-gdwarf-4"]
        if optimization == "lto":
            command.append("-flto")
        command.extend(f"-D{definition}" for definition in definitions)
        command.extend(["-I", str(include_dir), str(source)])
        if os.name == "nt":
            command.append("-lbcrypt")
        elif sys.platform == "darwin":
            command.extend(["-framework", "Security"])
        command.extend(["-o", str(executable)])
        return command

    command = [str(compiler), "/nologo", "/O2", "/DNDEBUG", "/Zi", "/EHsc"]
    command.append("/std:c++17" if standard == "cpp17" else "/std:c++23preview")
    if optimization == "lto":
        command.append("/GL")
    command.extend(f"/D{definition}" for definition in definitions)
    command.extend([f"/I{include_dir}", str(source), f"/Fe:{executable}", "/link"])
    if optimization == "lto":
        command.append("/LTCG")
    command.append("bcrypt.lib")
    return command


def _header_supports_portable(text: str) -> bool:
    return bool(re.search(r"#\s*(?:if|elif).*RANDX_USE_PORTABLE_SECURE_WIPE", text))


def _prepare_headers(source_dir: pathlib.Path, header_dir: pathlib.Path, work_dir: pathlib.Path, mutation: str | None) -> pathlib.Path:
    include_dir = work_dir / "include"
    include_dir.mkdir(parents=True, exist_ok=True)
    for filename in HEADER_NAMES.values():
        data = (header_dir / filename).read_bytes()
        if mutation not in {None, "direct", "scoped", "exception"}:
            text = _decode(data)
            data = mutate_header(text, mutation).encode("utf-8")
        (include_dir / filename).write_bytes(data)
    return include_dir


def _disassemble(
    binary: pathlib.Path,
    disassembler: pathlib.Path | None,
    compiler_kind: str,
    cwd: pathlib.Path,
) -> tuple[str | None, str | None, list[str] | None]:
    if disassembler is None:
        return None, None, None
    if disassembler.name.lower().startswith("dumpbin"):
        command = [str(disassembler), "/nologo", "/DISASM", str(binary)]
    else:
        command = [str(disassembler), "-dCSl", str(binary)]
    code, stdout, stderr = _capture(command, cwd)
    if code != 0:
        return None, stderr or stdout, command
    return stdout, stderr, command


def _audit_registered_configuration(info: dict[str, object], disassembly: str, unwind_text: str = "", triangular: bool = True, debug_text: str = "") -> CaseResult:
    kind = info.get("kind")
    target = str(info.get("target", "")).lower()
    version = str(info.get("version", ""))
    if kind != "gcc" or not target.startswith("x86_64-w64-mingw32"):
        return CaseResult(Status.UNDETERMINED, ["当前没有为该编译器/ABI登记并完成变异确认的机器码规则"], {})
    supported_version = any(
        re.search(rf"(?<![\w.-]){re.escape(candidate)}(?![\w.-])", version)
        for candidate in SUPPORTED_MINGW_GCC_VERSIONS
    )
    if not supported_version:
        return CaseResult(Status.UNDETERMINED, ["GCC/MinGW 版本不在已验证规则集中"], {"compiler": info})
    return audit_disassembly(disassembly, unwind_text, triangular, debug_text)


def run_case(
    *,
    root: pathlib.Path,
    compiler: pathlib.Path | None,
    disassembler: pathlib.Path | None,
    header_dir: pathlib.Path,
    output_dir: pathlib.Path,
    standard: str,
    optimization: str,
    backend: str,
    mutation: str | None,
) -> CaseResult:
    label = _case_label(standard, optimization, backend)
    case_dir = output_dir / label / (mutation or "baseline")
    case_dir.mkdir(parents=True, exist_ok=True)
    if compiler is None:
        return CaseResult(Status.UNDETERMINED, ["找不到所需 C++ 编译器"], {"case": label})

    header = header_dir / HEADER_NAMES[standard]
    if not header.is_file():
        return CaseResult(Status.FAIL, [f"缺少头文件 {header}"], {"case": label})
    header_text = _decode(header.read_bytes())
    if backend == "portable" and not _header_supports_portable(header_text):
        return CaseResult(Status.UNDETERMINED, ["此头文件尚未实现 RANDX_USE_PORTABLE_SECURE_WIPE 选择"], {"case": label})

    info = _compiler_info(compiler, root)
    include_dir = _prepare_headers(root, header_dir, case_dir, mutation)
    probe_source = root / PROBE_SOURCE
    executable = case_dir / ("secure_wipe_probe.exe" if os.name == "nt" else "secure_wipe_probe")
    command = _compiler_command(
        str(info["kind"]), compiler, probe_source, include_dir, executable,
        standard, optimization, backend, mutation,
    )
    triangular = "RandTriangular" in header_text
    if triangular:
        command.insert(1, "-DRANDX_AUDIT_TRIANGULAR=1" if info["kind"] != "msvc" else "/DRANDX_AUDIT_TRIANGULAR=1")
    (case_dir / "compile-command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    compile_result = _run(command, root)
    compile_out = _decode(compile_result.stdout) + _decode(compile_result.stderr)
    (case_dir / "compile.log").write_text(compile_out, encoding="utf-8")
    (case_dir / "compiler.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
    if compile_result.returncode != 0:
        return CaseResult(Status.FAIL, [f"编译或链接失败（退出码 {compile_result.returncode}）"], {"case": label, "compiler": info})
    execution = _run([str(executable)], root)
    (case_dir / "execution.log").write_text(_decode(execution.stdout) + _decode(execution.stderr), encoding="utf-8")
    if execution.returncode != 0:
        return CaseResult(Status.FAIL, [f"独立进程探针失败（退出码 {execution.returncode}）"], {"case": label, "compiler": info})

    dump, dump_error, disassembly_command = _disassemble(executable, disassembler, str(info["kind"]), root)
    if disassembly_command:
        (case_dir / "disassembler-command.json").write_text(json.dumps(disassembly_command, indent=2), encoding="utf-8")
    if dump is None:
        if dump_error:
            (case_dir / "disassembler.log").write_text(dump_error, encoding="utf-8")
        return CaseResult(Status.UNDETERMINED, ["最终产物反汇编工具缺失或执行失败"], {"case": label, "compiler": info})
    (case_dir / "final.disasm.txt").write_text(dump, encoding="utf-8")
    unwind_text = ""
    if disassembler is not None and info["kind"] == "gcc":
        unwind_command = [str(disassembler), "-p", str(executable)]
        code, unwind_text, unwind_error = _capture(unwind_command, root)
        (case_dir / "unwind-command.json").write_text(json.dumps(unwind_command, indent=2), encoding="utf-8")
        (case_dir / "unwind.txt").write_text(unwind_text + unwind_error, encoding="utf-8")
        if code:
            unwind_text = ""
    debug_text = ""
    if disassembler is not None and info["kind"] == "gcc":
        debug_command = [str(disassembler), "--dwarf=info", str(executable)]
        code, debug_text, debug_error = _capture(debug_command, root)
        (case_dir / "debug-command.json").write_text(json.dumps(debug_command, indent=2), encoding="utf-8")
        (case_dir / "debug.txt").write_text(debug_text + debug_error, encoding="utf-8")
        if code:
            debug_text = ""
    audit = _audit_registered_configuration(info, dump, unwind_text, triangular, debug_text)
    audit.evidence.update({"case": label, "mutation": mutation or "none", "compiler": info, "binary": str(executable)})
    (case_dir / "audit.json").write_text(
        json.dumps({"status": audit.status.value, "reasons": audit.reasons, "evidence": audit.evidence}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    return audit


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="检查 RandX 安全擦除在最终优化链接产物中的机器码证据。")
    parser.add_argument("--compiler", help="C++ 编译器路径；默认依次查找 CXX、g++、clang++、cl")
    parser.add_argument("--disassembler", help="objdump、llvm-objdump 或 dumpbin 路径")
    parser.add_argument("--standard", choices=("cpp17", "cpp23", "both"), default="both")
    parser.add_argument("--optimization", choices=("release", "lto", "both"), default="both")
    parser.add_argument("--backend", choices=("native", "portable", "both"), default="native")
    parser.add_argument("--header-dir", default=".", help="待审计 RandX.hpp / RandX_Cpp17.hpp 所在目录")
    parser.add_argument("--output-dir", default="build/secure-wipe-audit", help="保存二进制、命令、反汇编和判定的目录")
    parser.add_argument("--mutation", choices=MUTATION_NAMES, help="只构建指定缺失擦除点的阴性变异")
    parser.add_argument("--verify-mutations", action="store_true", help="基线通过后构建全局空擦除与每个单点变异")
    parser.add_argument("--list-mutations", action="store_true", help="列出支持的变异名称")
    return parser.parse_args(argv)


def mutation_detected(result: CaseResult, mutation: str) -> bool:
    checks = result.evidence.get("checks", {})
    return result.status is Status.FAIL and bool(result.evidence.get("audit_completed")) and any(
        name in checks and checks[name].get("status", "fail" if not checks[name].get("passed", True) else "pass") == "fail"
        for name in MUTATION_FAILURE_CHECKS[mutation])


def _print_line(text: str) -> None:
    try:
        print(text)
    except UnicodeEncodeError:
        print(text.encode("ascii", errors="backslashreplace").decode("ascii"))


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv or sys.argv[1:])
    if args.list_mutations:
        _print_line("\n".join(MUTATION_NAMES))
        return EXIT_PASS

    root = pathlib.Path(__file__).resolve().parent.parent
    compiler_arg = args.compiler or os.environ.get("CXX")
    compiler = _find_tool(compiler_arg, ("g++", "clang++", "cl"))
    if compiler is None and compiler_arg:
        print(f"UNDETERMINED compiler: {compiler_arg} 未找到")
        return EXIT_UNDETERMINED
    disassembler = _find_tool(args.disassembler, ("llvm-objdump", "objdump", "dumpbin"))
    header_dir = pathlib.Path(args.header_dir).resolve()
    output_dir = pathlib.Path(args.output_dir).resolve()
    standards = ("cpp17", "cpp23") if args.standard == "both" else (args.standard,)
    optimizations = ("release", "lto") if args.optimization == "both" else (args.optimization,)
    backends = ("native", "portable") if args.backend == "both" else (args.backend,)
    matrix: list[tuple[str, str, str]] = [
        (standard, optimization, backend)
        for standard in standards
        for optimization in optimizations
        for backend in backends
    ]
    results: list[tuple[str, str | None, CaseResult]] = []
    for standard, optimization, backend in matrix:
        baseline = run_case(
            root=root, compiler=compiler, disassembler=disassembler, header_dir=header_dir,
            output_dir=output_dir, standard=standard, optimization=optimization,
            backend=backend, mutation=None,
        )
        results.append((_case_label(standard, optimization, backend), None, baseline))
        if args.mutation:
            mutant = run_case(
                root=root, compiler=compiler, disassembler=disassembler, header_dir=header_dir,
                output_dir=output_dir, standard=standard, optimization=optimization,
                backend=backend, mutation=args.mutation,
            )
            results.append((_case_label(standard, optimization, backend), args.mutation, mutant))
        elif args.verify_mutations and baseline.status is Status.PASS:
            for mutation in MUTATION_NAMES:
                mutant = run_case(
                    root=root, compiler=compiler, disassembler=disassembler, header_dir=header_dir,
                    output_dir=output_dir, standard=standard, optimization=optimization,
                    backend=backend, mutation=mutation,
                )
                detected = mutation_detected(mutant, mutation)
                result_status = Status.PASS if detected else (Status.UNDETERMINED if mutant.status is Status.UNDETERMINED else Status.FAIL)
                result_reasons = [f"变异 {mutation} 被同一检查器拒绝"] if detected else [f"变异 {mutation} 未被确认拒绝", *mutant.reasons]
                results.append(( _case_label(standard, optimization, backend), mutation,
                    CaseResult(result_status, result_reasons, {"detected": detected, **mutant.evidence})))

    overall = Status.PASS
    report_cases: list[dict[str, object]] = []
    for label, mutation, result in results:
        report_cases.append({"case": label, "mutation": mutation or "none", "status": result.status.value, "reasons": result.reasons, "evidence": result.evidence})
        if result.status is Status.FAIL:
            overall = Status.FAIL
        elif result.status is Status.UNDETERMINED and overall is Status.PASS:
            overall = Status.UNDETERMINED
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "report.json").write_text(
        json.dumps({"status": overall.value, "cases": report_cases}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    for case in report_cases:
        _print_line(
            f"{str(case['status']).upper():14} {case['case']:32} mutation={case['mutation']}"
        )
        for reason in case["reasons"]:
            _print_line(f"  {reason}")
    _print_line(f"Overall: {overall.value.upper()} | evidence: {output_dir / 'report.json'}")
    return {Status.PASS: EXIT_PASS, Status.FAIL: EXIT_FAIL, Status.UNDETERMINED: EXIT_UNDETERMINED}[overall]


if __name__ == "__main__":
    raise SystemExit(main())
