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

NO_ANOMALIES_PATTERN = re.compile(
    r"no\s+anomalies\s+in\s+(\d+)\s+test\s+result\(s\)", re.IGNORECASE
)
OTHER_TEST_RESULTS_PATTERN = re.compile(
    r"and\s+(\d+)\s+other\s+test\s+result\(s\)", re.IGNORECASE
)
EXPLICIT_TEST_NAME_PATTERN = re.compile(
    r"^\s*Test Name:", re.MULTILINE
)
GENERIC_TEST_RESULT_PATTERN = re.compile(
    r"(?:no\s+anomalies\s+in|and)\s+(\d+)\s+(?:other\s+)?test\s+result\(s\)", re.IGNORECASE
)
POWER_OF_TWO_BYTES_PATTERN = re.compile(r"\(2\^(\d+)\s*bytes\)", re.IGNORECASE)
LENGTH_BYTES_PATTERN = re.compile(
    r"length=\s*([\d.]+)\s*(kilobytes|megabytes|gigabytes|terabytes|bytes)",
    re.IGNORECASE,
)

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
PRACTRAND_BUILD_DIR = SCRIPT_DIR / "PractRand_build"
PRACTRAND_LOCK_FILE = SCRIPT_DIR / "practrand.lock"
LOGS_DIR = SCRIPT_DIR / "logs"


# ============================================================================
# 数据结构
# ============================================================================

@dataclass
class TestResult:
    schema_version: str = "1.0"
    engine: str = ""
    status: str = ""  # pass, statistical_failure, environment_error, inconclusive
    reason: str = ""
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

    # 依赖检查：源文件或任何核心头文件新于可执行文件时重新编译
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


# ============================================================================
# 报告解析
# ============================================================================

def parse_practrand_output(output: str, target_bytes: int) -> tuple[int, int, bool, int]:
    """解析 PractRand 输出，返回 (max_tested_bytes, test_count, has_failure, suspicious_count)。"""
    # 1. 检查失败模式
    has_failure = any(p.search(output) for p in FAILURE_PATTERNS)

    # 2. 统计可疑项数量
    suspicious_count = sum(len(p.findall(output)) for p in SUSPICIOUS_PATTERNS)

    # 3. 统计测试项总数
    test_count = 0
    for match in NO_ANOMALIES_PATTERN.finditer(output):
        try:
            cnt = int(match.group(1))
            if cnt > test_count:
                test_count = cnt
        except ValueError:
            pass

    for match in OTHER_TEST_RESULTS_PATTERN.finditer(output):
        try:
            cnt = int(match.group(1))
            explicit_count = len(EXPLICIT_TEST_NAME_PATTERN.findall(output))
            total = cnt + explicit_count
            if total > test_count:
                test_count = total
        except ValueError:
            pass

    for match in GENERIC_TEST_RESULT_PATTERN.finditer(output):
        try:
            cnt = int(match.group(1))
            if cnt > test_count:
                test_count = cnt
        except ValueError:
            pass

    explicit_count = len(EXPLICIT_TEST_NAME_PATTERN.findall(output))
    if explicit_count > test_count:
        test_count = explicit_count

    # 4. 统计已测试字节数
    max_tested_bytes = 0
    # 匹配 length= X (2^Y bytes)
    for match in POWER_OF_TWO_BYTES_PATTERN.finditer(output):
        try:
            exp = int(match.group(1))
            bytes_val = 1 << exp
            if bytes_val > max_tested_bytes:
                max_tested_bytes = bytes_val
        except ValueError:
            pass

    unit_multipliers = {
        "bytes": 1,
        "kilobytes": 1024,
        "megabytes": 1024**2,
        "gigabytes": 1024**3,
        "terabytes": 1024**4,
    }
    for match in LENGTH_BYTES_PATTERN.finditer(output):
        try:
            val = float(match.group(1))
            unit = match.group(2).lower()
            bytes_val = int(val * unit_multipliers.get(unit, 1))
            if bytes_val > max_tested_bytes:
                max_tested_bytes = bytes_val
        except ValueError:
            pass

    return max_tested_bytes, test_count, has_failure, suspicious_count


