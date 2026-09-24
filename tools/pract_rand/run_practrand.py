#!/usr/bin/env python3
"""RandX PractRand 统计质量验证驱动.

用法:
    python3 run_practrand.py                    # 默认验证全部 8 引擎（统计 PRNG 1TB / ChaCha20 256GB）
    python3 run_practrand.py --engine sfc64     # 只测一个引擎
    python3 run_practrand.py --length 4GB      # 自定义测试长度
    python3 run_practrand.py --keep-going       # 失败仍继续后续引擎

前置条件:
    1. PractRand 二进制已构建（见 download_practrand.sh），或在 PATH 中存在 RNG_test
    2. gen_practrand_stream.cpp 已编译为 ./gen_practrand_stream

退出码:
    0 = pass: 全部引擎通过
    1 = statistical_failure: 至少一个引擎统计失败
    2 = environment_error: 环境或执行异常
    3 = inconclusive: 测试未完成或结果不足以判定
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

# ============================================================================
# 常量与配置
# ============================================================================

STATISTICAL_PRNG_LENGTH = "1TB"
CSPRNG_LENGTH = "256GB"

# 64位引擎与32位引擎分类（决定输入流模式：stdin64 或 stdin32）
ENGINES_64BIT = {
    "xoshiro256",
    "xoroshiro128",
    "splitmix64",
    "sfc64",
    "romuduojr",
    "chacha20",
}

ENGINES_32BIT = {
    "xoshiro128",
    "xoroshiro64",
}

STATISTICAL_ENGINES = (
    "xoshiro256",
    "xoroshiro128",
    "xoshiro128",
    "xoroshiro64",
    "splitmix64",
    "sfc64",
    "romuduojr",
)
CSPRNG_ENGINE = "chacha20"

FAILURE_PATTERNS = (
    re.compile(r"\bFAIL\b", re.IGNORECASE),
    re.compile(r"\bVERY\s+SUSPICIOUS\b", re.IGNORECASE),
    re.compile(r"^\s*!!", re.MULTILINE | re.IGNORECASE),
)

SUSPICIOUS_PATTERNS = (
    re.compile(r"\bSUSPICIOUS\b", re.IGNORECASE),
    re.compile(r"\bunusual\b", re.IGNORECASE),
)

CHECKPOINT_COMPLETE_PATTERNS = (
    re.compile(r"no\s+anomalies\s+in\s+(\d+)\s+test\s+result\(s\)", re.IGNORECASE),
    re.compile(r"(?:and|\.\.\.and)\s+(\d+)\s+(?:other\s+)?test\s+result\(s\)", re.IGNORECASE),
    re.compile(r"and\s+(\d+)\s+test\s+result\(s\)\s+without\s+anomalies", re.IGNORECASE),
)

POWER_OF_TWO_BYTES_PATTERN = re.compile(r"\(2\^(\d+)\s*bytes\)", re.IGNORECASE)
LENGTH_BYTES_PATTERN = re.compile(
    r"length=\s*([\d.]+)\s*(kilobytes|megabytes|gigabytes|terabytes|bytes)",
    re.IGNORECASE,
)
EXPLICIT_TEST_EVAL_PATTERN = re.compile(
    r"(?:Test Name:|\.\.\.\s*(?:unusual|mildly suspicious|suspicious|very suspicious|FAIL))",
    re.IGNORECASE,
)

UNIT_MULTIPLIERS = {
    "bytes": 1,
    "kilobytes": 1024,
    "megabytes": 1024**2,
    "gigabytes": 1024**3,
    "terabytes": 1024**4,
}

POLL_INTERVAL_SECONDS = 0.05
GRACE_PERIOD_SECONDS = 1.0
SUBPROCESS_CLEANUP_TIMEOUT_SECONDS = 1.0
EARLY_FAIL_GRACE_PERIOD_SECONDS = 0.5
POSIX_SIGPIPE = -13
EXIT_SIGPIPE_SHELL = 141
DEFAULT_TEST_TIMEOUT_SECONDS = 14400

STATUS_EXIT_CODES = {
    "pass": 0,
    "statistical_failure": 1,
    "environment_error": 2,
    "inconclusive": 3,
}

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
PRACTRAND_BUILD_DIR = SCRIPT_DIR / "PractRand_build"
PRACTRAND_LOCK_FILE = SCRIPT_DIR / "practrand.lock"
LOGS_DIR = SCRIPT_DIR / "logs"


# ============================================================================
# 数据结构
# ============================================================================

@dataclass
class Checkpoint:
    tested_bytes: int = 0
    test_count: int = 0
    suspicious_count: int = 0
    has_failure: bool = False
    is_complete: bool = False
    lines: list[str] = field(default_factory=list)


class ClassificationOutput(tuple):
    """分类结果封装，兼容五元组解包同时支持结构化属性访问."""

    status: str
    reason: str
    reported_tested_bytes: int
    max_tested_bytes: int
    test_count: int
    suspicious_count: int
    execution_status: str
    statistical_status: str
    reason_codes: list[str]

    def __new__(
        cls,
        status: str,
        reason: str,
        max_tested_bytes: int,
        test_count: int,
        suspicious_count: int,
        execution_status: str = "ok",
        statistical_status: str = "pass",
        reason_codes: list[str] | None = None,
    ):
        instance = super().__new__(
            cls,
            (status, reason, max_tested_bytes, test_count, suspicious_count),
        )
        instance.status = status
        instance.reason = reason
        instance.reported_tested_bytes = max_tested_bytes
        instance.max_tested_bytes = max_tested_bytes
        instance.test_count = test_count
        instance.suspicious_count = suspicious_count
        instance.execution_status = execution_status
        instance.statistical_status = statistical_status
        instance.reason_codes = list(reason_codes or [])
        return instance


@dataclass
class TestResult:
    schema_version: str = "2.0"
    engine: str = ""
    status: str = ""  # pass, statistical_failure, environment_error, inconclusive
    execution_status: str = ""  # ok, failed, timeout, cancelled, unknown
    statistical_status: str = ""  # pass, failure, insufficient_evidence
    reason: str = ""
    reason_codes: list[str] = field(default_factory=list)
    randx_commit: str = ""
    practrand_commit: str = ""
    command: list[str] = field(default_factory=list)
    seed: int | str = ""
    target_bytes: int = 0
    reported_tested_bytes: int = 0
    test_count: int = 0
    suspicious_count: int = 0
    gen_returncode: int | None = None
    pr_returncode: int | None = None
    duration_seconds: float = 0.0
    log_file: str = ""
    generator_log_file: str = ""
    generator: dict = field(default_factory=dict)
    tester: dict = field(default_factory=dict)


# ============================================================================
# 辅助函数
# ============================================================================

def parse_length_to_bytes(length_str: str) -> int:
    """将人类可读长度（如 4GB, 512MB, 1TB）转换为字节数。"""
    s = length_str.strip().upper()
    multipliers = {
        "KB": 1024,
        "MB": 1024**2,
        "GB": 1024**3,
        "TB": 1024**4,
    }
    for unit, mult in multipliers.items():
        if s.endswith(unit):
            return int(float(s[: -len(unit)].strip()) * mult)
    if s.endswith("B"):
        return int(float(s[:-1].strip()))
    return int(float(s))


def get_git_commit(cwd: Path) -> str:
    """获取指定仓库的 HEAD commit 哈希。"""
    try:
        res = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=str(cwd),
            capture_output=True,
            text=True,
            check=True,
        )
        return res.stdout.strip()
    except Exception:
        return "unknown"


def load_practrand_lock_commit() -> str:
    """读取 practrand.lock 中记录的 upstream commit。"""
    if PRACTRAND_LOCK_FILE.exists():
        try:
            with open(PRACTRAND_LOCK_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
                return data.get("commit", "unknown")
        except Exception:
            pass
    return "unknown"


def find_practrand() -> str:
    """返回 RNG_test 可执行文件绝对路径，不存在返回空字符串。"""
    binary_name = "RNG_test.exe" if os.name == "nt" else "RNG_test"
    candidate = PRACTRAND_BUILD_DIR / binary_name
    if candidate.exists():
        return str(candidate)
    candidate_no_ext = PRACTRAND_BUILD_DIR / "RNG_test"
    if candidate_no_ext.exists():
        return str(candidate_no_ext)
    return shutil.which("RNG_test") or ""


def ensure_generator_built() -> str:
    """确保 gen_practrand_stream 已编译且与头文件依赖同步。"""
    exe_name = "gen_practrand_stream.exe" if os.name == "nt" else "gen_practrand_stream"
    exe_path = SCRIPT_DIR / exe_name
    src = SCRIPT_DIR / "gen_practrand_stream.cpp"
    header1 = REPO_ROOT / "RandX.hpp"
    header2 = REPO_ROOT / "RandX_Cpp17.hpp"

    if not src.exists():
        raise FileNotFoundError(f"找不到源文件: {src}")

    needs_build = not exe_path.exists()
    if not needs_build:
        exe_mtime = exe_path.stat().st_mtime
        if src.stat().st_mtime > exe_mtime:
            needs_build = True
        elif header1.exists() and header1.stat().st_mtime > exe_mtime:
            needs_build = True
        elif header2.exists() and header2.stat().st_mtime > exe_mtime:
            needs_build = True

    if needs_build:
        if exe_path.exists():
            exe_path.unlink()
        print(f"[build] 编译 {src.name} ...", file=sys.stderr)
        link_libs = []
        if os.name == "nt":
            link_libs = ["-lbcrypt"]
        elif sys.platform == "darwin":
            link_libs = ["-framework", "Security"]

        cmd = [
            "g++",
            "-std=c++17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wno-unknown-pragmas",
            f"-I{REPO_ROOT}",
            "-o",
            str(exe_path),
            str(src),
            *link_libs,
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(f"编译失败:\n{result.stderr}")

    return str(exe_path)


def atomic_write_json(file_path: Path | str, data: list[dict] | dict) -> None:
    """使用临时文件和原子替换保存 JSON 数据."""
    p = Path(file_path).resolve()
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp_p = p.with_name(f"{p.name}.tmp.{os.getpid()}_{int(time.time() * 1000)}")
    try:
        with open(tmp_p, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_p, p)
    finally:
        if tmp_p.exists():
            try:
                tmp_p.unlink()
            except OSError:
                pass


def _terminate_and_kill(proc: subprocess.Popen | None, timeout: float = SUBPROCESS_CLEANUP_TIMEOUT_SECONDS) -> None:
    """分阶段终止进程：先 terminate 并等待，未退出则尝试平台级进程树清理与 kill 并等待."""
    if proc is None or proc.poll() is not None:
        return
    try:
        proc.terminate()
        proc.wait(timeout=timeout)
    except (subprocess.TimeoutExpired, OSError):
        pass

    if proc.poll() is None:
        if sys.platform == "win32":
            try:
                subprocess.run(
                    ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
                )
            except OSError:
                pass
        try:
            proc.kill()
            proc.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, OSError):
            pass


# ============================================================================
# 报告解析
# ============================================================================

def parse_checkpoints(output: str) -> list[Checkpoint]:
    """独立解析输出文本中的各个 Checkpoint 块."""
    checkpoints: list[Checkpoint] = []
    current_cp: Checkpoint | None = None

    for line in output.splitlines(keepends=True):
        m_len = LENGTH_BYTES_PATTERN.search(line)
        if m_len:
            if current_cp is not None:
                checkpoints.append(current_cp)
            current_cp = Checkpoint()
            current_cp.lines.append(line)

            m_pow2 = POWER_OF_TWO_BYTES_PATTERN.search(line)
            if m_pow2:
                current_cp.tested_bytes = 1 << int(m_pow2.group(1))
            else:
                val = float(m_len.group(1))
                unit = m_len.group(2).lower()
                current_cp.tested_bytes = int(val * UNIT_MULTIPLIERS.get(unit, 1))
            continue

        if current_cp is not None:
            current_cp.lines.append(line)

    if current_cp is not None:
        checkpoints.append(current_cp)

    for cp in checkpoints:
        cp_text = "".join(cp.lines)

        cp.has_failure = any(p.search(cp_text) for p in FAILURE_PATTERNS)

        explicit_eval_count = 0
        for line in cp.lines:
            if EXPLICIT_TEST_EVAL_PATTERN.search(line):
                explicit_eval_count += 1
            if any(p.search(line) for p in SUSPICIOUS_PATTERNS) and not any(p.search(line) for p in FAILURE_PATTERNS):
                cp.suspicious_count += 1

        for pat in CHECKPOINT_COMPLETE_PATTERNS:
            m_comp = pat.search(cp_text)
            if m_comp:
                cp.is_complete = True
                cnt = int(m_comp.group(1))
                if "no anomalies" in m_comp.group(0).lower():
                    cp.test_count = cnt
                else:
                    cp.test_count = cnt + explicit_eval_count
                break

    return checkpoints


def parse_practrand_output(output: str, target_bytes: int) -> tuple[int, int, bool, int]:
    """解析 PractRand 输出，返回 (max_tested_bytes, test_count, has_failure, suspicious_count)。"""
    has_global_failure = any(p.search(output) for p in FAILURE_PATTERNS)
    checkpoints = parse_checkpoints(output)

    completed_cps = [cp for cp in checkpoints if cp.is_complete]
    if not completed_cps:
        return 0, 0, has_global_failure, 0

    best_cp = max(completed_cps, key=lambda c: c.tested_bytes)
    has_failure = has_global_failure or any(cp.has_failure for cp in checkpoints)
    total_suspicious = best_cp.suspicious_count

    return best_cp.tested_bytes, best_cp.test_count, has_failure, total_suspicious


def classify_result(
    full_output: str,
    target_bytes: int,
    pr_returncode: int | None,
    gen_returncode: int | None = 0,
    timed_out: bool = False,
    timeout_seconds: int = 0,
    length: str = "",
    generator_termination_cause: str = "natural_exit",
    tester_termination_cause: str = "natural_exit",
    gen_cleanup_requested: bool = False,
) -> ClassificationOutput:
    """根据 PractRand 报告、退出码与进程终止原因进行两轴判定。"""
    reason_codes: list[str] = []
    execution_status: str = "ok"

    # 1. 执行轴评定 (execution_status)
    gen_is_normal = (
        gen_returncode in (0, POSIX_SIGPIPE, EXIT_SIGPIPE_SHELL)
        or (gen_cleanup_requested and generator_termination_cause in ("supervisor_cleanup", "cancelled"))
        or (timed_out and generator_termination_cause == "timed_out")
    )

    if gen_returncode is not None and not gen_is_normal:
        execution_status = "failed"
        reason_codes.append("GENERATOR_CRASH" if (gen_returncode < 0 or gen_returncode > 128) else "GENERATOR_NONZERO_EXIT")

    if pr_returncode is not None and pr_returncode != 0:
        execution_status = "failed"
        reason_codes.append("TESTER_CRASH" if (pr_returncode < 0 or pr_returncode > 128) else "TESTER_NONZERO_EXIT")
    elif pr_returncode is None:
        execution_status = "unknown"
        reason_codes.append("TESTER_UNKNOWN_EXIT")

    if timed_out:
        execution_status = "timeout"
        reason_codes.append("TIMEOUT")

    if gen_returncode is None and "GENERATOR_UNKNOWN_EXIT" not in reason_codes:
        if execution_status != "timeout":
            execution_status = "unknown"
        reason_codes.append("GENERATOR_UNKNOWN_EXIT")

    # 2. 统计轴评定 (statistical_status)
    max_tested = 0
    test_count = 0
    has_failure = False
    suspicious_count = 0

    if not full_output.strip():
        statistical_status = "insufficient_evidence"
        reason_codes.append("EMPTY_OUTPUT")
    else:
        has_banner = ("RNG_test using PractRand" in full_output or "PractRand" in full_output)
        max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(full_output, target_bytes)

        if has_failure:
            statistical_status = "failure"
            reason_codes.append("STATISTICAL_FAILURE")
        elif not has_banner:
            statistical_status = "insufficient_evidence"
            reason_codes.append("UNRECOGNIZED_TESTER_BANNER")
        elif max_tested == 0:
            statistical_status = "insufficient_evidence"
            reason_codes.append("ZERO_BYTES_TESTED")
        elif test_count == 0:
            statistical_status = "insufficient_evidence"
            reason_codes.append("ZERO_TEST_RESULTS")
        elif max_tested < target_bytes:
            statistical_status = "insufficient_evidence"
            reason_codes.append("INCOMPLETE_TEST_LENGTH")
        else:
            statistical_status = "pass"

    # 3. 顶层状态与原因决策
    if statistical_status == "failure":
        status = "statistical_failure"
        reason = "PractRand 报告包含 FAIL 或严重异常标记"
        if execution_status in ("failed", "unknown"):
            reason += f" (伴随执行状态异常: {execution_status})"
    elif execution_status in ("failed", "unknown"):
        status = "environment_error"
        if "GENERATOR_CRASH" in reason_codes or "GENERATOR_NONZERO_EXIT" in reason_codes:
            reason = f"生成器进程提前异常退出 (退出码 {gen_returncode})"
        elif "TESTER_CRASH" in reason_codes or "TESTER_NONZERO_EXIT" in reason_codes:
            reason = f"PractRand 异常退出 (退出码 {pr_returncode})"
        elif "TESTER_UNKNOWN_EXIT" in reason_codes:
            reason = "PractRand 退出状态未知 (退出码为 None)"
        elif "GENERATOR_UNKNOWN_EXIT" in reason_codes:
            reason = "生成器退出状态未知 (退出码为 None)"
        else:
            reason = "执行环境异常"
    elif statistical_status == "failure":
        status = "statistical_failure"
        reason = "PractRand 报告包含 FAIL 或严重异常标记"
    elif execution_status == "timeout":
        status = "inconclusive"
        reason = f"单引擎测试超时 ({timeout_seconds}s)"
    elif statistical_status == "insufficient_evidence":
        if "EMPTY_OUTPUT" in reason_codes:
            status = "environment_error"
            reason = "测试器输出为空"
        elif "UNRECOGNIZED_TESTER_BANNER" in reason_codes:
            status = "environment_error"
            reason = "测试器身份不符合预期（未检测到 PractRand banner）"
        elif "ZERO_BYTES_TESTED" in reason_codes:
            status = "environment_error"
            reason = "测试器未产出任何数据量检查点 (0 bytes tested)"
        elif "ZERO_TEST_RESULTS" in reason_codes:
            status = "inconclusive"
            reason = "未解析到有效统计检验结果 (0 test results)"
        elif "INCOMPLETE_TEST_LENGTH" in reason_codes:
            status = "inconclusive"
            reason = f"未达到目标测试量 (已测 {max_tested} 字节，目标 {target_bytes} 字节)"
        else:
            status = "inconclusive"
            reason = "测试结果不充分"
    else:
        # execution_status == "ok" 且 statistical_status == "pass"
        status = "pass"
        msg = f"已完成 {length} 验证并通过全部检验" if length else "验证通过"
        if suspicious_count > 0:
            msg += f" (含 {suspicious_count} 项可疑标记)"
        reason = msg

    return ClassificationOutput(
        status=status,
        reason=reason,
        max_tested_bytes=max_tested,
        test_count=test_count,
        suspicious_count=suspicious_count,
        execution_status=execution_status,
        statistical_status=statistical_status,
        reason_codes=reason_codes,
    )


def compute_overall_exit_code(statuses: list[str] | list[TestResult]) -> int:
    """计算多引擎测试的总体退出码。

    优先级规则：
    1. 空集合不能成为“全部通过”，返回 2 (environment_error)。
    2. statistical_failure (1) > environment_error (2) > inconclusive (3) > pass (0)。
    """
    if not statuses:
        return 2

    overall = 0
    for item in statuses:
        s = item.status if isinstance(item, TestResult) else str(item)
        c = STATUS_EXIT_CODES.get(s, 2)
        if c == 1:
            overall = 1
        elif c == 2 and overall != 1:
            overall = 2
        elif c == 3 and overall == 0:
            overall = 3
    return overall


compute_exit_code = compute_overall_exit_code


# ============================================================================
# 单引擎测试流程
# ============================================================================

def test_engine(
    generator: str | list[str],
    practrand: str | list[str],
    engine: str,
    length: str,
    seed: int = 0x9E3779B97F4A7C15,
    timeout_seconds: int = 14400,
    env: dict[str, str] | None = None,
    generator_env: dict[str, str] | None = None,
    tester_env: dict[str, str] | None = None,
) -> TestResult:
    """运行单引擎测试，执行进程隔离、超时回收与状态判定。"""
    LOGS_DIR.mkdir(parents=True, exist_ok=True)
    timestamp = int(time.time() * 1000)
    tester_log_path = LOGS_DIR / f"{engine}_{timestamp}_tester.log"
    gen_log_path = LOGS_DIR / f"{engine}_{timestamp}_generator.log"

    target_bytes = parse_length_to_bytes(length)
    is_64bit = engine in ENGINES_64BIT
    stream_mode = "stdin64" if is_64bit else "stdin32"

    gen_base = [generator] if isinstance(generator, str) else list(generator)
    pr_base = [practrand] if isinstance(practrand, str) else list(practrand)

    gen_cmd = (
        [*gen_base, engine]
        if engine == CSPRNG_ENGINE
        else [*gen_base, engine, str(seed)]
    )
    pr_cmd = [*pr_base, stream_mode, "-tlmin", length, "-tlmax", length, "-te", "1"]

    result = TestResult(
        engine=engine,
        randx_commit=get_git_commit(REPO_ROOT),
        practrand_commit=load_practrand_lock_commit(),
        command=pr_cmd,
        seed="os_entropy" if engine == CSPRNG_ENGINE else seed,
        target_bytes=target_bytes,
        log_file=str(tester_log_path),
        generator_log_file=str(gen_log_path),
    )

    print(f"\n[test] {engine} (length={length}, mode={stream_mode})", file=sys.stderr)
    print(f"  pipeline: {' '.join(gen_cmd)} | {' '.join(pr_cmd)}", file=sys.stderr)

    start_time = time.monotonic()
    deadline = start_time + timeout_seconds

    gen_proc: subprocess.Popen | None = None
    pr_proc: subprocess.Popen | None = None
    gen_log_f = None
    tester_log_f = None
    reader_f = None

    gen_cleanup_requested = False
    gen_term_cause = "natural_exit"
    pr_term_cause = "natural_exit"
    timed_out = False

    effective_gen_env = generator_env if generator_env is not None else env
    effective_tester_env = tester_env if tester_env is not None else env

    try:
        gen_log_f = open(gen_log_path, "w", encoding="utf-8", errors="replace")
        tester_log_f = open(tester_log_path, "w", encoding="utf-8", errors="replace")

        try:
            gen_proc = subprocess.Popen(
                gen_cmd,
                stdout=subprocess.PIPE,
                stderr=gen_log_f,
                env=effective_gen_env,
            )
        except Exception as e:
            result.status = "environment_error"
            result.execution_status = "failed"
            result.statistical_status = "insufficient_evidence"
            result.reason = f"无法启动生成器进程: {e}"
            result.reason_codes = ["PROCESS_START_FAILURE"]
            result.generator = {"returncode": None, "termination_cause": "start_failure", "cleanup_requested": False}
            result.tester = {"returncode": None, "termination_cause": "not_started"}
            return result

        try:
            pr_proc = subprocess.Popen(
                pr_cmd,
                stdin=gen_proc.stdout,
                stdout=tester_log_f,
                stderr=subprocess.STDOUT,
                env=effective_tester_env,
            )
        except Exception as e:
            _terminate_and_kill(gen_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
            result.status = "environment_error"
            result.execution_status = "failed"
            result.statistical_status = "insufficient_evidence"
            result.reason = f"无法启动 PractRand 进程: {e}"
            result.reason_codes = ["PROCESS_START_FAILURE"]
            result.generator = {
                "returncode": gen_proc.returncode if gen_proc is not None else None,
                "termination_cause": "cancelled",
                "cleanup_requested": False,
            }
            result.tester = {"returncode": None, "termination_cause": "start_failure"}
            return result
        finally:
            if gen_proc is not None and gen_proc.stdout is not None:
                gen_proc.stdout.close()

        reader_f = open(tester_log_path, "r", encoding="utf-8", errors="replace")
        gen_early_failed = False
        gen_early_fail_time = 0.0

        while True:
            now = time.monotonic()
            if now >= deadline:
                timed_out = True
                pr_term_cause = "timed_out"
                gen_term_cause = "timed_out"
                break

            new_text = reader_f.read()
            if new_text:
                sys.stderr.write(new_text)
                sys.stderr.flush()

            if pr_proc.poll() is not None:
                pr_term_cause = "natural_exit" if pr_proc.returncode == 0 else "non_zero_exit"
                break

            if gen_proc.poll() is not None and gen_proc.returncode not in (0, POSIX_SIGPIPE, EXIT_SIGPIPE_SHELL):
                if not gen_early_failed:
                    gen_early_failed = True
                    gen_early_fail_time = time.monotonic()
                elif time.monotonic() - gen_early_fail_time > EARLY_FAIL_GRACE_PERIOD_SECONDS:
                    pr_term_cause = "cancelled"
                    break

            time.sleep(POLL_INTERVAL_SECONDS)

        remaining = reader_f.read()
        if remaining:
            sys.stderr.write(remaining)
            sys.stderr.flush()

        if timed_out:
            _terminate_and_kill(pr_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
            _terminate_and_kill(gen_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
        elif pr_proc.poll() is not None and pr_proc.returncode == 0:
            grace_end = time.monotonic() + GRACE_PERIOD_SECONDS
            while time.monotonic() < grace_end:
                if gen_proc.poll() is not None:
                    break
                time.sleep(POLL_INTERVAL_SECONDS)

            if gen_proc.poll() is None:
                gen_cleanup_requested = True
                gen_term_cause = "supervisor_cleanup"
                _terminate_and_kill(gen_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
            else:
                rc = gen_proc.returncode
                if rc in (0, POSIX_SIGPIPE, EXIT_SIGPIPE_SHELL):
                    gen_term_cause = "natural_exit" if rc == 0 else "expected_sigpipe"
                else:
                    gen_term_cause = "unexpected_signal" if rc < 0 else "non_zero_exit"
        else:
            if gen_proc.poll() is None:
                gen_cleanup_requested = True
                gen_term_cause = "cancelled"
            _terminate_and_kill(pr_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
            _terminate_and_kill(gen_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
            if gen_proc.poll() is not None:
                rc = gen_proc.returncode
                if rc in (0, POSIX_SIGPIPE, EXIT_SIGPIPE_SHELL):
                    gen_term_cause = "natural_exit" if rc == 0 else "expected_sigpipe"
                elif not gen_cleanup_requested:
                    gen_term_cause = "unexpected_signal" if rc < 0 else "non_zero_exit"
            if pr_proc.poll() is not None:
                pr_term_cause = "natural_exit" if pr_proc.returncode == 0 else ("unexpected_signal" if pr_proc.returncode < 0 else "non_zero_exit")

    finally:
        if reader_f is not None and not reader_f.closed:
            reader_f.close()
        if tester_log_f is not None and not tester_log_f.closed:
            tester_log_f.close()
        if gen_log_f is not None and not gen_log_f.closed:
            gen_log_f.close()
        if gen_proc is not None and gen_proc.poll() is None:
            _terminate_and_kill(gen_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
        if pr_proc is not None and pr_proc.poll() is None:
            _terminate_and_kill(pr_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)

    result.duration_seconds = round(time.monotonic() - start_time, 2)
    result.gen_returncode = gen_proc.returncode if gen_proc is not None else None
    result.pr_returncode = pr_proc.returncode if pr_proc is not None else None
    result.generator = {
        "returncode": result.gen_returncode,
        "termination_cause": gen_term_cause,
        "cleanup_requested": gen_cleanup_requested,
    }
    result.tester = {
        "returncode": result.pr_returncode,
        "termination_cause": pr_term_cause,
    }

    full_output = ""
    if tester_log_path.exists():
        try:
            full_output = tester_log_path.read_text(encoding="utf-8", errors="replace")
        except Exception:
            pass

    classification = classify_result(
        full_output=full_output,
        target_bytes=target_bytes,
        pr_returncode=result.pr_returncode,
        gen_returncode=result.gen_returncode,
        timed_out=timed_out,
        timeout_seconds=timeout_seconds,
        length=length,
        generator_termination_cause=gen_term_cause,
        tester_termination_cause=pr_term_cause,
        gen_cleanup_requested=gen_cleanup_requested,
    )

    result.status = classification.status
    result.execution_status = classification.execution_status
    result.statistical_status = classification.statistical_status
    result.reason = classification.reason
    result.reason_codes = classification.reason_codes
    result.reported_tested_bytes = classification.reported_tested_bytes
    result.test_count = classification.test_count
    result.suspicious_count = classification.suspicious_count

    return result


# ============================================================================
# 主入口
# ============================================================================

def main() -> int:
    parser = argparse.ArgumentParser(description="RandX PractRand 统计质量验证驱动")
    parser.add_argument(
        "--engine",
        help="只测单个引擎（默认测全部 8 个）",
        choices=[*STATISTICAL_ENGINES, CSPRNG_ENGINE],
    )
    parser.add_argument(
        "--length",
        help="覆盖默认测试长度（如 4GB、512MB、32MB）",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="单个引擎失败时仍继续后续引擎",
    )
    parser.add_argument(
        "--output-json",
        help="保存结构化报告结果到 JSON 文件",
    )
    args = parser.parse_args()

    # 1. 查找 PractRand RNG_test
    practrand = find_practrand()
    if not practrand:
        print(
            f"错误：找不到 PractRand 的 RNG_test 可执行文件。\n"
            f"请先运行 {SCRIPT_DIR}/download_practrand.sh 构建，或将 RNG_test 放入 PATH。",
            file=sys.stderr,
        )
        return 2

    # 2. 编译或更新生成器
    try:
        generator = ensure_generator_built()
    except (FileNotFoundError, RuntimeError) as e:
        print(f"错误：{e}", file=sys.stderr)
        return 2

    # 3. 确定测试引擎列表
    engines = [args.engine] if args.engine else [*STATISTICAL_ENGINES, CSPRNG_ENGINE]
    if not engines:
        print("错误：未指定任何测试引擎。", file=sys.stderr)
        return 2

    # 4. 执行测试
    results: list[TestResult] = []

    for eng in engines:
        length = (
            args.length
            if args.length
            else (CSPRNG_LENGTH if eng == CSPRNG_ENGINE else STATISTICAL_PRNG_LENGTH)
        )
        res = test_engine(generator, practrand, eng, length)
        results.append(res)

        print(
            f"\n[result] {eng}: status={res.status}, tested={res.reported_tested_bytes}/{res.target_bytes} bytes, tests={res.test_count}",
            file=sys.stderr,
        )
        if res.status != "pass":
            print(f"  reason: {res.reason}", file=sys.stderr)

        if res.status != "pass" and not args.keep_going:
            break

    overall_exit_code = compute_overall_exit_code(results)

    # 5. 输出汇总
    print("\n" + "=" * 60, file=sys.stderr)
    print("PractRand 统计质量测试汇总：", file=sys.stderr)
    for r in results:
        print(
            f"  - {r.engine:15s} [{r.status.upper():20s}] {r.reason}",
            file=sys.stderr,
        )

    if args.output_json:
        try:
            atomic_write_json(args.output_json, [asdict(r) for r in results])
            print(f"\n已写入结构化报告: {args.output_json}", file=sys.stderr)
        except Exception as e:
            print(f"写入报告 JSON 失败: {e}", file=sys.stderr)
            return 2

    return overall_exit_code


if __name__ == "__main__":
    sys.exit(main())
