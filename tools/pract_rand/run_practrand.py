#!/usr/bin/env python3
"""RandX PractRand 统计质量验证驱动.

用法:
    python3 run_practrand.py                    # 默认验证全部 8 引擎（统计 PRNG 1TB / ChaCha20 256GB）
    python3 run_practrand.py --engine sfc64     # 只测一个引擎
    python3 run_practrand.py --length 4GB      # 自定义测试长度
    python3 run_practrand.py --keep-going       # 失败仍继续后续引擎

前置条件:
    1. PractRand 二进制已构建（见 download_practrand.sh），或在 PATH 中存在 RNG_output
    2. gen_practrand_stream.cpp 已编译为 ./gen_practrand_stream

退出码:
    0 = 全部引擎通过
    1 = 至少一个引擎失败
    2 = 环境错误（缺少 PractRand 或编译失败）
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# ============================================================================
# 常量（无魔法数字：均用具名常量）
# ============================================================================

# PractRand 测试长度：统计 PRNG 验证 1TB，CSPRNG 验证 256GB
# 理由：统计 PRNG 周期 >= 2^64，1TB = 2^40 字节覆盖足够；
#       CSPRNG 无周期概念，但 256GB = 2^38 字节足以暴露任何统计偏差
STATISTICAL_PRNG_LENGTH = "1TB"
CSPRNG_LENGTH = "256GB"

# PractRand 报告中的失败关键字（出现任一即判失败）
FAILURE_PATTERNS = (
    re.compile(r"FAIL", re.IGNORECASE),
    re.compile(r"^\s*!!", re.IGNORECASE),
)

# 统计 PRNG 列表（7 个）+ CSPRNG（1 个）
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

# 仓库根目录（脚本所在目录的父目录）
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
PRACTRAND_BUILD_DIR = SCRIPT_DIR / "PractRand_build"


# ============================================================================
# 环境检测
# ============================================================================

def find_practrand() -> str:
    """返回 RNG_output 可执行路径，找不到返回空字符串。"""
    # 1) 本目录下构建的副本
    candidate = PRACTRAND_BUILD_DIR / "RNG_output"
    if candidate.exists():
        return str(candidate)
    # 2) PATH 中
    return shutil.which("RNG_output") or ""


def ensure_generator_built() -> str:
    """确保 gen_practrand_stream 已编译，返回可执行路径。失败抛异常。"""
    exe_path = SCRIPT_DIR / ("gen_practrand_stream.exe" if os.name == "nt" else "gen_practrand_stream")
    if exe_path.exists():
        return str(exe_path)

    src = SCRIPT_DIR / "gen_practrand_stream.cpp"
    if not src.exists():
        raise FileNotFoundError(f"找不到源文件: {src}")

    print(f"[build] 编译 {src.name} ...", file=sys.stderr)
    # 平台特定链接参数
    if os.name == "nt":
        link_libs = ["-lbcrypt"]
    elif sys.platform == "darwin":
        link_libs = ["-framework", "Security"]
    else:
        link_libs = []

    cmd = [
        "g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-unknown-pragmas",
        f"-I{REPO_ROOT}",
        "-o", str(exe_path),
        str(src),
        *link_libs,
    ]
    print(f"[build] {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"编译失败:\n{result.stderr}")
    return str(exe_path)


# ============================================================================
# 单引擎测试
# ============================================================================

def test_engine(
    generator: str,
    practrand: str,
    engine: str,
    length: str,
    seed: int = 0x9E3779B97F4A7C15,
) -> tuple[bool, str]:
    """运行一次测试，返回 (是否通过, 原始输出)。"""
    # ChaCha20 走 OS 熵，不需要 seed；其他引擎传 seed 保证可复现
    gen_cmd = [generator, engine] if engine == CSPRNG_ENGINE else [generator, engine, str(seed)]
    # PractRand 选项：
    #   -tl <len>   测试长度
    #   -te 1       启用扩展测试集（更敏感）
    #   -stdin      从 stdin 读取
    pr_cmd = [practrand, "-stdin", f"-tl", length, "-te", "1"]

    print(f"\n[test] {engine} (length={length})", file=sys.stderr)
    print(f"  cmd: {' '.join(gen_cmd)} | {' '.join(pr_cmd)}", file=sys.stderr)

    gen = subprocess.Popen(gen_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pr = subprocess.Popen(pr_cmd, stdin=gen.stdout, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert gen.stdout is not None
    gen.stdout.close()  # 允许 gen 收到 SIGPIPE 当 pr 退出

    output = ""
    assert pr.stdout is not None
    for line in pr.stdout:
        text = line.decode("utf-8", errors="replace")
        output += text
        # 实时回显进度（PractRand 每 2 的幂次输出一行）
        sys.stderr.write(text)
        sys.stderr.flush()

    pr.wait()
    gen.terminate()

    # 判定失败
    for pattern in FAILURE_PATTERNS:
        if pattern.search(output):
            return False, output
    return True, output


# ============================================================================
# 主流程
# ============================================================================

def main() -> int:
    parser = argparse.ArgumentParser(description="RandX PractRand 统计质量验证")
    parser.add_argument(
        "--engine",
        help="只测单个引擎（默认测全部 8 个）",
        choices=[*STATISTICAL_ENGINES, CSPRNG_ENGINE],
    )
    parser.add_argument(
        "--length",
        help="覆盖默认测试长度（如 4GB、512MB）",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="引擎失败时仍继续后续引擎",
    )
    args = parser.parse_args()

    # 1) 检测 PractRand
    practrand = find_practrand()
    if not practrand:
        print(
            f"错误：找不到 PractRand 的 RNG_output。\n"
            f"请先运行 {SCRIPT_DIR}/download_practrand.sh 构建，或将 RNG_output 放入 PATH。",
            file=sys.stderr,
        )
        return 2

    # 2) 编译生成器
    try:
        generator = ensure_generator_built()
    except (FileNotFoundError, RuntimeError) as e:
        print(f"错误：{e}", file=sys.stderr)
        return 2

    # 3) 选择引擎列表
    if args.engine:
        engines = [args.engine]
    else:
        engines = [*STATISTICAL_ENGINES, CSPRNG_ENGINE]

    # 4) 逐引擎测试
    failures: list[str] = []
    for eng in engines:
        if args.length:
            length = args.length
        elif eng == CSPRNG_ENGINE:
            length = CSPRNG_LENGTH
        else:
            length = STATISTICAL_PRNG_LENGTH

        ok, _ = test_engine(generator, practrand, eng, length)
        status = "PASS" if ok else "FAIL"
        print(f"\n[result] {eng}: {status}", file=sys.stderr)
        if not ok:
            failures.append(eng)
            if not args.keep_going:
                break

    # 5) 汇总
    print("\n" + "=" * 60, file=sys.stderr)
    if failures:
        print(f"失败引擎: {', '.join(failures)}", file=sys.stderr)
        return 1
    print("全部引擎通过 PractRand 验证", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