def classify_result(
    full_output: str,
    target_bytes: int,
    pr_returncode: int | None,
    gen_returncode: int | None = 0,
    timed_out: bool = False,
    timeout_seconds: int = 0,
    length: str = "",
) -> tuple[str, str, int, int, int]:
    """根据 PractRand 报告与进程退出码判定测试状态。

    返回 (status, reason, max_tested_bytes, test_count, suspicious_count)。
    """
    if timed_out:
        return "inconclusive", f"单引擎测试超时 ({timeout_seconds}s)", 0, 0, 0

    if not full_output.strip():
        return "environment_error", "测试器输出为空", 0, 0, 0

    if "RNG_test using PractRand" not in full_output and "PractRand" not in full_output:
        return "environment_error", "测试器身份不符合预期（未检测到 PractRand banner）", 0, 0, 0

    max_tested, test_count, has_failure, suspicious_count = parse_practrand_output(
        full_output, target_bytes
    )

    if has_failure:
        return (
            "statistical_failure",
            "PractRand 报告包含 FAIL 或严重异常标记",
            max_tested,
            test_count,
            suspicious_count,
        )

    if max_tested == 0:
        return (
            "environment_error",
            "测试器未产出任何数据量检查点 (0 bytes tested)",
            0,
            test_count,
            suspicious_count,
        )

    if test_count == 0:
        return (
            "inconclusive",
            "未解析到有效统计检验结果 (0 test results)",
            max_tested,
            0,
            suspicious_count,
        )

    is_sigpipe = (
        gen_returncode == -13  # -SIGPIPE on Unix
        or gen_returncode == 141  # 128 + 13
    )
    if max_tested < target_bytes:
        if gen_returncode not in (0, None) and not is_sigpipe:
            return (
                "environment_error",
                f"生成器进程提前异常退出 (退出码 {gen_returncode})",
                max_tested,
                test_count,
                suspicious_count,
            )
        return (
            "inconclusive",
            f"未达到目标测试量 (已测 {max_tested} 字节，目标 {target_bytes} 字节)",
            max_tested,
            test_count,
            suspicious_count,
        )

    if pr_returncode not in (0, None):
        return (
            "environment_error",
            f"PractRand 异常退出 (退出码 {pr_returncode})",
            max_tested,
            test_count,
            suspicious_count,
        )

    msg = f"已完成 {length} 验证并通过全部检验" if length else "验证通过"
    if suspicious_count > 0:
        msg += f" (含 {suspicious_count} 项可疑标记)"
    return "pass", msg, max_tested, test_count, suspicious_count


# ============================================================================
# 单引擎测试流程
# ============================================================================

STATUS_EXIT_CODES = {
    "pass": 0,
    "statistical_failure": 1,
    "environment_error": 2,
    "inconclusive": 3,
}


def compute_overall_exit_code(statuses: list[str] | list[TestResult]) -> int:
    """计算多引擎测试的总体退出码。

    优先级规则：statistical_failure (1) > environment_error (2) > inconclusive (3) > pass (0)。
    """
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


