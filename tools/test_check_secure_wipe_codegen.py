"""验证安全擦除机器码检查器的范围、顺序和失败语义。"""
from pathlib import Path
import tempfile
from contextlib import redirect_stdout
import io
import unittest
from unittest.mock import patch

import check_secure_wipe_codegen as audit


def function_code(instructions: list[tuple[str, str]]) -> audit.FunctionCode:
    if instructions[-1][0] not in {"ret", "call"}:
        instructions = [*instructions, ("ret", "")]
    return audit.FunctionCode("probe", 0x1000, [
        audit.Instruction(0x1000 + index, mnemonic, operands, None, None)
        for index, (mnemonic, operands) in enumerate(instructions)])


class RangeTests(unittest.TestCase):
    def test_windows_source_paths_and_instructions_are_separate(self):
        parsed = audit.parse_objdump("00001000 <probe>:\nC:/repo/file.hpp:23\n 1000: 90 nop\n")
        self.assertEqual(parsed[0].instructions[0].source_line, 23)
        self.assertEqual(parsed[0].instructions[0].source_path, "C:/repo/file.hpp")

    def test_rep_stos_requires_zero_value_and_exact_extent(self):
        code = function_code([("lea", "0x20(%rsp),%rdi"), ("xor", "%eax,%eax"),
            ("mov", "$0x25,%ecx"), ("rep", "stos %al,(%rdi)")])
        self.assertTrue(audit._covers(code, audit.Address("rsp", 32), 37))
        self.assertFalse(audit._covers(code, audit.Address("rsp", 32), 38))
        code.instructions[1] = audit.Instruction(0x1001, "mov", "$0x1,%eax", None, None)
        self.assertFalse(audit._covers(code, audit.Address("rsp", 32), 37))

    def test_initialization_does_not_satisfy_post_consume_requirement(self):
        code = function_code([("lea", "0x20(%rsp),%rdi"), ("xor", "%eax,%eax"),
            ("mov", "$0x25,%ecx"), ("rep", "stos %al,(%rdi)"),
            ("lea", "0x20(%rsp),%rcx"), ("mov", "$0x25,%edx"),
            ("call", "2000 <consumeBytes>")])
        self.assertFalse(audit._has_exact_post_consume_wipe(code, 37)[0])

    def test_wrong_object_does_not_satisfy_wipe(self):
        code = function_code([("mov", "%rdx,%rdi"), ("xor", "%eax,%eax"),
            ("mov", "$0x30,%ecx"), ("rep", "stos %al,(%rdi)")])
        self.assertTrue(audit._covers(code, audit.Address("arg1"), audit.CHACHA_STATE_BYTES))
        self.assertFalse(audit._covers(code, audit.Address("arg0"), audit.CHACHA_STATE_BYTES))

    def test_pointer_spill_and_reload_preserve_target_identity(self):
        code = function_code([("mov", "%rcx,0x20(%rsp)"), ("mov", "0x20(%rsp),%rdi"),
            ("xor", "%eax,%eax"), ("mov", "$0x30,%ecx"), ("rep", "stos %al,(%rdi)")])
        self.assertTrue(audit._covers(code, audit.Address("arg0"), audit.CHACHA_STATE_BYTES))

    def test_unknown_configuration_is_undetermined(self):
        self.assertEqual(audit._audit_registered_configuration(
            {"kind": "clang", "target": "x86_64-w64-mingw32"}, "").status, audit.Status.UNDETERMINED)
        self.assertEqual(audit.audit_disassembly("unsupported format").status, audit.Status.UNDETERMINED)

    def test_registered_mingw_gcc_versions_are_explicit(self):
        info: dict[str, object] = {
            "kind": "gcc",
            "target": "x86_64-w64-mingw32",
            "version": "",
        }
        passed: audit.CaseResult = audit.CaseResult(audit.Status.PASS, [], {})
        for version in ("16.1.0", "16.2.0"):
            with self.subTest(version=version), patch.object(
                audit, "audit_disassembly", return_value=passed
            ) as recognize:
                info["version"] = f"g++.exe (Rev4, Built by MSYS2 project) {version}"
                result: audit.CaseResult = audit._audit_registered_configuration(
                    info, "machine code", "unwind data", False, "DWARF data"
                )
                self.assertIs(result, passed)
                recognize.assert_called_once_with("machine code", "unwind data", False, "DWARF data")

        for version in ("16.2.1", "16.3.0", "17.1.0"):
            with self.subTest(version=version), patch.object(audit, "audit_disassembly") as recognize:
                info["version"] = f"g++.exe (Rev4, Built by MSYS2 project) {version}"
                result: audit.CaseResult = audit._audit_registered_configuration(info, "machine code")
                self.assertEqual(result.status, audit.Status.UNDETERMINED)
                recognize.assert_not_called()

    def test_unrolled_loop_requires_every_byte_in_the_stride(self):
        code = function_code([("lea", "0x20(%rsp),%rax"), ("lea", "0x60(%rsp),%rdx"),
            ("movb", "$0x0,(%rax)"), ("add", "$0x2,%rax"),
            ("movb", "$0x0,-0x1(%rax)"), ("cmp", "%rax,%rdx"), ("jne", "1002 <probe+0x2>")])
        self.assertTrue(audit._covers(code, audit.Address("rsp", 32), 64))
        code.instructions[4] = audit.Instruction(0x1004, "nop", "", None, None)
        self.assertFalse(audit._covers(code, audit.Address("rsp", 32), 64))

    def test_indexed_exception_loop_has_the_full_extent(self):
        code = function_code([("xor", "%eax,%eax"), ("movb", "$0x0,0x20(%rsp,%rax,1)"),
            ("add", "$0x1,%rax"), ("cmp", "$0x29,%rax"), ("jne", "1001 <probe+0x1>")])
        self.assertTrue(audit._covers(code, audit.Address("rsp", 32), 41))
        self.assertFalse(audit._covers(code, audit.Address("rsp", 32), 42))

    def test_normal_seed_wipe_does_not_cover_exception_exit(self):
        prefix = [("mov", "%rcx,%rsi"), ("movl", "$0x1,(%rsi)"),
            ("lea", "0x20(%rsp),%rdi"), ("xor", "%eax,%eax"),
            ("mov", "$0x2c,%ecx"), ("rep", "stos %al,(%rdi)"), ("ret", "")]
        code = function_code(prefix + [("call", "2000 <_Unwind_Resume>")])
        code.name = "RandX::ChaCha20::reseed()"
        self.assertTrue(audit._lifecycle_checks([code])["reseed_seed"]["passed"])
        self.assertFalse(audit._lifecycle_checks([code])["reseed_seed_exception"]["passed"])
        code = function_code(prefix + [("lea", "0x20(%rsp),%rdi"), ("xor", "%eax,%eax"),
            ("mov", "$0x2c,%ecx"), ("rep", "stos %al,(%rdi)"), ("call", "2000 <_Unwind_Resume>")])
        code.name = "RandX::ChaCha20::reseed()"
        self.assertFalse(audit._lifecycle_checks([code])["reseed_seed_exception"]["passed"])
        self.assertEqual(audit._lifecycle_checks([code])["reseed_seed_exception"]["status"], "undetermined")

    def test_initialization_requires_a_positive_control(self):
        invalid = function_code([("ret", "")])
        invalid.name = "randx_audit_triangular_invalid"
        degenerate = function_code([("ret", "")])
        degenerate.name = "randx_audit_triangular_degenerate"
        valid = function_code([("call", "2000 <RandX::detail::CreateDefaultEngine()>")])
        valid.name = "randx_audit_triangular_valid"
        checks = audit._initialization_checks([invalid, degenerate, valid])
        self.assertTrue(all(check["passed"] for check in checks.values()))
        self.assertFalse(audit._initialization_checks([invalid, degenerate])["triangular_first_valid"]["passed"])


