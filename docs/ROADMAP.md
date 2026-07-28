# RandX 项目演进与改进路线图 (Project Roadmap)

本文档旨在明确 **RandX** 伪随机数生成器库的未来改进与演进方向。通过与全球顶级工业级随机数基础设施（如 *Intel MKL VSL*, *Google Abseil Random*, *PCG*, *Rust `rand`*, *libsodium*, *TRNG*, *SFMT* 等）进行系统性多维对比，我们制定了以下重构与功能演化方案，以推动 RandX 从一个优秀的轻量级开源库，演进为兼具极限吞吐量、工程安全性与现代 C++ 规范的顶级 PRNG/CSPRNG 基础设施。

> **维护约定**：本路线图每次版本发布后须与代码库对账修订；每个特性条目须标注
> **C++17 同步策略**（同步 / 降级 / 仅 C++23）、**破坏性**（是 / 否）与**验收标准**。

---

## 目录

- [一、 战略定位与总体目标](#一-战略定位与总体目标)
- [二、 核心改进维度与落地方案](#二-核心改进维度与落地方案)
  - [1. 安全维度：SecureWipe 强化与密码学内存屏障](#1-安全维度securewipe-强化与密码学内存屏障)
  - [2. 数值与分布维度：直通浮点便捷层与科学分布扩展](#2-数值与分布维度直通浮点便捷层与科学分布扩展)
  - [3. 工程生态维度：上游包仓库提交与性能看板](#3-工程生态维度上游包仓库提交与性能看板)
  - [4. 性能维度：SIMD 矢量化批量填充](#4-性能维度simd-矢量化批量填充)
  - [5. 多线程与 HPC 维度：并行随机流切割](#5-多线程与-hpc-维度并行随机流切割)
  - [6. 接口维度：强类型 BitGen 范式隔离](#6-接口维度强类型-bitgen-范式隔离)
- [三、 已完成事项存档](#三-已完成事项存档)
- [四、 版本演进阶段划分 (Milestones)](#四-版本演进阶段划分-milestones)
- [五、 前瞻观察项（暂不排期）](#五-前瞻观察项暂不排期)

---

## 一、 战略定位与总体目标

RandX 致力于在保持 **Header-only（纯头文件）**、**Zero-dependency（零外部依赖）**、**C++23 Concepts / C++17 SFINAE 现代化 API** 的优势前提下，针对工业级高并发、高性能计算 (HPC) 与严苛安全场景填补补强，实现以下三大目标：

1. **工程安全深化**：完善 Release 模式下的密码学内存抹除机制，对齐 *libsodium* 级别的敏感数据防护。
2. **吞吐量突破**：引入硬件级 SIMD 矢量化批量生成（AVX2/NEON），消除大数组填充的标量瓶颈。
3. **多线程/集群扩展**：在既有 `jump()`/`MakeStreamEngine` 之上封装一键式并行流分配器，支持大规模多线程与 OpenMP 计算。

### 与既有核心约束的张力声明

本项目存在两条第一优先级约束，所有路线图条目在设计时必须显式回应：

- **双头文件同步**：`RandX.hpp` 与 `RandX_Cpp17.hpp` 的算法输出序列完全一致、便捷 API 签名一致。新特性若无法在 C++17 下等价实现，须明确标注降级或排除策略。
- **引擎精简原则**（v1.5 引擎去重的延续）：新增引擎前必须完成与现有引擎（`SFC64`、`RomuDuoJr` 等）的差异化定位论证，避免功能重叠回潮。

---

## 二、 核心改进维度与落地方案

> 各维度按当前优先级排序：低风险高价值项优先，需前置设计文档的高风险项靠后。

### 1. 安全维度：SecureWipe 强化与密码学内存屏障

* **标杆**：*libsodium*
* **现状**：`detail::SecureWipe` 目前为 volatile 逐字节清零，可防常规死存储消除，但在 LTO 全程序优化下防护强度不足，且未利用 OS 专用 API。
* **落地方案**：
  * Windows 下调用 `::SecureZeroMemory`，Linux 下调用 `explicit_bzero`（glibc ≥ 2.25）/ `memset_s`，macOS 下调用 `memset_s`，无可用 API 时回退现有 volatile 循环并追加 `asm volatile("" ::: "memory")` 编译器屏障。
  * 平台分支与既有 OS 熵源优先级链（BCrypt/getrandom/SecRandomCopyBytes）的条件编译结构保持一致风格。
* **C++17 同步策略**：同步（纯预处理器分支，无语言特性差异）。
* **破坏性**：否。
* **验收标准**：各平台编译通过；`-O2 -flto` 下反汇编确认 wipe 未被消除；ChaCha20 现有擦除调用点行为不变。

### 2. 数值与分布维度：直通浮点便捷层与科学分布扩展

* **公开 `RandUniform53` / `RandUniform24` 便捷 API**
  * **标杆**：*xoshiro 官方原论文推荐算法*
  * **现状**：核心转换算法已由 `DoubleFromBits`（`(i >> 11) * 0x1.0p-53`）与 `FloatFromBits` 实现并公开声明。
  * **落地方案**：在便捷 API 层补充带引擎参数与默认引擎两种重载的 `RandUniform53(engine)` / `RandUniform24(engine)`，内部复用既有转换函数；32 位输出引擎（`Xoshiro128StarStar`、`Xoroshiro64StarStar`）在 53-bit 路径下须双抽取拼接，24-bit 路径走 `FloatFromBits`。同时评估 `RandReal` 在 `[0,1)` 特例下切换直通快速路径的收益与回归风险。
  * **C++17 同步策略**：同步。
  * **破坏性**：否（`RandReal` 快速路径若改变输出序列则单独评审，默认不改变）。
  * **验收标准**：KAT 固定种子输出比对；与 `std::uniform_real_distribution` 基准对比提速 ≥ 30%；两头文件输出一致。
* **扩展工程与科学分布：Zipf（齐普夫）、Triangular（三角形）**
  * **现状**：标准库无此二者；柯西分布已由 `RandCauchy` 提供，不在本条目范围。
  * **落地方案**：`RandZipf(n, s)` 采用拒绝-反演法（参考 Rust `rand_distr::Zipf`）；`RandTriangular(min, peak, max)` 采用逆变换法。参数校验与异常行为对齐现有 16 种分布 API 风格。
  * **C++17 同步策略**：同步。
  * **破坏性**：否。
  * **验收标准**：矩检验（均值/方差）纳入统计分布测试套件；卡方拟合优度检验通过。

### 3. 工程生态维度：上游包仓库提交与性能看板

* **上游包仓库提交**
  * **现状**：本地 vcpkg port（`ports/randx/`）、registry 版本片段（`packaging/vcpkg/versions/`）、xmake-repo 打包（`packaging/xmake-repo/`）均已就绪；`RandXConfig.cmake.in` 与 CMake `install(EXPORT)` 已支持 `find_package(RandX)` + `target_link_libraries(target PRIVATE RandX::RandX)`。
  * **落地方案**：剩余工作为向上游提交——vcpkg 官方 registry PR、xmake-repo 官方仓库 PR、新增 Conan Center recipe 并提交 conan-center-index。
  * **C++17 同步策略**：不适用（打包层）。
  * **破坏性**：否。
  * **验收标准**：三个上游仓库的 PR 被合并，用户可通过官方源直接安装。
* **自动化性能看板**
  * **现状**：`benchmark_gbench.cpp`（Google Benchmark）与 `tools/compare_benchmark.py` 已存在。
  * **落地方案**：CI 中接入 `github-action-benchmark`，将各引擎 GB/s 吞吐量按提交历史发布至 gh-pages 看板；覆盖 x86-64（AVX2）与 ARM64（GitHub Actions ARM runner）两类架构。
  * **验收标准**：看板随每次 push 自动更新；性能回退超阈值（如 10%）时 CI 告警。

### 4. 性能维度：SIMD 矢量化批量填充

* **标杆**：*Intel MKL VSL*, *SFMT*
* **落地方案**：提供 `RandFillSIMD`（AVX2 / ARM NEON）批量填充接口，利用 `__m256i` 一次并行推进 4 条独立 64-bit 引擎状态，将大数组填充吞吐量提升 3~4 倍。
* **可复现性声明（设计前提）**：
  * SIMD 路径本质是 N 条独立流的交织输出，**输出序列与标量引擎不一致，且不承诺跨指令集一致**。`RandFillSIMD` 定位为独立的高吞吐填充设施，配套独立 KAT（固定种子下 AVX2 与 NEON 各自的已知序列），不纳入标量引擎 KAT 体系。
  * 指令集选择采用**编译期分发**（`__AVX2__` / `__ARM_NEON` 宏检测，未启用时静默回退标量实现），不做运行时 dispatch——header-only 形态下运行时多版本分发在 MSVC 上支持不佳。
  * AVX-512 降级为实验性观察项（CI 标准 runner 无法覆盖测试，见[前瞻观察项](#五-前瞻观察项暂不排期)）。
* **前置条件**：需先产出独立设计文档（状态布局、seed 派生规则、回退语义），评审通过后开工。
* **C++17 同步策略**：同步（intrinsics 与语言标准无关；C++23 版可额外提供 ranges 风格重载）。
* **破坏性**：否（新增接口）。
* **验收标准**：AVX2/NEON 路径吞吐 ≥ 标量 `RandFill` 3 倍；SIMD KAT 通过；PractRand 256GB 无失败；未启用 SIMD 的平台回退路径输出与文档声明一致。

### 5. 多线程与 HPC 维度：并行随机流切割

* **标杆**：*TRNG*, *Intel MKL VSL*
* **现状**：已有 `MakeStreamEngine<Engine>(streamId, seed)` 基于 `jump()` 创建非重叠子序列。
* **落地方案**：`ParallelStreamGrid` 定位为 **`MakeStreamEngine` 之上的高阶封装**（非替代）：
  * **Block-Splitting（块切分）**：基于既有 `jump()`（2^128 步长）为线程池 / OpenMP 并行循环一键分配互不重叠的引擎实例，管理流 ID 分配与生命周期。
  * **跨线程自动派生**：主线程派生子线程时自动递增流 ID，保证数学上的无重叠。
  * **明确不做 Leapfrog（跨步法）**：xoshiro 族无廉价任意步长 skip-ahead，Leapfrog 需逐步交织或现算跳转多项式，性能与复杂度均不可接受。该能力仅在未来引入支持任意跳转的引擎（如 PCG64）时重新评估。
* **C++17 同步策略**：同步（`std::thread` / thread_local 均为 C++17 可用）。
* **破坏性**：否。
* **验收标准**：多线程场景下各流序列两两无重叠（jump 数学保证 + 抽样碰撞检测）；与手写 `MakeStreamEngine` 循环相比零额外吞吐损耗。

### 6. 接口维度：强类型 BitGen 范式隔离

* **标杆**：*Google Abseil*（`absl::BitGen` vs `absl::InsecureBitGen`）
* **落地方案**：在 API 层明确区分场景，从编译期杜绝类型误用：

  ```cpp
  // 用于游戏、渲染、模拟（追求极速）
  RandX::InsecureRng rng;

  // 用于密钥、令牌、安全通信（密码学安全）
  RandX::CryptoRng secureRng;
  ```

* **迁移与弃用路径（破坏性变更，随 v3.0 主版本发布）**：
  * 现有便捷 API 的 `thread_local Xoshiro256StarStar` 默认引擎归入 `InsecureRng` 语义侧，行为不变；
  * v2.x 末期先以别名 + `[[deprecated]]` 提示引导迁移，v3.0 正式切换；
  * footer 标注 `BREAKING CHANGE`，CHANGELOG 提供逐 API 迁移对照表。
* **前置条件**：独立设计文档（类型层级、与既有 8 引擎的关系、`DefaultEngine()` 归属）评审通过。
* **C++17 同步策略**：同步（类型别名与包装类均可在 C++17 实现）。
* **破坏性**：**是**。
* **验收标准**：`CryptoRng` 误用于普通分布 API 或反向误用时编译期报错（C++23 concepts / C++17 static_assert）；既有测试全量迁移通过。

---

## 三、 已完成事项存档

以下条目曾列于路线图，经与代码库对账确认已实现，存档备查：

| 事项 | 实现位置 | 完成版本 |
|------|----------|----------|
| 直通无偏浮点转换核心算法 | `DoubleFromBits` / `FloatFromBits` | v1.x 早期 |
| 柯西分布 | `RandCauchy`（含统计测试） | v1.x |
| Release 模式全零吸收态兜底 | `EngineBase` state 构造 / `deserialize()` 静默修正（`s_[0]=1`）；ChaCha20 强制修正 | v1.5 |
| 现代 CMake 导出（`RandX::RandX`） | `RandXConfig.cmake.in` + `install(EXPORT)` | v1.x |
| 本地 vcpkg port 与 registry 片段 | `ports/randx/`、`packaging/vcpkg/versions/` | v1.x |
| xmake-repo 打包 | `packaging/xmake-repo/packages/r/randx/` | v1.x |
| Google Benchmark 基准与对比脚本 | `benchmark_gbench.cpp`、`tools/compare_benchmark.py` | v1.4 |

---

## 四、 版本演进阶段划分 (Milestones)

当前版本：**v1.4.3**（v1.5 引擎去重已在主干落地，待发布）。

```
      [v1.6 近期计划] ──► SecureWipe OS API 强化 / RandUniform53·24 便捷层 / Zipf·Triangular 分布
                                │
                                ▼
      [v1.7 生态补全] ──► vcpkg·Conan·xmake 上游提交 / CI 性能看板 (gh-pages)
                                │
                                ▼
      [v2.0 性能与多线程] ──► RandFillSIMD (AVX2/NEON) / ParallelStreamGrid 并行流封装
                                │
                                ▼
      [v3.0 架构升级 ⚠BREAKING] ──► Abseil 式强类型 InsecureRng/CryptoRng 隔离
```

### Milestone 1 (v1.6 安全与数值补强)

- [ ] 升级 `detail::SecureWipe`：接入 `SecureZeroMemory` / `explicit_bzero` / `memset_s` 与编译器内存屏障（两头文件同步）。
- [ ] 公开 `RandUniform53` / `RandUniform24` 便捷 API（复用既有 `DoubleFromBits`/`FloatFromBits`），并评估 `RandReal` 快速路径。
- [ ] 新增 `RandZipf`、`RandTriangular` 分布及配套统计测试。

### Milestone 2 (v1.7 生态补全)

- [ ] vcpkg 官方 registry、conan-center-index、xmake-repo 官方仓库三路上游提交。
- [ ] CI 接入 `github-action-benchmark`，建立 x86-64 / ARM64 双架构 GB/s 吞吐看板与回退告警。

### Milestone 3 (v2.0 性能与多线程突破)

- [ ] 产出并评审 `RandFillSIMD` 设计文档（状态布局 / seed 派生 / 独立 KAT / 回退语义）。
- [ ] 实现 AVX2 / NEON 编译期分发的 `RandFillSIMD` 及标量回退路径。
- [ ] 实现 `ParallelStreamGrid`（基于 `MakeStreamEngine` 的高阶封装，Block-Splitting 策略）。

### Milestone 4 (v3.0 生态与架构升级，BREAKING)

- [ ] 产出并评审强类型 `InsecureRng` / `CryptoRng` 隔离层设计文档（含 `DefaultEngine()` 归属决策）。
- [ ] v2.x 末期落地 `[[deprecated]]` 迁移提示，v3.0 完成切换并发布迁移对照表。

---

## 五、 前瞻观察项（暂不排期）

以下方向保持技术跟踪，暂不进入里程碑：

* **AVX-512 矢量化**：CI 标准 runner 无 AVX-512 硬件，无法建立可靠回归测试；待基础设施可用后由 v2.0 的 AVX2 设施平滑扩展。
* **新增极速引擎（wyrand / PCG64）**：与现有 `SFC64` / `RomuDuoJr` 定位重叠度高，违背 v1.5 引擎去重方向；仅当出现明确差异化需求（如 PCG64 的任意步长跳转解锁 Leapfrog）时重启论证。
* **C++26 `std::generate_random`（P1068）**：标准化落地后为 `RandFillSIMD` 提供标准接口适配层。
* **C++20 Modules（`import RandX;`）**：待三大编译器 named modules 支持成熟后评估。
* **TestU01 BigCrush 定期检测**：作为 PractRand nightly 的补充，评估 CI 时长成本后决定。
* **Doxygen 文档站点**：基于既有 `Doxyfile` 发布 GitHub Pages API 文档。

---

*最新更新日期：2026-07-28（依据代码库对账结果全面修订）*
