# 变更记录

本文件记录 RandX 的版本演进。完整版本记录见此文件；README 仅保留当前特性概览。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/)。

## 未发布

- **CI**：benchmark.yml 基线对比升级为门禁——实际移除 continue-on-error（v1.4.3 记录的移除未落地），容差 15%→25% 以适配共享 runner 噪声，失败自动创建 issue，并增加与 ci.yml 一致的 paths 过滤。

## v1.4.3 - 2026-07-26

- **修复 `Generate64Bits` 双文件位序不一致（P0）**：C++17 版 32 位引擎拼接顺序改为 lo/hi，与 C++23 版一致，消除 `RandUUID` 等函数跨头文件输出差异。
- **修复 `RandWeighted` 全零权重 UB（P0）**：assert 增加 `any_of(w > 0)` 检查，防止 `std::discrete_distribution` 未定义行为。
- **修复 `DefaultEngine()` 播种（P1）**：改用 `RandomSeed()` 回退链（RDRAND → OS API → random_device → 时间戳），替代裸 `std::random_device`。
- **修复 `RandElement` 右值悬垂引用（P1）**：拆分为左值（返回引用）和右值（按值返回）两个重载。
- **安全加固 `SecureRandomBytes`（P1）**：无 OS 密码学 API 时不再静默降级为 `std::random_device`，改为返回 false 触发异常。
- **参数校验补全**：`RandReal` 引擎重载、`RandChar`、10 个分布函数补 `std::isfinite` 校验；`RandIntCE` 增加编译期 `min > max` 契约检查。
- **约束修正**：`RandFill` 整数重载增加 `std::integral<T>` 约束，消除与浮点重载的歧义。
- **CI/文档**：benchmark.yml 移除 continue-on-error；RFC 7539→8439；ChaCha20 状态大小 56B→48B+；ci.yml paths 补 benchmark_gbench.cpp；MinGW 编译提示 `-lbcrypt`。
- **测试**：新增 11 个边界用例（双文件同步）。

## v1.4.2 - 2026-07-25

- **修复 `RandUUID` 高位置零缺陷**：适配 32 位引擎通过拼接两次 32-bit 输出生成 64 位整数，避免直接使用 32 位引擎时 UUID 高位恒为零。
- **修复 `RandBeta` 极值退化**：处理伯努利极值参数下的退化情形，并为全部分布补充 `std::isfinite` 断言防范非有限值。
- **C++17 兼容性补全**：补全 C++17 容器 Type Traits，完善 `RandShuffle`/`RandSample` 模板约束。
- **性能优化**：优化 `RandBool` 伯努利分布实现；新增 `RandWeighted` 高频采样重载。

## v1.4.1 - 2026-07-25

- **CSPRNG 安全加固**：禁用 `ChaCha20` 拷贝构造与赋值，添加显示移动语义并在转移后调用 `SecureWipe` 安全擦除缓冲区；修复 Windows `BCryptGenRandom` 超大缓冲区时的 `ULONG` 截断隐患。
- **核心算法与 UB 修复**：修复 C++23 `RandIntCE` 64 位无符号全范围溢出退化缺陷；修复 `RandBeta` 下溢错判；优化 `RotL` 移位消解 C++ 未定义行为。
- **API 优化与原生数组支持**：`RandElement` 替换为全局 `std::empty`/`std::size` 以原生兼容 C 风格数组；`RandUUID` 优化为 2 次 64-bit PRNG 调用并支持指定 `Engine&`；`RandBool` 切换为 `std::bernoulli_distribution`。
- **边界防范断言**：`RandReal` / `RandVector` / `RandFill` 补充 `assert(min <= max)` 校验。

## v1.4.0 - 2026-07-23

- **新增分布函数引擎重载**：`RandNormal`/`RandExp`/`RandPoisson`/`RandGamma` 支持传入自定义引擎实例（此前仅使用线程局部默认引擎）。
- **新增 jumpable/stream engine concepts**：`JumpableEngine`、`StreamEngine` 概念约束，为多流并行提供编译期类型检查。
- **新增 Google Benchmark 结构化基准套件**：替代原手工计时，支持 JSON 输出与跨 run 基线对比。
- **安全加固**：修复便捷 API 双头文件输出序列不一致（高危）、`RandBits` 位宽越界、ChaCha20 reseed 后状态擦除不完整、平台熵源回退路径等多项缺陷。
- **CMake 改进**：传播最低 C++ 标准要求（`target_compile_features`）；标记版本文件架构无关（`ARCH_INDEPENDENT`）；benchmark 目标重命名避免与 Google Benchmark 冲突。
- **发布渠道**：移除 Conan，新增 xmake-repo (xrepo) 支持；vcpkg port 改用 CMake 导出目标安装。
- **文档**：新增 Doxygen API 参考（GitHub Pages 部署）；补全注释组块与概念文档。
- **CI**：添加 concurrency/paths 过滤、issue 查重、benchmark 对比 continue-on-error。

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
