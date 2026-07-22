# 变更记录

本文件记录 RandX 的版本演进。完整版本记录见此文件；README 仅保留当前特性概览。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/)。

## v1.3.0 - 2026-07-22

- **新增 ChaCha20 CSPRNG 引擎**（RFC 8439）：首个密码学安全引擎，OS 熵自动播种，2^20 字节自动 reseed 提供前向安全；counter 回绕前必然 reseed 杜绝 keystream 复用。提供三种构造方式（OS 熵默认 / 64-bit 种子测试复现 / 显式 key+nonce+counter KAT）。不提供 `serialize`/`jump`（状态导出违背 CSPRNG 安全模型）。
- **新增跨平台 OS 熵源 API**：`SecureRandomBytes(buf, n)`、`SecureSeed()`、`IsOsCryptoEntropyAvailable()`。优先级链 BCryptGenRandom (Windows) / getrandom (Linux) / SecRandomCopyBytes (macOS) → `std::random_device` 兜底（后者非密码学安全，通过 `IsOsCryptoEntropyAvailable()` 暴露）。
- **安全声明**：README 顶部与头文件注释新增"非 CSPRNG"警告；新增"安全使用指南"小节，给出 7 种场景的引擎选型推荐。
- **测试**：新增 RFC 8439 §2.3.2 官方 KAT 测试向量、SecureRandomBytes/SecureSeed/IsOsCryptoEntropyAvailable 测试、ChaCha20 字节缓存与 reseed 测试。
- **构建**：CMakeLists.txt 添加 bcrypt (Windows) / Security (macOS) INTERFACE 链接；conanfile.py / vcpkg.json 版本升至 1.3.0，topics 添加 `chacha20`/`csprng`。
- **修复**：MinGW 下 `<bcrypt.h>` 依赖 `<windows.h>` 的包含顺序问题；ChaCha20 quarter-round 旋转量按 RFC 8439 §2.1 规定为 16/12/8/7。

## v1.2.1 - 2026-07-22

- **项目更名为 RandX**：命名空间由 `xoshiro` 改为 `RandX`，头文件由 `Random.hpp`/`XoshiroCpp.hpp` 重命名为 `RandX.hpp`/`RandX_Cpp17.hpp`，同步更新相关文档与配置。
- **精简引擎体系**：由 14 引擎精简为常用 7 款（xoshiro/xoroshiro 系列 + SplitMix64 + SFC64 + RomuDuoJr），移除低使用率的变体引擎。
- **新增 `CharSet::Base64UrlSafe`**：RFC 4648 §5 URL-safe 变体，字母表 `[A-Za-z0-9-_]`（`+` → `-`，`/` → `_`），适用于 URL/文件名安全的随机 token 生成。
- **`RandSample` 切换阈值修订**：hash-set 与索引数组分支的切换点由 `n² < size` 改为 `n·64 < size`（具名常量 `detail::HashSetThresholdK = 64`）。原阈值在大容器小样本场景（n ∈ [√N, N/127]）导致 3–25× 性能损失与 80 MB 不必要内存占用；新阈值基于线性交叉点实测（n ≈ N/127），K=64 留 2× 裕度。API 与输出分布不变，纯性能改进。

## v1.2.0 - 2026-07-21

- **新增 11 种标准统计分布**：二项、对数正态、几何、柯西、韦布尔、极值、卡方、学生 t、Fisher F、Beta 分布，以及 `RandBernoulli` 别名封装。
- **新增预设字符集枚举**：`RandChar`/`RandString` 支持预设字符集接口（Alphanumeric / Alpha / Lower / Upper / Digit / Hex / Printable / Base64）。
- **新增迭代器版 `RandSample`**：支持随机访问/输入迭代器双路径优化（随机访问 O(1)，输入迭代器 reservoir sampling O(n) 内存）。
- **新增 C++23 ranges 风格 API**：`ranges::RandElement`/`RandSample`/`RandShuffle`/`RandFill`，支持与 STL 范围视图无缝结合（仅 RandX.hpp）。
- **优化 `RandString` 接口**：改用 `string_view` 减少拷贝。
- **构建**：更新 CMake 与 Conan 版本号至 1.2.0。

## v1.1.0 - 2026-07-21

- **新增 `RandChar` 字符类型安全随机 API**：支持 `char`/`wchar_t`/`char16_t`/`char32_t`（C++20+ 含 `char8_t`）。
- **新增 `RandElement` 迭代器版本**：支持非随机访问容器（输入迭代器用 reservoir sampling）。
- **新增 `RandFill` 容器填充与 `RandVector` 快速生成 vector 工具 API**。
- **流式序列化**：为支持的引擎添加 `operator<<`/`operator>>`。
- **测试与 CI**：完善测试与基准测试流程，新增 C++20 编译支持与覆盖率检查（lcov，阈值 80%）。
- **构建**：更新文档与构建配置，添加覆盖率编译选项。
- **重构**：统一代码重构与代码风格优化。

## v1.0.0 - 2026-07-21

首个正式发布版本。基于 [Xoshiro-cpp](https://github.com/Reputeless/Xoshiro-cpp)（Ryo Suzuki，MIT）升级重构，确立双头文件 + 便捷 API + 包管理 的整体架构。

- **双头文件策略**：`Random.hpp`（C++23，concepts 约束）+ `XoshiroCpp.hpp`（C++17，SFINAE 约束），两版本算法实现与输出序列完全一致。
- **14 引擎全覆盖**：xoshiro/xoroshiro 全系列（含 xoroshiro64*/**）+ SplitMix64 + **新增 SFC64**（PractRand 通过）+ **新增 RomuDuoJr**（极简极快）。每个引擎满足 `std::uniform_random_bit_generator` 概念，新增 `discard(n)`。
- **便捷 API 层**：`DefaultEngine()` 线程局部默认引擎、`RandomSeed()` 硬件种子、`RandInt`/`RandReal`/`RandBool`/`RandElement` 基础生成、`RandNormal`/`RandShuffle`/`RandWeighted` 分布与容器操作、`RandSample`/`RandPermutation`/`RandString`/`RandExp`/`RandPoisson`/`RandGamma`/`RandBits<N>`/`RandUUID` 扩展 API。
- **编译期随机**：`RandIntCE` 采用 Lemire 有界法消除模偏差（GCC/Clang 用 `__uint128_t`，MSVC 回退拒绝采样）；`ShuffleCE`/`ShuffledArray` 编译期 Fisher-Yates 洗牌。
- **多流并行**：`MakeStreamEngine<Engine>(streamId, seed)` 利用 `jump()` 创建非重叠子序列。
- **基础设施**：全零吸收态防御（`assert`）、`std::seed_seq` 播种支持（全部引擎）、`Reseed`/`ReseedRandom` 重置默认引擎、`RandomSeed()` 优先使用 RDRAND（x86_64）。
- **序列化**：`serialize()`/`deserialize()` 状态持久化。
- **构建与分发**：CMakeLists.txt + cmake config（`find_package` / `FetchContent`）、vcpkg.json + ports/ 自定义注册表、conanfile.py（header-only）。
- **CI**：GitHub Actions 三编译器矩阵（GCC 14 / Clang 18 / MSVC × C++17/23）。
- **测试**：确定性单元测试（doctest），C++23 测试 26 项、C++17 测试 24 项；卡方统计自检；`benchmark.cpp` 吞吐量/jump/RandInt 基准测试。
