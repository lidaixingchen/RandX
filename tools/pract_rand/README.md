# PractRand 统计质量验证工具

本目录提供 RandX 库所有引擎（7 统计 PRNG + 1 CSPRNG = 8 个）的 PractRand 统计质量自动化验证工具。

## 目录结构

```
tools/pract_rand/
├── gen_practrand_stream.cpp   引擎原始字节流生成器（C++）
├── run_practrand.py           测试驱动（Python3）
├── download_practrand.sh      PractRand 下载与构建脚本（Bash）
└── README.md                  本文件
```

## 快速开始

### 1. 构建 PractRand（一次性）

```bash
cd tools/pract_rand
bash download_practrand.sh
# 产物：./PractRand_build/RNG_output
```

环境要求：g++、make、git。CI 中由 nightly workflow 自动完成。

### 2. 运行测试

```bash
# 默认：测全部 8 引擎（统计 PRNG 各 1TB，ChaCha20 256GB）
python3 run_practrand.py

# 只测单个引擎
python3 run_practrand.py --engine sfc64

# 自定义测试长度（本地快速验证）
python3 run_practrand.py --length 512MB

# 失败仍继续（CI 调试用）
python3 run_practrand.py --keep-going
```

测试驱动会自动编译 `gen_practrand_stream.cpp`（若未编译），并按平台自动添加链接参数：
- Windows：`-lbcrypt`
- macOS：`-framework Security`
- Linux：无（getrandom 在 libc 内置）

## 测试长度说明

| 引擎类型 | 默认长度 | 理由 |
|----------|----------|------|
| 统计 PRNG (7 个) | 1 TB | 周期 ≥ 2^64，1TB = 2^40 字节覆盖足够暴露任何统计缺陷 |
| ChaCha20 (CSPRNG) | 256 GB | 无周期概念，2^38 字节足以暴露算法实现偏差 |

> 注：1TB 测试在 4 GHz CPU + 内存充足时约需 1–2 小时；256GB 约 30 分钟。本地开发可用 `--length 4GB` 快速验证。

## 引擎列表

| 引擎名（CLI 参数） | 类型号 | 输出位宽 |
|--------------------|--------|----------|
| `xoshiro256`       | 统计 | 64-bit |
| `xoroshiro128`     | 统计 | 64-bit |
| `xoshiro128`       | 统计 | 32-bit |
| `xoroshiro64`      | 统计 | 32-bit |
| `splitmix64`       | 统计 | 64-bit |
| `sfc64`            | 统计 | 64-bit |
| `romuduojr`        | 统计 | 64-bit |
| `chacha20`         | CSPRNG | 64-bit |

引擎名不区分大小写，可带或不带 `**` 后缀（如 `xoshiro256` 等价 `xoshiro256**`）。

## 失败判定

PractRand 输出中包含以下任一关键字即判失败：
- `FAIL`（任何大小写）
- `!!`（行首的严重警告）

通过则退出码 0；失败则退出码 1；环境错误（无 PractRand / 编译失败）退出码 2。

## CI 集成

由 `.github/workflows/practrand-nightly.yml` 触发 nightly 调度，非 push 触发（PractRand 耗时长）。失败时自动创建 GitHub issue 通知维护者。

## 手动调用流程（不用 Python 驱动）

```bash
# 编译生成器
g++ -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -I ../..
    -o gen_practrand_stream gen_practrand_stream.cpp
# （Windows 加 -lbcrypt；macOS 加 -framework Security）

# 管道到 PractRand
./gen_practrand_stream sfc64 | ./PractRand_build/RNG_output -stdin -tl 4GB -te 1
```
