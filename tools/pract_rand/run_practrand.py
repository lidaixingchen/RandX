#!/usr/bin/env python3
"""RandX PractRand 统计质量验证驱动.

用法:
    python3 run_practrand.py                    # 使用策略中的默认 profile 验证全部 8 引擎
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
import signal
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Callable

# ============================================================================
# 常量与配置
# ============================================================================

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
CHECKPOINT_LINE_PATTERN = re.compile(r"\blength=\s*", re.IGNORECASE)
LENGTH_BYTES_PATTERN = re.compile(
    r"\blength=\s*([\d.]+)\s*(kilobytes?|megabytes?|gigabytes?|terabytes?|bytes?)",
    re.IGNORECASE,
)
EXPLICIT_TEST_EVAL_PATTERN = re.compile(
    r"(?:Test Name:|\.\.\.\s*(?:unusual|mildly suspicious|suspicious|very suspicious|FAIL))",
    re.IGNORECASE,
)

UNIT_MULTIPLIERS = {
    "byte": 1,
    "bytes": 1,
    "kilobyte": 1024,
    "kilobytes": 1024,
    "megabyte": 1024**2,
    "megabytes": 1024**2,
    "gigabyte": 1024**3,
    "gigabytes": 1024**3,
    "terabyte": 1024**4,
    "terabytes": 1024**4,
}

POLL_INTERVAL_SECONDS = 0.05
GRACE_PERIOD_SECONDS = 1.0
SUBPROCESS_CLEANUP_TIMEOUT_SECONDS = 1.0
EARLY_FAIL_GRACE_PERIOD_SECONDS = 0.5
POSIX_SIGTERM = -15
POSIX_SIGKILL = -9
POSIX_SIGPIPE = -13
EXIT_SIGPIPE_SHELL = 141
WINDOWS_TERMINATE_PROCESS_EXIT_CODE = 1
POSIX_SIGNAL_OFFSET = 128
DEFAULT_TEST_TIMEOUT_SECONDS = 14400
DEFAULT_PRNG_SEED = 0x9E3779B97F4A7C15
CANCELLATION_REQUESTED = False

SUPERVISOR_ACCEPTABLE_EXIT_CODES = {0, POSIX_SIGPIPE, EXIT_SIGPIPE_SHELL, POSIX_SIGTERM, POSIX_SIGKILL}
if os.name == "nt":
    SUPERVISOR_ACCEPTABLE_EXIT_CODES.add(WINDOWS_TERMINATE_PROCESS_EXIT_CODE)

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
PRACTRAND_POLICY_FILE = SCRIPT_DIR / "practrand_policy.json"
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
    start_line: int = 0
    lines: list[str] = field(default_factory=list)
    suspicious_markers: list[dict[str, str | int]] = field(default_factory=list)


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
    profile: str = ""
    profile_acceptance_status: str = ""
    input_width_bits: int = 0
    seed_strategy: str = ""
    seed_value: int | None = None
    checkpoint_min_bytes: int = 0
    timeout_seconds: int = 0
    test_parameters: list[str] = field(default_factory=list)
    phase: str = "final"
    checkpoints: list[dict[str, object]] = field(default_factory=list)
    run_suspicious_count: int = 0
    suspicious_markers: list[dict[str, str | int]] = field(default_factory=list)


# ============================================================================
# 辅助函数
# ============================================================================

def parse_length_to_bytes(length_str: str) -> int:
    """将人类可读长度（如 4GB, 512MB, 1TB, 1024B）转换为字节数。

    必须显式指定单位（B, KB, MB, GB, TB），禁止裸数字以避免与 PractRand 内部的指数规则发生歧义。
    """
    s = length_str.strip().upper()
    multipliers = {
        "TB": 1024**4,
        "GB": 1024**3,
        "MB": 1024**2,
        "KB": 1024,
        "T": 1024**4,
        "G": 1024**3,
        "M": 1024**2,
        "K": 1024,
        "B": 1,
    }
    for unit, mult in multipliers.items():
        if s.endswith(unit):
            num_part = s[:-len(unit)].strip()
            if not num_part:
                raise ValueError(f"无效的长度格式: '{length_str}'（缺少数值部分）")
            val = float(num_part)
            if val <= 0:
                raise ValueError(f"长度必须为正数: '{length_str}'")
            return int(val * mult)

    raise ValueError(
        f"长度 '{length_str}' 格式无效或缺少单位。"
        "为消除字节数与 PractRand 二进制指数的歧义，必须显式指定单位（例如 '32MB', '4GB', '1TB', '1024B'）。"
    )


def format_bytes_for_practrand(target_bytes: int) -> str:
    """将目标字节数转换为 PractRand -tlmin / -tlmax 接受的无歧义参数。

    优先使用大单位整除形式（如 4G, 512M, 64K），若非 1024 整数倍则以 B 为后缀。
    """
    if target_bytes <= 0:
        raise ValueError(f"目标字节数必须为正数: {target_bytes}")
    if target_bytes % (1024**4) == 0:
        return f"{target_bytes // (1024**4)}T"
    if target_bytes % (1024**3) == 0:
        return f"{target_bytes // (1024**3)}G"
    if target_bytes % (1024**2) == 0:
        return f"{target_bytes // (1024**2)}M"
    if target_bytes % 1024 == 0:
        return f"{target_bytes // 1024}K"
    return f"{target_bytes}B"


def load_practrand_policy(policy_path: Path | str = PRACTRAND_POLICY_FILE) -> dict[str, object]:
    """读取并校验策略文件中的 profile、引擎和 runner 预算."""
    path: Path = Path(policy_path)
    with path.open("r", encoding="utf-8") as policy_file:
        policy: dict[str, object] = json.load(policy_file)

    profiles: object = policy.get("profiles")
    engines: object = policy.get("engines")
    runner_budget: object = policy.get("runner_budget")
    if not isinstance(profiles, dict) or not profiles:
        raise ValueError("策略必须包含至少一个 profile")
    if not isinstance(engines, list) or not engines:
        raise ValueError("策略必须包含至少一个引擎")
    if not isinstance(runner_budget, dict):
        raise ValueError("策略必须包含 runner_budget")

    engine_names: list[str] = []
    for engine in engines:
        if not isinstance(engine, dict):
            raise ValueError("引擎策略必须为对象")
        name: object = engine.get("name")
        width: object = engine.get("input_width_bits")
        seed_strategy: object = engine.get("seed_strategy")
        if not isinstance(name, str) or width not in (32, 64):
            raise ValueError("引擎策略缺少有效名称或输入位宽")
        if seed_strategy not in ("fixed", "os_entropy"):
            raise ValueError(f"引擎 {name} 的 seed_strategy 无效")
        engine_names.append(name)
    if len(engine_names) != len(set(engine_names)):
        raise ValueError("策略中的引擎名称不能重复")

    default_profile: object = policy.get("default_profile")
    if not isinstance(default_profile, str) or default_profile not in profiles:
        raise ValueError("default_profile 必须引用已定义的 profile")

    for profile_name, profile_value in profiles.items():
        if not isinstance(profile_name, str) or not isinstance(profile_value, dict):
            raise ValueError("profile 配置必须为对象")
        target_lengths: object = profile_value.get("target_lengths")
        checkpoint_min: object = profile_value.get("checkpoint_min")
        timeout: object = profile_value.get("timeout_seconds")
        acceptance_status: object = profile_value.get("acceptance_status")
        if not isinstance(target_lengths, dict) or not isinstance(checkpoint_min, str):
            raise ValueError(f"profile {profile_name} 缺少长度或起始检查点配置")
        if not isinstance(timeout, int) or timeout <= 0:
            raise ValueError(f"profile {profile_name} 的 timeout_seconds 必须为正整数")
        if not isinstance(acceptance_status, str) or not acceptance_status:
            raise ValueError(f"profile {profile_name} 必须记录 acceptance_status")
        parse_length_to_bytes(checkpoint_min)
        for target_length in target_lengths.values():
            if not isinstance(target_length, str):
                raise ValueError(f"profile {profile_name} 的目标长度必须是字符串")
            parse_length_to_bytes(target_length)

    return policy


def build_practrand_plan(
    policy: dict[str, object],
    profile_name: str,
    engine_name: str | None = None,
    length_override: str | None = None,
    checkpoint_min_override: str | None = None,
    timeout_override: int | None = None,
    randx_commit: str = "",
    practrand_commit: str = "",
) -> dict[str, object]:
    """将 profile 和显式覆盖冻结为完整的逐引擎计划."""
    profiles: dict[str, dict[str, object]] = policy["profiles"]  # type: ignore[assignment]
    profile: dict[str, object] | None = profiles.get(profile_name)
    if profile is None:
        raise ValueError(f"未知 profile: {profile_name}")

    engine_catalog: list[dict[str, object]] = policy["engines"]  # type: ignore[assignment]
    if engine_name is None:
        selected_engines: list[dict[str, object]] = engine_catalog
    else:
        selected_engines = [engine for engine in engine_catalog if engine["name"] == engine_name]
        if not selected_engines:
            raise ValueError(f"未知引擎: {engine_name}")

    runner_budget: dict[str, int] = policy["runner_budget"]  # type: ignore[assignment]
    available_seconds: int = (
        runner_budget["job_timeout_minutes"] - runner_budget["reserved_minutes"]
    ) * 60
    timeout_seconds: int = timeout_override if timeout_override is not None else int(profile["timeout_seconds"])
    if timeout_seconds <= 0 or timeout_seconds > available_seconds:
        raise ValueError(
            f"单引擎超时 {timeout_seconds}s 必须大于 0 且不超过预留后的 runner 预算 {available_seconds}s"
        )

    checkpoint_min: str = checkpoint_min_override or str(profile["checkpoint_min"])
    checkpoint_min_bytes: int = parse_length_to_bytes(checkpoint_min)
    target_lengths: dict[str, str] = profile["target_lengths"]  # type: ignore[assignment]
    items: list[dict[str, object]] = []
    for engine in selected_engines:
        name: str = str(engine["name"])
        target_length: str = length_override or target_lengths.get(name, target_lengths.get("default", ""))
        target_bytes: int = parse_length_to_bytes(target_length)
        if checkpoint_min_bytes > target_bytes:
            raise ValueError(f"起始检查点 {checkpoint_min} 大于 {name} 的目标长度 {target_length}")

        width: int = int(engine["input_width_bits"])
        stream_mode: str = f"stdin{width}"
        test_parameters: list[str] = [
            stream_mode,
            "-tlmin",
            format_bytes_for_practrand(checkpoint_min_bytes),
            "-tlmax",
            format_bytes_for_practrand(target_bytes),
            "-te",
            "1",
        ]
        seed_strategy: str = str(engine["seed_strategy"])
        seed_value: int | None = int(policy["fixed_seed"]) if seed_strategy == "fixed" else None
        items.append(
            {
                "engine": name,
                "randx_commit": randx_commit,
                "practrand_commit": practrand_commit,
                "profile": profile_name,
                "profile_acceptance_status": str(profile["acceptance_status"]),
                "input_width_bits": width,
                "seed_strategy": seed_strategy,
                "seed_value": seed_value,
                "checkpoint_min_bytes": checkpoint_min_bytes,
                "target_bytes": target_bytes,
                "timeout_seconds": timeout_seconds,
                "test_parameters": test_parameters,
            }
        )

    return {
        "schema_version": "1.0",
        "profile": profile_name,
        "profile_acceptance_status": str(profile["acceptance_status"]),
        "requested_engine": engine_name or "all",
        "randx_commit": randx_commit,
        "practrand_commit": practrand_commit,
        "runner_budget": dict(runner_budget),
        "items": items,
    }


def validate_plan_item(item: dict[str, object]) -> None:
    """校验冻结计划中的必需身份字段与实际 PractRand 参数."""
    required_fields: tuple[str, ...] = (
        "engine",
        "randx_commit",
        "practrand_commit",
        "profile",
        "profile_acceptance_status",
        "input_width_bits",
        "seed_strategy",
        "seed_value",
        "checkpoint_min_bytes",
        "target_bytes",
        "timeout_seconds",
        "test_parameters",
    )
    missing_fields: list[str] = [name for name in required_fields if name not in item]
    if missing_fields:
        raise ValueError(f"冻结计划缺少必需字段: {', '.join(missing_fields)}")

    engine: str = str(item["engine"])
    if engine not in ENGINES_64BIT | ENGINES_32BIT:
        raise ValueError(f"冻结计划包含未知引擎: {engine}")
    width: int = int(item["input_width_bits"])
    expected_width: int = 64 if engine in ENGINES_64BIT else 32
    if width != expected_width:
        raise ValueError(f"冻结计划的 {engine} 输入位宽与引擎分类不符")
    minimum: int = int(item["checkpoint_min_bytes"])
    target: int = int(item["target_bytes"])
    timeout: int = int(item["timeout_seconds"])
    if minimum <= 0 or target <= 0 or minimum > target or timeout <= 0:
        raise ValueError("冻结计划中的长度或超时无效")
    if item["seed_strategy"] == "fixed" and not isinstance(item["seed_value"], int):
        raise ValueError("固定 seed 策略必须带整数 seed_value")
    if item["seed_strategy"] == "os_entropy" and item["seed_value"] is not None:
        raise ValueError("OS 熵 seed 策略的 seed_value 必须为空")

    expected_parameters: list[str] = [
        f"stdin{width}",
        "-tlmin",
        format_bytes_for_practrand(minimum),
        "-tlmax",
        format_bytes_for_practrand(target),
        "-te",
        "1",
    ]
    if item["test_parameters"] != expected_parameters:
        raise ValueError("冻结计划中的实际测试参数与身份字段不一致")


def read_plan_item(plan_path: Path | str) -> dict[str, object]:
    """从 matrix 传入的 JSON 读取唯一引擎计划."""
    with Path(plan_path).open("r", encoding="utf-8") as plan_file:
        item: dict[str, object] = json.load(plan_file)
    if not isinstance(item, dict):
        raise ValueError("单引擎计划必须是 JSON 对象")
    validate_plan_item(item)
    return item


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


def load_practrand_build_commit(binary: str | list[str]) -> str:
    """读取测试二进制所在目录的构建来源记录。"""
    executable: str = binary[0] if isinstance(binary, list) else binary
    metadata: Path = Path(executable).resolve().parent / "practrand-commit.txt"
    try:
        return metadata.read_text(encoding="utf-8").strip() or "unknown"
    except OSError:
        return "unknown"


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

    for line_number, line in enumerate(output.splitlines(keepends=True), start=1):
        m_len = LENGTH_BYTES_PATTERN.search(line)
        m_pow2 = POWER_OF_TWO_BYTES_PATTERN.search(line)
        is_checkpoint_start = bool(m_len or (CHECKPOINT_LINE_PATTERN.search(line) and m_pow2))
        if is_checkpoint_start:
            if current_cp is not None:
                checkpoints.append(current_cp)
            current_cp = Checkpoint()
            current_cp.start_line = line_number
            current_cp.lines.append(line)

            if m_pow2:
                current_cp.tested_bytes = 1 << int(m_pow2.group(1))
            elif m_len:
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
        for line_offset, line in enumerate(cp.lines):
            line_index = cp.start_line + line_offset
            if EXPLICIT_TEST_EVAL_PATTERN.search(line):
                explicit_eval_count += 1
            if any(p.search(line) for p in SUSPICIOUS_PATTERNS):
                cp.suspicious_markers.append(
                    {
                        "test_name": line.strip(),
                        "line_number": line_index,
                    }
                )
                if not any(p.search(line) for p in FAILURE_PATTERNS):
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


def checkpoint_evidence(output: str, log_file: str) -> tuple[list[dict[str, object]], list[dict[str, str | int]], int]:
    """生成可重复计算的检查点证据和全程可疑标记位置."""
    checkpoints: list[Checkpoint] = parse_checkpoints(output)
    serialized_checkpoints: list[dict[str, object]] = []
    all_markers: list[dict[str, str | int]] = []
    for checkpoint_index, checkpoint in enumerate(checkpoints, start=1):
        markers: list[dict[str, str | int]] = [
            {
                "test_name": str(marker["test_name"]),
                "checkpoint_bytes": checkpoint.tested_bytes,
                "checkpoint_index": checkpoint_index,
                "line_number": int(marker["line_number"]),
                "log_file": log_file,
            }
            for marker in checkpoint.suspicious_markers
        ]
        all_markers.extend(markers)
        serialized_checkpoints.append(
            {
                "checkpoint_index": checkpoint_index,
                "tested_bytes": checkpoint.tested_bytes,
                "completed": checkpoint.is_complete,
                "test_count": checkpoint.test_count,
                "suspicious_count": checkpoint.suspicious_count,
                "has_failure": checkpoint.has_failure,
                "suspicious_markers": markers,
            }
        )
    return serialized_checkpoints, all_markers, len(all_markers)


def update_result_checkpoint_evidence(result: TestResult, output: str) -> None:
    """按最新完整输出快照替换检查点证据，避免重复读取累计计数."""
    checkpoints, markers, run_suspicious_count = checkpoint_evidence(output, result.log_file)
    completed: list[dict[str, object]] = [item for item in checkpoints if item["completed"] is True]
    best_checkpoint: dict[str, object] | None = max(
        completed,
        key=lambda item: int(item["tested_bytes"]),
        default=None,
    )
    result.checkpoints = checkpoints
    result.suspicious_markers = markers
    result.run_suspicious_count = run_suspicious_count
    if best_checkpoint is None:
        result.reported_tested_bytes = 0
        result.test_count = 0
        result.suspicious_count = 0
    else:
        result.reported_tested_bytes = int(best_checkpoint["tested_bytes"])
        result.test_count = int(best_checkpoint["test_count"])
        result.suspicious_count = int(best_checkpoint["suspicious_count"])


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
    cancelled: bool = False,
) -> ClassificationOutput:
    """根据 PractRand 报告、退出码与进程终止原因进行两轴判定。"""
    reason_codes: list[str] = []
    execution_status: str = "ok"

    gen_is_normal = (
        gen_returncode in (0, POSIX_SIGPIPE, EXIT_SIGPIPE_SHELL)
        or (
            gen_cleanup_requested
            and generator_termination_cause in ("supervisor_cleanup", "cancelled")
            and gen_returncode in SUPERVISOR_ACCEPTABLE_EXIT_CODES
        )
        or (
            timed_out
            and generator_termination_cause == "timed_out"
            and gen_returncode in SUPERVISOR_ACCEPTABLE_EXIT_CODES
        )
    )

    if gen_returncode is not None and not gen_is_normal:
        execution_status = "failed"
        reason_codes.append("GENERATOR_CRASH" if (gen_returncode < 0 or gen_returncode > POSIX_SIGNAL_OFFSET) else "GENERATOR_NONZERO_EXIT")

    if pr_returncode is not None and pr_returncode != 0:
        execution_status = "failed"
        reason_codes.append("TESTER_CRASH" if (pr_returncode < 0 or pr_returncode > POSIX_SIGNAL_OFFSET) else "TESTER_NONZERO_EXIT")
    elif pr_returncode is None:
        execution_status = "unknown"
        reason_codes.append("TESTER_UNKNOWN_EXIT")

    if timed_out:
        execution_status = "timeout"
        reason_codes.append("TIMEOUT")
    elif cancelled:
        execution_status = "cancelled"
        reason_codes.append("CANCELLED")

    if gen_returncode is None and "GENERATOR_UNKNOWN_EXIT" not in reason_codes:
        if execution_status not in ("timeout", "cancelled"):
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
    elif execution_status == "timeout":
        status = "inconclusive"
        reason = f"单引擎测试超时 ({timeout_seconds}s)"
    elif execution_status == "cancelled":
        status = "inconclusive"
        reason = "测试运行被取消，已保留可用检查点证据"
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
    seed: int = DEFAULT_PRNG_SEED,
    timeout_seconds: int = DEFAULT_TEST_TIMEOUT_SECONDS,
    env: dict[str, str] | None = None,
    generator_env: dict[str, str] | None = None,
    tester_env: dict[str, str] | None = None,
    checkpoint_min_bytes: int | None = None,
    plan_item: dict[str, object] | None = None,
    on_update: Callable[[TestResult], None] | None = None,
) -> TestResult:
    """运行单引擎测试，执行进程隔离、超时回收与状态判定。"""
    global CANCELLATION_REQUESTED

    if plan_item is not None:
        timeout_seconds = int(plan_item["timeout_seconds"])
    LOGS_DIR.mkdir(parents=True, exist_ok=True)
    timestamp = int(time.time() * 1000)
    tester_log_path = LOGS_DIR / f"{engine}_{timestamp}_tester.log"
    gen_log_path = LOGS_DIR / f"{engine}_{timestamp}_generator.log"

    target_bytes: int = int(plan_item["target_bytes"]) if plan_item is not None else parse_length_to_bytes(length)
    minimum_bytes: int = (
        int(plan_item["checkpoint_min_bytes"])
        if plan_item is not None
        else (checkpoint_min_bytes if checkpoint_min_bytes is not None else target_bytes)
    )
    practrand_len = format_bytes_for_practrand(target_bytes)
    stream_mode = f"stdin{int(plan_item['input_width_bits'])}" if plan_item is not None else (
        "stdin64" if engine in ENGINES_64BIT else "stdin32"
    )

    gen_base = [generator] if isinstance(generator, str) else list(generator)
    pr_base = [practrand] if isinstance(practrand, str) else list(practrand)

    effective_seed: int = int(plan_item["seed_value"]) if plan_item is not None and plan_item["seed_strategy"] == "fixed" else seed
    gen_cmd = (
        [*gen_base, engine]
        if engine == CSPRNG_ENGINE
        else [*gen_base, engine, str(effective_seed)]
    )
    test_parameters: list[str] = list(plan_item["test_parameters"]) if plan_item is not None else [
        stream_mode,
        "-tlmin",
        format_bytes_for_practrand(minimum_bytes),
        "-tlmax",
        practrand_len,
        "-te",
        "1",
    ]
    pr_cmd = [*pr_base, *test_parameters]

    result = TestResult(
        engine=engine,
        status="inconclusive",
        execution_status="unknown",
        statistical_status="insufficient_evidence",
        randx_commit=get_git_commit(REPO_ROOT),
        practrand_commit=load_practrand_build_commit(practrand),
        command=pr_cmd,
        seed="os_entropy" if engine == CSPRNG_ENGINE else effective_seed,
        target_bytes=target_bytes,
        log_file=str(tester_log_path),
        generator_log_file=str(gen_log_path),
        profile=str(plan_item.get("profile", "")) if plan_item is not None else "",
        profile_acceptance_status=str(plan_item.get("profile_acceptance_status", "")) if plan_item is not None else "",
        input_width_bits=int(plan_item["input_width_bits"]) if plan_item is not None else (64 if engine in ENGINES_64BIT else 32),
        seed_strategy=str(plan_item["seed_strategy"]) if plan_item is not None else ("os_entropy" if engine == CSPRNG_ENGINE else "fixed"),
        seed_value=plan_item.get("seed_value") if plan_item is not None else (None if engine == CSPRNG_ENGINE else seed),
        checkpoint_min_bytes=minimum_bytes,
        timeout_seconds=timeout_seconds,
        test_parameters=test_parameters,
        phase="running",
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
    cancelled = False

    effective_gen_env = generator_env if generator_env is not None else env
    effective_tester_env = tester_env if tester_env is not None else env

    last_checkpoint_snapshot: str = ""

    def update_progress(new_text: str) -> None:
        nonlocal last_checkpoint_snapshot
        if not new_text:
            return
        current_output: str = tester_log_path.read_text(encoding="utf-8", errors="replace")
        update_result_checkpoint_evidence(result, current_output)
        checkpoint_snapshot: str = json.dumps(result.checkpoints, ensure_ascii=False, sort_keys=True)
        if checkpoint_snapshot != last_checkpoint_snapshot:
            last_checkpoint_snapshot = checkpoint_snapshot
            if on_update is not None:
                on_update(result)

    try:
        if on_update is not None:
            on_update(result)
        if CANCELLATION_REQUESTED:
            raise KeyboardInterrupt

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
            result.phase = "final"
            if on_update is not None:
                on_update(result)
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
            result.phase = "final"
            if on_update is not None:
                on_update(result)
            return result
        finally:
            if gen_proc is not None and gen_proc.stdout is not None:
                gen_proc.stdout.close()

        reader_f = open(tester_log_path, "r", encoding="utf-8", errors="replace")
        gen_early_failed = False
        gen_early_fail_time = 0.0

        while True:
            if CANCELLATION_REQUESTED:
                cancelled = True
                pr_term_cause = "cancelled"
                gen_term_cause = "cancelled"
                break

            now = time.monotonic()
            if now >= deadline:
                timed_out = True
                pr_term_cause = "timed_out"
                gen_term_cause = "timed_out"
                break

            new_text = reader_f.read()
            if new_text:
                update_progress(new_text)
                sys.stderr.write(new_text)
                sys.stderr.flush()
                if CANCELLATION_REQUESTED:
                    raise KeyboardInterrupt

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
            update_progress(remaining)
            sys.stderr.write(remaining)
            sys.stderr.flush()

        if CANCELLATION_REQUESTED:
            cancelled = True
            pr_term_cause = "cancelled"
            gen_term_cause = "cancelled"

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
                if gen_proc.poll() is not None:
                    rc = gen_proc.returncode
                    if rc not in SUPERVISOR_ACCEPTABLE_EXIT_CODES:
                        gen_term_cause = "unexpected_signal" if (rc < 0 or rc > POSIX_SIGNAL_OFFSET) else "non_zero_exit"
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
                elif gen_cleanup_requested:
                    if rc not in SUPERVISOR_ACCEPTABLE_EXIT_CODES:
                        gen_term_cause = "unexpected_signal" if (rc < 0 or rc > POSIX_SIGNAL_OFFSET) else "non_zero_exit"
                else:
                    gen_term_cause = "unexpected_signal" if rc < 0 else "non_zero_exit"
            if pr_proc.poll() is not None:
                pr_term_cause = "natural_exit" if pr_proc.returncode == 0 else ("unexpected_signal" if pr_proc.returncode < 0 else "non_zero_exit")

    except KeyboardInterrupt:
        CANCELLATION_REQUESTED = True
        cancelled = True
        pr_term_cause = "cancelled"
        gen_term_cause = "cancelled"
        gen_cleanup_requested = True
        _terminate_and_kill(pr_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
        _terminate_and_kill(gen_proc, timeout=SUBPROCESS_CLEANUP_TIMEOUT_SECONDS)
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

    if CANCELLATION_REQUESTED:
        cancelled = True

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
    update_result_checkpoint_evidence(result, full_output)

    def apply_classification(cancelled_value: bool) -> None:
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
            cancelled=cancelled_value,
        )
        result.status = classification.status
        result.execution_status = classification.execution_status
        result.statistical_status = classification.statistical_status
        result.reason = classification.reason
        result.reason_codes = classification.reason_codes
        result.reported_tested_bytes = classification.reported_tested_bytes
        result.test_count = classification.test_count
        result.suspicious_count = classification.suspicious_count

    apply_classification(cancelled or CANCELLATION_REQUESTED)
    result.phase = "final"
    if on_update is not None:
        on_update(result)
        if CANCELLATION_REQUESTED and result.execution_status != "cancelled":
            apply_classification(True)
            on_update(result)

    return result


# ============================================================================
# 主入口
# ============================================================================

def environment_error_result(item: dict[str, object], message: str) -> TestResult:
    """为未能启动的计划项目保留带完整身份的环境错误报告."""
    result: TestResult = TestResult(
        engine=str(item["engine"]),
        status="environment_error",
        execution_status="failed",
        statistical_status="insufficient_evidence",
        reason=message,
        reason_codes=["PROCESS_START_FAILURE"],
        randx_commit=str(item["randx_commit"]),
        practrand_commit=str(item["practrand_commit"]),
        seed=("os_entropy" if item["seed_strategy"] == "os_entropy" else int(item["seed_value"])),
        target_bytes=int(item["target_bytes"]),
        profile=str(item["profile"]),
        profile_acceptance_status=str(item["profile_acceptance_status"]),
        input_width_bits=int(item["input_width_bits"]),
        seed_strategy=str(item["seed_strategy"]),
        seed_value=item["seed_value"],
        checkpoint_min_bytes=int(item["checkpoint_min_bytes"]),
        timeout_seconds=int(item["timeout_seconds"]),
        test_parameters=list(item["test_parameters"]),
        phase="final",
    )
    return result


def cancelled_before_start_result(item: dict[str, object]) -> TestResult:
    """为启动前收到取消请求的冻结项目保留终态报告."""
    return TestResult(
        engine=str(item["engine"]),
        status="inconclusive",
        execution_status="cancelled",
        statistical_status="insufficient_evidence",
        reason="项目启动前收到取消请求",
        reason_codes=["CANCELLED"],
        randx_commit=str(item["randx_commit"]),
        practrand_commit=str(item["practrand_commit"]),
        seed="os_entropy" if item["seed_strategy"] == "os_entropy" else int(item["seed_value"]),
        target_bytes=int(item["target_bytes"]),
        profile=str(item["profile"]),
        profile_acceptance_status=str(item["profile_acceptance_status"]),
        input_width_bits=int(item["input_width_bits"]),
        seed_strategy=str(item["seed_strategy"]),
        seed_value=item["seed_value"],
        checkpoint_min_bytes=int(item["checkpoint_min_bytes"]),
        timeout_seconds=int(item["timeout_seconds"]),
        test_parameters=list(item["test_parameters"]),
        generator={"returncode": None, "termination_cause": "not_started", "cleanup_requested": False},
        tester={"returncode": None, "termination_cause": "not_started"},
        phase="final",
    )


def mark_result_cancelled(result: TestResult) -> None:
    """将尚未完成的通过结果改为取消终态，同时保留统计证据."""
    if result.status not in ("statistical_failure", "environment_error") and result.execution_status != "cancelled":
        result.status = "inconclusive"
        result.execution_status = "cancelled"
        result.reason = "测试运行被取消，已保留可用检查点证据"
        if "CANCELLED" not in result.reason_codes:
            result.reason_codes.append("CANCELLED")


def persist_result_update(
    results: list[TestResult],
    result: TestResult,
    output_json: Path | str | None = None,
) -> None:
    """替换同一项目的最新状态，并以既有项目数组格式原子保存."""
    for result_index, previous in enumerate(results):
        if previous.engine == result.engine:
            results[result_index] = result
            break
    else:
        results.append(result)
    if output_json is not None:
        atomic_write_json(output_json, [asdict(project) for project in results])


def parse_arguments(arguments: list[str] | None = None) -> argparse.Namespace:
    """解析兼容既有运行参数并支持 profile 与冻结计划入口的命令行."""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description="RandX PractRand 统计质量验证驱动")
    parser.add_argument("--engine", help="只测单个引擎")
    parser.add_argument("--length", help="覆盖 profile 目标长度（如 4GB、512MB、32MB）")
    parser.add_argument("--keep-going", action="store_true", help="单个引擎失败时仍继续后续引擎")
    parser.add_argument("--output-json", help="增量保存项目数组格式的结构化报告")
    parser.add_argument("--profile", help="使用策略中的 profile")
    parser.add_argument("--checkpoint-min", help="覆盖最早的 PractRand 检查点")
    parser.add_argument("--timeout", type=int, help="覆盖单引擎超时秒数")
    parser.add_argument("--plan-output", help="只生成冻结计划 JSON，不启动测试")
    parser.add_argument("--plan-item", help="按冻结的单引擎计划运行")
    parser.add_argument("--github-output", help="向 GitHub Actions 输出矩阵和 profile 验收状态")
    return parser.parse_args(arguments)


def write_github_plan_output(output_path: Path | str, plan: dict[str, object]) -> None:
    """写入 GitHub Actions matrix 和 profile 状态输出."""
    items: list[dict[str, object]] = plan["items"]  # type: ignore[assignment]
    matrix: dict[str, object] = {"include": [{"engine": item["engine"]} for item in items]}
    output_file: Path = Path(output_path)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    with output_file.open("a", encoding="utf-8", newline="\n") as stream:
        stream.write(f"matrix={json.dumps(matrix, ensure_ascii=False, separators=(',', ':'))}\n")
        stream.write(f"acceptance_status={plan['profile_acceptance_status']}\n")
        runner_budget: dict[str, int] = plan["runner_budget"]  # type: ignore[assignment]
        stream.write(f"job_timeout_minutes={runner_budget['job_timeout_minutes']}\n")


def _run_main(arguments: list[str] | None = None) -> int:
    args: argparse.Namespace = parse_arguments(arguments)
    try:
        policy: dict[str, object] = load_practrand_policy()
        if args.plan_item:
            if any((args.engine, args.length, args.profile, args.checkpoint_min, args.timeout is not None, args.plan_output)):
                raise ValueError("--plan-item 运行不能与 profile 或其 CLI 覆盖项并用")
            items: list[dict[str, object]] = [read_plan_item(args.plan_item)]
        else:
            default_profile: str = str(policy["default_profile"])
            profile_name: str = args.profile or default_profile
            randx_commit: str = get_git_commit(REPO_ROOT)
            practrand_commit: str = load_practrand_lock_commit()
            if not randx_commit or randx_commit == "unknown":
                raise ValueError("无法确定 RandX 提交，不能冻结 PractRand 计划")
            if not practrand_commit or practrand_commit == "unknown":
                raise ValueError("无法从 practrand.lock 确定 PractRand 提交")
            plan: dict[str, object] = build_practrand_plan(
                policy=policy,
                profile_name=profile_name,
                engine_name=args.engine,
                length_override=args.length,
                checkpoint_min_override=args.checkpoint_min,
                timeout_override=args.timeout,
                randx_commit=randx_commit,
                practrand_commit=practrand_commit,
            )
            items = plan["items"]  # type: ignore[assignment]
            if args.plan_output:
                atomic_write_json(args.plan_output, plan)
                if args.github_output:
                    write_github_plan_output(args.github_output, plan)
                return 0
            if args.github_output:
                raise ValueError("--github-output 只能与 --plan-output 并用")
    except (OSError, TypeError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"参数或策略错误：{error}", file=sys.stderr)
        return 2

    results: list[TestResult] = []

    def persist_result(result: TestResult) -> None:
        persist_result_update(results, result, args.output_json)

    practrand: str = "" if CANCELLATION_REQUESTED else find_practrand()
    setup_error: str = ""
    if not practrand and not CANCELLATION_REQUESTED:
        setup_error = (
            f"找不到 PractRand 的 RNG_test。请先运行 {SCRIPT_DIR}/download_practrand.sh 构建，"
            "或将 RNG_test 放入 PATH。"
        )
    generator: str = ""
    if not setup_error and not CANCELLATION_REQUESTED:
        try:
            generator = ensure_generator_built()
        except (FileNotFoundError, RuntimeError, OSError) as error:
            setup_error = f"生成器构建失败：{error}"

    for item in items:
        if CANCELLATION_REQUESTED:
            result: TestResult = cancelled_before_start_result(item)
            persist_result(result)
        elif setup_error:
            result: TestResult = environment_error_result(item, setup_error)
            persist_result(result)
        else:
            result = test_engine(
                generator=generator,
                practrand=practrand,
                engine=str(item["engine"]),
                length=format_bytes_for_practrand(int(item["target_bytes"])),
                seed=int(item["seed_value"]) if item["seed_value"] is not None else DEFAULT_PRNG_SEED,
                timeout_seconds=int(item["timeout_seconds"]),
                plan_item=item,
                on_update=persist_result,
            )
            if CANCELLATION_REQUESTED:
                mark_result_cancelled(result)
                persist_result(result)

        print(
            f"\n[result] {result.engine}: status={result.status}, tested={result.reported_tested_bytes}/{result.target_bytes} bytes, tests={result.test_count}",
            file=sys.stderr,
        )
        if result.status != "pass":
            print(f"  reason: {result.reason}", file=sys.stderr)
        if result.execution_status == "cancelled":
            continue
        if result.status != "pass" and not args.keep_going:
            break

    overall_exit_code: int = compute_overall_exit_code(results)
    print("\n" + "=" * 60, file=sys.stderr)
    print("PractRand 统计质量测试汇总：", file=sys.stderr)
    for result in results:
        print(f"  - {result.engine:15s} [{result.status.upper():20s}] {result.reason}", file=sys.stderr)
    return overall_exit_code


def _handle_cancellation(signum: int, frame: object) -> None:
    """记录终止请求，由执行流程在安全边界清理并保存终态证据。"""
    global CANCELLATION_REQUESTED
    CANCELLATION_REQUESTED = True


def main(arguments: list[str] | None = None) -> int:
    global CANCELLATION_REQUESTED
    signals: tuple[int, ...] = (signal.SIGINT, signal.SIGTERM)
    previous_handlers: dict[int, object] = {}
    previous_cancellation_request: bool = CANCELLATION_REQUESTED
    CANCELLATION_REQUESTED = False
    try:
        for signum in signals:
            previous_handlers[signum] = signal.signal(signum, _handle_cancellation)
        return _run_main(arguments)
    finally:
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)
        CANCELLATION_REQUESTED = previous_cancellation_request


if __name__ == "__main__":
    sys.exit(main())