class ControlFlowTests(unittest.TestCase):
    def test_unconditional_jump_over_wipe_fails(self):
        code = function_code([("jmp", "1005 <probe+0x5>"), ("lea", "0x20(%rsp),%rdi"),
            ("xor", "%eax,%eax"), ("mov", "$0x25,%ecx"), ("rep", "stos %al,(%rdi)"), ("ret", "")])
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 37)[0], audit.Status.FAIL)

    def test_conditional_path_over_wipe_fails(self):
        code = function_code([("test", "%rdx,%rdx"), ("je", "1006 <probe+0x6>"),
            ("lea", "0x20(%rsp),%rdi"), ("xor", "%eax,%eax"), ("mov", "$0x25,%ecx"),
            ("rep", "stos %al,(%rdi)"), ("ret", "")])
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 37)[0], audit.Status.FAIL)

    def test_partial_wipes_on_exclusive_paths_do_not_combine(self):
        code = function_code([("test", "%rdx,%rdx"), ("je", "1004 <probe+0x4>"),
            ("movb", "$0x0,0x20(%rsp)"), ("jmp", "1005 <probe+0x5>"),
            ("movb", "$0x0,0x21(%rsp)"), ("ret", "")])
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 2)[0], audit.Status.FAIL)

    def test_wipe_after_return_cannot_cover_normal_exit(self):
        code = function_code([("ret", ""), ("lea", "0x20(%rsp),%rdi"),
            ("xor", "%eax,%eax"), ("mov", "$0x2c,%ecx"), ("rep", "stos %al,(%rdi)"),
            ("call", "2000 <_Unwind_Resume>")])
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 44)[0], audit.Status.FAIL)

    def test_exception_landing_must_execute_wipe(self):
        code = function_code([("call", "2000 <protectedCall>"), ("lea", "0x20(%rsp),%rdi"),
            ("xor", "%eax,%eax"), ("mov", "$0x2c,%ecx"), ("rep", "stos %al,(%rdi)"),
            ("ret", ""), ("lea", "0x20(%rsp),%rdi"), ("xor", "%eax,%eax"),
            ("mov", "$0x2c,%ecx"), ("rep", "stos %al,(%rdi)"), ("call", "2000 <_Unwind_Resume>")])
        code.exception_sites = [(0x1000, 0x1001, 0x1006)]
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 44)[0], audit.Status.PASS)
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 44, exit_kind="exception")[0], audit.Status.PASS)
        code.exception_sites = [(0x1000, 0x1001, 0x100a)]
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 44, exit_kind="exception")[0], audit.Status.FAIL)

    def test_pe_exception_table_supplies_real_landing(self):
        code = function_code([("call", "2000 <protectedCall>"), ("ret", ""), ("call", "2000 <_Unwind_Resume>")])
        audit.attach_exception_tables([code], """ 1000 (rva: 1000): 1000 - 1003
 Flags: UNW_FLAG_EHANDLER
 User data:
 000: ff ff 01 04 00 01 02 00
""")
        self.assertEqual(code.exception_sites, [(0x1000, 0x1001, 0x1002)])

    def test_named_array_binding_ignores_declaration_line_spills(self):
        code = function_code([("push", "%rbx"), ("sub", "$0x80,%rsp"),
            ("movl", "$0xa,0x20(%rsp)"), ("ret", "")])
        audit.attach_local_materials([code], """ <1><10>: Abbrev Number: 1 (DW_TAG_subprogram)
 <11> DW_AT_low_pc : 0x1000
 <12> DW_AT_frame_base : 1 byte block: 9c (DW_OP_call_frame_cfa)
 <2><20>: Abbrev Number: 2 (DW_TAG_variable)
 <21> DW_AT_name : working
 <22> DW_AT_location : 2 byte block: 91 30 (DW_OP_fbreg: -80)
 <1><30>: Abbrev Number: 0
""")
        self.assertEqual(code.local_materials, {"working": audit.Address("rsp", 64)})

    def test_pointer_identity_is_checked_on_each_branch(self):
        code = function_code([("lea", "0x20(%rsp),%rdi"), ("test", "%rdx,%rdx"),
            ("je", "1004 <probe+0x4>"), ("lea", "0x40(%rsp),%rdi"),
            ("xor", "%eax,%eax"), ("mov", "$0x2,%ecx"), ("rep", "stos %al,(%rdi)"), ("ret", "")])
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 2)[0], audit.Status.FAIL)

    def test_unknown_jump_target_is_undetermined(self):
        code = function_code([("jmp", "*%rax"), ("ret", "")])
        self.assertEqual(audit._path_coverage(code, audit.Address("rsp", 32), 1)[0], audit.Status.UNDETERMINED)