def test_engine(
    generator: str,
    practrand: str,
    engine: str,
    length: str,
    seed: int = 0x9E3779B97F4A7C15,
    timeout_seconds: int = 14400,
) -> TestResult:
    """运行单引擎测试，执行进程隔离、超时回收与状态判定。"""
    LOGS_DIR.mkdir(parents=True, exist_ok=True)
    log_file_path = LOGS_DIR / f"{engine}_{int(time.time())}.log"

    target_bytes = parse_length_to_bytes(length)
    is_64bit = engine in ENGINES_64BIT
    stream_mode = "stdin64" if is_64bit else "stdin32"

    gen_cmd = (
        [generator, engine]
        if engine == CSPRNG_ENGINE
        else [generator, engine, str(seed)]
    )
    pr_cmd = [practrand, stream_mode, "-tlmin", length, "-tlmax", length, "-te", "1"]

    result = TestResult(
        engine=engine,
        randx_commit=get_git_commit(REPO_ROOT),
        practrand_commit=load_practrand_lock_commit(),
        command=pr_cmd,
        seed="os_entropy" if engine == CSPRNG_ENGINE else seed,
        target_bytes=target_bytes,
        log_file=str(log_file_path),
    )

    print(f"\n[test] {engine} (length={length}, mode={stream_mode})", file=sys.stderr)
    print(f"  pipeline: {' '.join(gen_cmd)} | {' '.join(pr_cmd)}", file=sys.stderr)

    start_time = time.monotonic()
    output_lines: list[str] = []

    gen_proc = None
    pr_proc = None
    try:
        try:
            gen_proc = subprocess.Popen(
                gen_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
        except Exception as e:
            result.status = "environment_error"
            result.reason = f"无法启动生成器进程: {e}"
            return result

        try:
            pr_proc = subprocess.Popen(
                pr_cmd,
                stdin=gen_proc.stdout,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        except Exception as e:
            result.status = "environment_error"
            result.reason = f"无法启动 PractRand 进程: {e}"
            return result
        finally:
            if gen_proc.stdout is not None:
                gen_proc.stdout.close()

        timed_out = False
        with open(log_file_path, "w", encoding="utf-8", errors="replace") as log_f:
            try:
                assert pr_proc.stdout is not None
                for raw_line in pr_proc.stdout:
                    line_str = raw_line.decode("utf-8", errors="replace")
                    output_lines.append(line_str)
                    log_f.write(line_str)
                    log_f.flush()
                    sys.stderr.write(line_str)
                    sys.stderr.flush()

                    if time.monotonic() - start_time > timeout_seconds:
                        timed_out = True
                        break

                pr_proc.wait(timeout=10 if not timed_out else 1)
            except subprocess.TimeoutExpired:
                pr_proc.kill()
                timed_out = True
            except Exception as e:
                pr_proc.kill()
                output_lines.append(f"\n[error] 读取输出异常: {e}\n")
    finally:
        if pr_proc is not None:
            if pr_proc.stdout is not None:
                pr_proc.stdout.close()
            if pr_proc.poll() is None:
                pr_proc.kill()
                pr_proc.wait()

        if gen_proc is not None:
            if gen_proc.stdout is not None:
                gen_proc.stdout.close()
            if gen_proc.poll() is None:
                gen_proc.kill()
                gen_proc.wait()

    result.duration_seconds = round(time.monotonic() - start_time, 2)
    result.gen_returncode = gen_proc.returncode if gen_proc is not None else None
    result.pr_returncode = pr_proc.returncode if pr_proc is not None else None

    full_output = "".join(output_lines)

    status, reason, max_tested, test_count, suspicious_count = classify_result(
        full_output=full_output,
        target_bytes=target_bytes,
        pr_returncode=result.pr_returncode,
        gen_returncode=result.gen_returncode,
        timed_out=timed_out,
        timeout_seconds=timeout_seconds,
        length=length,
    )
    result.status = status
    result.reason = reason
    result.reported_tested_bytes = max_tested
    result.test_count = test_count
    result.suspicious_count = suspicious_count
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
            with open(args.output_json, "w", encoding="utf-8") as f:
                json.dump([asdict(r) for r in results], f, indent=2, ensure_ascii=False)
            print(f"\n已写入结构化报告: {args.output_json}", file=sys.stderr)
        except Exception as e:
            print(f"写入报告 JSON 失败: {e}", file=sys.stderr)

    return overall_exit_code


if __name__ == "__main__":
    sys.exit(main())
