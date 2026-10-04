"""保存并比较同工具链双标准三角分布观测。"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp17", required=True)
    parser.add_argument("--cpp23", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()
    output = Path(args.output_dir)
    output.mkdir(parents=True, exist_ok=True)
    observations: list[bytes] = []
    for standard in ("cpp17", "cpp23"):
        result = subprocess.run([getattr(args, standard), "--triangular-only"], capture_output=True, check=False)
        (output / f"{standard}.txt").write_bytes(result.stdout)
        (output / f"{standard}.stderr.txt").write_bytes(result.stderr)
        if result.returncode != 0:
            print(f"{standard} 观测进程失败：{result.returncode}")
            return 1
        observations.append(result.stdout)
    if not observations[0] or observations[0] != observations[1]:
        print("双标准三角分布观测不一致或为空")
        return 1
    print(f"双标准三角分布观测逐字节一致：{len(observations[0])} 字节")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