class MutationTests(unittest.TestCase):
    def test_each_registered_header_mutation_has_a_unique_site(self):
        root = Path(__file__).resolve().parent.parent
        for name in audit.HEADER_NAMES.values():
            original = (root / name).read_text(encoding="utf-8")
            for mutation in audit.MUTATION_NAMES:
                with self.subTest(header=name, mutation=mutation):
                    changed = audit.mutate_header(original, mutation)
                    if mutation not in {"direct", "scoped", "exception"}:
                        self.assertNotEqual(original, changed)

    def test_unrelated_failure_cannot_validate_a_mutation(self):
        result = audit.CaseResult(audit.Status.FAIL, [], {"audit_completed": True,
            "checks": {"direct": {"status": "fail"}, "generate_working": {"status": "pass"}}})
        self.assertFalse(audit.mutation_detected(result, "generate_working"))
        self.assertTrue(audit.mutation_detected(result, "direct"))

    def test_undetermined_related_check_cannot_validate_a_mutation(self):
        result = audit.CaseResult(audit.Status.FAIL, [], {"audit_completed": True,
            "checks": {"direct": {"status": "fail"}, "generate_working": {"status": "undetermined"}}})
        self.assertFalse(audit.mutation_detected(result, "generate_working"))

    def test_compiler_failure_cannot_validate_a_mutation(self):
        failure = audit.CaseResult(audit.Status.FAIL, ["compile failed"], {})
        baseline = audit.CaseResult(audit.Status.PASS, [], {"audit_completed": True})
        with redirect_stdout(io.StringIO()), tempfile.TemporaryDirectory() as directory, patch.object(audit, "_find_tool", return_value=Path("compiler")), \
            patch.object(audit, "run_case", side_effect=[baseline] + [failure] * len(audit.MUTATION_NAMES)):
            result = audit.main(["--standard", "cpp23", "--optimization", "release", "--backend", "native",
                "--output-dir", directory, "--verify-mutations"])
        self.assertEqual(result, audit.EXIT_FAIL)

    def test_console_encoding_failure_keeps_report_and_exit_status(self):
        class Cp1252Stream(io.StringIO):
            def write(self, text: str) -> int:
                text.encode("cp1252")
                return super().write(text)

        result: audit.CaseResult = audit.CaseResult(
            audit.Status.UNDETERMINED,
            ["GCC/MinGW 版本不在已验证规则集中"],
            {"audit_completed": False},
        )
        with tempfile.TemporaryDirectory() as directory:
            console: Cp1252Stream = Cp1252Stream()
            with redirect_stdout(console), patch.object(audit, "_find_tool", return_value=Path("compiler")), patch.object(
                audit, "run_case", return_value=result
            ):
                exit_code: int = audit.main([
                    "--standard", "cpp23", "--optimization", "release", "--backend", "native",
                    "--output-dir", directory,
                ])

            report: str = (Path(directory) / "report.json").read_text(encoding="utf-8")
            self.assertEqual(exit_code, audit.EXIT_UNDETERMINED)
            self.assertIn("GCC/MinGW 版本不在已验证规则集中", report)
            self.assertIn(r"\u", console.getvalue())


if __name__ == "__main__":
    unittest.main()
