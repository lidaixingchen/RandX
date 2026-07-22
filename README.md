# RandX

基于 **xoshiro/xoroshiro** 算法族的纯头文件伪随机数生成器库。

原始算法：[David Blackman & Sebastiano Vigna](http://prng.di.unimi.it/)
原始 C++ 封装：[Ryo Suzuki (Xoshiro-cpp)](https://github.com/Reputeless/Xoshiro-cpp)

> **⚠️ 安全声明**
>
> 本库的 xoshiro / xoroshiro / SFC64 / RomuDuoJr / SplitMix64 引擎**均非 CSPRNG**（非密码学安全伪随机数生成器）。其状态可从输出逆推，**不可用于密码、密钥、会话 token、CSRF 令牌等安全场景**。
>
> 此类安全场景请使用 **`ChaCha20`** 引擎（RFC 8439，基于 OS 熵自动播种）或 **`SecureRandomBytes()`** 函数。

## 版本选择

本仓库提供两个头文件，按需选用：

| 头文件                 | 标准            | 命名空间    | 说明                                                                                                                                |
| ------------------- | ------------- | ------- | --------------------------------------------------------------------------------------------------------------------------------- |
| **RandX.hpp**       | C++23         | `RandX` | 增强版：concepts 约束、便捷 API（`RandInt`/`RandReal`/`RandBool`/`RandElement`）、线程局部默认引擎、`discard(n)`、Lemire 无偏 `RandIntCE`、多流接口、7 引擎、中文注释 |
| **RandX_Cpp17.hpp** | C++17 | `RandX` | 兼容版：SFINAE 约束、便捷 API、`discard(n)`、7 引擎、中文注释                                                                                      |

> 如果你的编译器支持 C++23，推荐使用 `RandX.hpp`；否则使用 `RandX_Cpp17.hpp`。
> 两者算法实现完全一致，输出序列相同（相同种子下），便捷 API 签名一致。

## 特性

- 满足 `std::uniform_random_bit_generator` 概念（RandX.hpp 含 `static_assert` 编译期验证）
- 全 `constexpr`，编译期可用
- 便捷 API：基础生成（`RandInt`/`RandReal`/`RandBool`/`RandChar`/`RandBits`）、分布（`RandNormal`/`RandExp`/`RandPoisson`/`RandGamma`/`RandWeighted` 等 16 种标准统计分布）、容器（`RandElement` 容器版/迭代器版/`RandSample` 容器版/迭代器版/`RandShuffle`/`RandPermutation`/`RandFill`/`RandVector`）、字符串（`RandString`/`RandUUID`/`RandChar(CharSet)` 预设字符集）、ranges 风格（`RandX::ranges::RandElement`/`RandSample`/`RandShuffle`/`RandFill`，仅 RandX.hpp）、流式序列化（`operator<<`/`operator>>`）
- 线程局部默认引擎（`DefaultEngine()`），零配置即用
- `constexpr` 编译期随机（仅 RandX.hpp）：`RandIntCE<Seed>(min, max)`
- 支持 `jump()` / `longJump()` 并行子序列、`discard(n)` 跳过、`serialize()` / `deserialize()` 状态持久化
- Lemire 有界法消除模偏差（`RandIntCE`）
- 全零吸收态防御（`assert` 检测）
- 多流接口 `MakeStreamEngine<Engine>(streamId, seed)` 用于并行计算
- 支持 SFC64 / RomuDuoJr 高速引擎
- **ChaCha20 CSPRNG**（RFC 8439）：密码学安全引擎，OS 熵自动播种，2^20 字节自动 reseed 前向安全
- **跨平台 OS 熵源**：`SecureRandomBytes()` 优先使用 BCryptGenRandom (Windows) / getrandom (Linux) / SecRandomCopyBytes (macOS)，RDRAND 可选加速
- 8 个引擎全覆盖（7 非 CSPRNG + 1 CSPRNG）
- 编译期洗牌 `ShuffleCE` / `ShuffledArray`（`std::shuffle` 的 constexpr 替代）
- `Reseed(seed)` 重置默认引擎，方便测试复现
- `std::seed_seq` 播种支持（所有 7 引擎）
- 硬件种子：`RandomSeed()` 优先使用 RDRAND（x86_64）
- MSVC 兼容：`RandIntCE` 条件编译（`__uint128_t` / 拒绝采样回退）
- CMake 安装支持：`find_package(RandX)` / `FetchContent`
- 包管理器支持：vcpkg / Conan
- GitHub Actions CI：GCC 14 / Clang 18 / MSVC 三编译器矩阵

## 引擎一览

| 引擎                   | 输出     | 周期           | 状态  | 适用场景                     |
| -------------------- | ------ | ------------ | --- | ------------------------ |
| Xoshiro256StarStar   | 64-bit | 2^256-1      | 32B | 通用首选，统计质量最优              |
| Xoroshiro128StarStar | 64-bit | 2^128-1      | 16B | 内存受限，统计更优                |
| Xoshiro128StarStar   | 32-bit | 2^128-1      | 16B | 32 位平台，统计更优              |
| Xoroshiro64StarStar  | 32-bit | 2^64-1       | 8B  | 极端内存受限                   |
| SplitMix64           | 64-bit | 2^64         | 8B  | 种子扩展 / 哈希，非通用 PRNG       |
| SFC64                | 64-bit | >= 2^64      | 32B | 速度极快，通过 PractRand，无 jump |
| RomuDuoJr            | 64-bit | >= 2^51 (估计) | 16B | 极简极快，非关键模拟，无 jump        |
| **ChaCha20**         | 64-bit | 无周期 (CSPRNG) | 56B | **密码学安全**，RFC 8439，OS 熵播种 |

## 快速上手

```cpp
#include "RandX.hpp"
#include <iostream>

int main()
{
    // 最简用法：便捷函数（内部使用线程局部 Xoshiro256StarStar）
    std::cout << RandX::RandInt(1, 6) << '\n';   // 掷骰子 [1, 6]
    std::cout << RandX::RandReal() << '\n';       // [0.0, 1.0)
    std::cout << RandX::RandBool(0.3) << '\n';    // 30% 概率为 true
    std::cout << RandX::RandNormal() << '\n';     // 正态分布 N(0,1)
}
```

## API 一览

| 分类        | 函数                                   | 说明                                                                           | 双标准        |
| --------- | ------------------------------------ | ---------------------------------------------------------------------------- | ---------- |
| 基础生成      | `RandInt(min, max)`                  | [min, max] 闭区间整数                                                             | ✅          |
|           | `RandReal(min, max)`                 | [min, max) 浮点数                                                               | ✅          |
|           | `RandBool(p)`                        | 概率 p 为 true                                                                  | ✅          |
|           | `RandChar(min, max)`                 | [min, max] 范围随机字符（`char`/`wchar_t`/`char16_t`/`char32_t`，C++20+ 含 `char8_t`） | ✅          |
|           | `RandChar(CharSet)`                  | 从预设字符集随机取一个字符                                                                | ✅          |
|           | `RandBits<N>()`                      | N 位随机整数 [0, 2^N)                                                             | ✅          |
| 分布        | `RandNormal(mean, stddev)`           | 正态分布                                                                         | ✅          |
|           | `RandExp(lambda)`                    | 指数分布                                                                         | ✅          |
|           | `RandPoisson(mean)`                  | 泊松分布                                                                         | ✅          |
|           | `RandGamma(alpha, beta)`             | 伽马分布                                                                         | ✅          |
|           | `RandWeighted(weights)`              | 按权重选取索引                                                                      | ✅          |
|           | `RandBernoulli(p)`                   | 伯努利分布（别名封装 `RandBool`）                                                       | ✅          |
|           | `RandBinomial(t, p)`                 | 二项分布                                                                         | ✅          |
|           | `RandLogNormal(mean, stddev)`        | 对数正态分布                                                                       | ✅          |
|           | `RandGeometric(p)`                   | 几何分布                                                                         | ✅          |
|           | `RandCauchy(a, b)`                   | 柯西分布                                                                         | ✅          |
|           | `RandWeibull(a, b)`                  | 韦布尔分布                                                                        | ✅          |
|           | `RandExtremeValue(a, b)`             | 极值分布                                                                         | ✅          |
|           | `RandChiSquared(n)`                  | 卡方分布                                                                         | ✅          |
|           | `RandStudentT(n)`                    | 学生 t 分布                                                                      | ✅          |
|           | `RandFisherF(m, n)`                  | Fisher F 分布                                                                  | ✅          |
|           | `RandBeta(a, b)`                     | Beta 分布（自实现）                                                                 | ✅          |
| 容器        | `RandElement(container)`             | 随机取一个元素（容器版，返回引用）                                                            | ✅          |
|           | `RandElement(first, last)`           | 随机取一个元素（迭代器版，返回迭代器；随机访问 O(1)，输入迭代器 O(n) reservoir sampling）                  | ✅          |
|           | `RandSample(container, n)`           | 无放回抽样 n 个（容器版）                                                               | ✅          |
|           | `RandSample(first, last, n)`         | 无放回抽样 n 个（迭代器版；随机访问含 hash-set/索引双分支，输入迭代器用 reservoir sampling O(n) 内存）       | ✅          |
|           | `RandShuffle(container)`             | 随机打乱                                                                         | ✅          |
|           | `RandPermutation(n)`                 | [0, n) 随机排列                                                                  | ✅          |
|           | `RandFill(first, last, min, max)`    | 用随机值填充 [first, last) 范围（STL 风格）                                              | ✅          |
|           | `RandVector(min, max, n)`            | 生成 n 个随机值构成的 `std::vector`                                                   | ✅          |
| ranges 风格 | `ranges::RandElement(range)`         | 随机取一个元素（返回值拷贝，仅 RandX.hpp）                                                   | ❌ C++23 专属 |
|           | `ranges::RandSample(range, n)`       | 无放回抽样（仅 RandX.hpp）                                                           | ❌ C++23 专属 |
|           | `ranges::RandShuffle(range)`         | 随机打乱（仅 RandX.hpp）                                                            | ❌ C++23 专属 |
|           | `ranges::RandFill(range, min, max)`  | 填充（模板化支持 int/double/float，仅 RandX.hpp）                                       | ❌ C++23 专属 |
| 字符串/ID    | `RandString(len, charset)`           | 随机字符串（`charset` 为 `string_view`）                                             | ✅          |
|           | `RandString(n, CharSet)`             | 从预设字符集生成随机字符串                                                                | ✅          |
|           | `RandUUID()`                         | UUID v4                                                                      | ✅          |
| 序列化       | `serialize()` / `deserialize(state)` | 状态持久化（主接口，所有引擎）                                                              | ✅          |
|           | `operator<<` / `operator>>`          | 流式语法糖（仅 `state_type` 为容器类的引擎，排除 `SplitMix64`）                                | ✅          |
| 编译期       | `RandIntCE<Seed>(min, max)`          | 编译期随机整数（仅 RandX.hpp）                                                         | ❌ C++23 专属 |
|           | `ShuffleCE(first, last)`             | 编译期洗牌                                                                        | ❌ C++23 专属 |
|           | `ShuffledArray<T,N,Seed>(arr)`       | 编译期洗牌数组版                                                                     | ❌ C++23 专属 |
| CSPRNG     | `ChaCha20()`                          | 默认构造（OS 熵自动播种，密码学安全）                                                     | ✅          |
|           | `ChaCha20(seed)`                      | 显式 64-bit 种子构造（**非密码学安全**，仅测试/复现）                                       | ✅          |
|           | `ChaCha20(key, klen, nonce, nlen, ctr)` | 显式 key(32B)+nonce(12B)+counter 构造（KAT/高级用法）                              | ✅          |
|           | `rng.reseed()`                       | 手动从 OS 熵重新播种（重置 counter/缓存）                                              | ✅          |
| OS 熵源     | `SecureRandomBytes(buf, n)`          | OS 密码学熵填充 n 字节（失败抛 `std::runtime_error`）                                  | ✅          |
|           | `SecureSeed()`                       | 密码学安全 64-bit 种子（`[[nodiscard]]`）                                            | ✅          |
|           | `IsOsCryptoEntropyAvailable()`       | 当前是否走 OS 密码学 API（false=降级到 `std::random_device`，非密码学安全）                  | ✅          |

> **CharSet 枚举**：`Alphanumeric` / `Alpha` / `Lower` / `Upper` / `Digit` / `Hex` / `Printable` / `Base64` / `Base64UrlSafe`，覆盖常见字符集场景（`Base64UrlSafe` 为 RFC 4648 §5 URL-safe 变体，字母表 `[A-Za-z0-9-_]`）。

所有函数默认使用线程局部 `Xoshiro256StarStar` 引擎，也支持传入自定义引擎：`RandInt(rng, min, max)`。

```cpp
// 示例
RandX::RandInt(1, 6);              // 掷骰子
RandX::RandExp(2.0);               // 指数分布 λ=2
RandX::RandSample(pool, 3);        // 无放回抽 3 个
RandX::RandString(16);             // 16 位随机 token
RandX::RandUUID();                 // "a1b2c3d4-e5f6-4789-abcd-..."
constexpr auto v = RandX::RandIntCE(0, 100);  // 编译期确定
```

## 手动管理引擎

```cpp
#include "RandX.hpp"
#include <random>
#include <iostream>

int main()
{
    // 用真随机种子创建引擎
    RandX::Xoshiro256StarStar rng{ RandX::RandomSeed() };

    // 指定引擎的便捷函数
    std::cout << RandX::RandInt(rng, 0, 99) << '\n';

    // 配合标准库 distribution
    std::normal_distribution<double> norm(0.0, 1.0);
    std::cout << norm(rng) << '\n';

    // 序列化 / 反序列化
    auto state = rng.serialize();
    rng.deserialize(state);

    // 跳跃（并行子序列）
    rng.jump();      // 前进 2^128 步
    rng.longJump();  // 前进 2^192 步

    // 跳过
    rng.discard(1000);
}
```

## 编译期随机（仅 RandX.hpp）

```cpp
#include "RandX.hpp"

// 编译期确定结果（固定种子）
constexpr int compileTimeDice = RandX::RandIntCE(1, 6);
static_assert(compileTimeDice >= 1 && compileTimeDice <= 6);

// 自定义种子
constexpr auto v = RandX::RandIntCE<int, 42>(0, 100);
```

## 按权重随机选取

```cpp
#include "RandX.hpp"
#include <vector>
#include <iostream>

int main()
{
    // 权重：A=1, B=2, C=7 → C 被选中概率约 70%
    std::vector<double> weights = { 1.0, 2.0, 7.0 };
    auto idx = RandX::RandWeighted(weights);
    std::cout << "选中索引: " << idx << '\n';
}
```

## 多流并行

```cpp
#include "RandX.hpp"
#include <iostream>

int main()
{
    // 创建 4 个不重叠的子序列引擎（每个间隔 2^128 步）
    auto rng0 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(0);
    auto rng1 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(1);
    auto rng2 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(2);
    auto rng3 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(3);

    // 各流独立生成，互不重叠
    std::cout << RandX::RandInt(rng0, 0, 99) << '\n';
    std::cout << RandX::RandInt(rng1, 0, 99) << '\n';
}
```

## 安全使用指南

不同场景应选择不同引擎，错误选型可能导致安全漏洞或性能浪费：

| 场景                            | 推荐方案                                              | 理由                                  |
| ----------------------------- | ------------------------------------------------- | ----------------------------------- |
| 模拟 / 蒙特卡洛 / 游戏                | `Xoshiro256StarStar`                              | 统计质量最优，速度极快                         |
| 性能极致 / 非关键模拟                  | `SFC64` / `RomuDuoJr`                             | 速度更快，统计质量足够                          |
| 并行子序列                         | `Xoshiro256StarStar` + `MakeStreamEngine` (jump) | jump 提供不重叠子序列                       |
| 32 位平台 / 内存受限                 | `Xoroshiro128StarStar` / `Xoshiro128StarStar`     | 状态更小（16B）                            |
| **密码 / 密钥 / 会话 token / CSRF 令牌** | **`ChaCha20`** 或 **`SecureRandomBytes()`**         | CSPRNG，状态不可逆推，OS 熵播种                 |
| 一次性安全随机字节                     | **`SecureRandomBytes()`**                         | 直接取 OS 熵，不经 PRNG 算法                  |
| 密码学安全种子                       | **`SecureSeed()`**                                | 8 字节 OS 熵，可直接喂给统计 PRNG              |

> **核心判别**：如果"输出可预测"会造成损害 → 必须用 CSPRNG（`ChaCha20` 或 `SecureRandomBytes`）；否则用统计 PRNG 即可。
>
> **降级检测**：使用 `IsOsCryptoEntropyAvailable()` 在运行时检测是否走 OS 密码学 API。返回 false 表示当前运行在 `std::random_device` 兜底路径（非密码学安全），此时 `ChaCha20()` 默认构造**不可用于安全场景**。

## ChaCha20 CSPRNG（密码学安全）

密码学安全场景（密钥/token/会话密钥/CSRF 令牌等）请使用 `ChaCha20` 引擎或 `SecureRandomBytes()`。

```cpp
#include "RandX.hpp"
#include <iostream>

int main()
{
    // 1) 默认构造：OS 熵自动播种（密码学安全）
    RandX::ChaCha20 rng;
    std::cout << rng() << '\n';           // 64-bit 随机数

    // 2) 直接取 OS 密码学熵字节（不经 ChaCha20，一次性）
    std::uint8_t key[32];
    RandX::SecureRandomBytes(key, sizeof(key));

    // 3) 密码学安全种子（8 字节，可直接喂给统计 PRNG）
    std::uint64_t seed = RandX::SecureSeed();

    // 4) 检测当前平台是否走 OS 密码学 API（true=安全；false=降级兜底）
    if (!RandX::IsOsCryptoEntropyAvailable())
        std::cerr << "警告：当前运行在 std::random_device 兜底路径\n";

    // 5) 与便捷 API 配合（rng 满足 UniformRandomBitGenerator）
    std::cout << RandX::RandInt(rng, 1, 1000) << '\n';

    // 6) 手动 reseed（强制刷新 key/nonce/counter）
    rng.reseed();

    // 7) 显式 key + nonce + counter（仅测试/KAT 复现，非密码学安全）
    const std::uint8_t k[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                                 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    const std::uint8_t n[12] = {0,0,0,9,0,0,0,0x4a,0,0,0,0};
    RandX::ChaCha20 kat_rng(k, 32, n, 12, 1);  // counter=1 对应 RFC 8439 §2.3.2
}
```

> **安全边界**：`ChaCha20` 单实例非线程安全；不提供 `serialize`/`jump`（状态导出违背 CSPRNG 安全模型）。生成 `2^20` 字节后自动 reseed 提供前向安全；counter 回绕前必然触发 reseed，杜绝 keystream 复用。
>
> **降级提示**：当 `IsOsCryptoEntropyAvailable()` 返回 false 时，`ChaCha20()` 默认构造走的 `std::random_device` 路径**非密码学安全**，仅作兜底保证可用性。

## 编译期洗牌

```cpp
#include "RandX.hpp"
#include <array>
#include <iostream>

int main()
{
    // 编译期 Fisher-Yates 洗牌
    constexpr auto shuffled = [] {
        std::array<int, 10> a = {0,1,2,3,4,5,6,7,8,9};
        RandX::ShuffleCE(a.begin(), a.end());
        return a;
    }();

    for (const auto& x : shuffled)
        std::cout << x << ' ';
}
```

## 基准测试

编译并运行 benchmark.cpp：

```bash
g++ -std=c++23 -O2 benchmark.cpp -o benchmark && ./benchmark
```

输出各引擎的吞吐量（Mops/s、MB/s）、jump 开销和 RandInt API 性能。

## CMake 集成

### FetchContent（推荐）

```cmake
include(FetchContent)
FetchContent_Declare(RandX
    GIT_REPOSITORY https://github.com/lidaixingchen/RandX.git
    GIT_TAG master
)
FetchContent_MakeAvailable(RandX)
target_link_libraries(myapp PRIVATE RandX::RandX)
```

### find_package

```bash
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
cmake --install .
```

```cmake
find_package(RandX REQUIRED)
target_link_libraries(myapp PRIVATE RandX::RandX)
```

### 构建测试

```bash
cmake -B build -DRANDX_BUILD_TESTS=ON -DRANDX_BUILD_BENCHMARK=ON
cmake --build build
ctest --test-dir build
```

## 包管理器

> **状态**：尚未发布到 vcpkg 官方 ports / xmake-repo。当前仅以 overlay / 本地仓库形式可用。本仓库已就绪 PR 模板与 CI 预演，待提交后即可被官方收录。提交进度详见 `packaging/` 目录与下方"发布流程"小节。

### CMake FetchContent（零依赖推荐）

适合所有 CMake 3.11+ 用户，无需安装任何包管理器：

```cmake
include(FetchContent)
FetchContent_Declare(
    randx
    URL https://github.com/lidaixingchen/RandX/archive/refs/tags/v1.3.1.tar.gz
)
FetchContent_MakeAvailable(randx)

target_link_libraries(your_target PRIVATE RandX::RandX)
```

### vcpkg（overlay 模式，当前可用）

```bash
# 自定义注册表方式：将本仓库的 ports/ 作为 overlay
vcpkg install randx --overlay-ports=path/to/this/repo/ports
```

### xrepo / xmake（本地仓库，当前可用）

```bash
# 注册本地 xmake-repo 后即可安装
xrepo add-repo local-randx path/to/this/repo/packaging/xmake-repo
xrepo install randx
```

在 xmake 工程中使用：

```lua
add_repositories("local-randx path/to/this/repo/packaging/xmake-repo")
add_requires("randx")

target("your_target")
    set_languages("c++23")
    add_packages("randx")
```

### 发布流程（维护者）

> CI 工作流 [`packaging-validation.yml`](.github/workflows/packaging-validation.yml) 会在推送 `v*` 标签时自动验证 port 是否能正常构建；通过后仍需手动提交 PR——官方 registry 不接受 CI 自动推送。

**首次发布到 vcpkg 官方 ports**：

1. Fork [microsoft/vcpkg](https://github.com/microsoft/vcpkg)
2. 复制本仓库 `ports/randx/` 到 fork 仓库的 `ports/randx/`
3. 在 fork 中执行 `vcpkg x-add-version randx` 自动生成 `versions/r-/randx.json` 和更新 `versions/baseline.json`（参考 `packaging/vcpkg/versions/` 下的模板）
4. 提交 PR，等待 vcpkg 维护者 review

**首次发布到 xmake-repo**：

1. Fork [xmake-io/xmake-repo](https://github.com/xmake-io/xmake-repo)
2. 复制本仓库 `packaging/xmake-repo/packages/r/randx/` 到 fork 仓库的 `packages/r/randx/`
3. 在 fork 中执行 `xrepo install randx` 本地验证
4. 提交 PR，等待 xmake 维护者 review

**后续版本发布**：

1. 推送新版本 tag（如 `v1.4.0`），CI 会自动验证
2. 更新 `vcpkg.json`、`ports/randx/vcpkg.json`、`CMakeLists.txt` 中的 `version` 字段
3. 下载新版本 GitHub tarball 计算 SHA512（更新到 `ports/randx/portfile.cmake`）与 SHA256（更新到 `packaging/xmake-repo/packages/r/randx/xmake.lua` 的 `add_versions`）
4. 重新执行上述 PR 流程，在 `versions/r-/randx.json` 中追加新版本记录

## 编译要求

| 头文件             | 最低标准  | 推荐编译器                             |
| --------------- | ----- | --------------------------------- |
| RandX.hpp       | C++23 | GCC 14+ / Clang 18+ / MSVC 17.10+ |
| RandX_Cpp17.hpp | C++17 | GCC 9+ / Clang 10+ / MSVC 16.8+   |

- 无外部依赖，纯头文件，复制即用

## 测试

仓库包含确定性单元测试（doctest 框架，固定种子 → 断言已知输出序列）：

```bash
# C++23 测试（RandX.hpp）
g++ -std=c++23 -Wall -Wextra -I third_party -o test_randx test_randx.cpp && ./test_randx

# C++17 测试（RandX_Cpp17.hpp）
g++ -std=c++17 -Wall -Wextra -I third_party -o test_randx_cpp17 test_randx_cpp17.cpp && ./test_randx_cpp17

# C++20 测试（验证 char8_t 条件编译路径）
g++ -std=c++20 -Wall -Wextra -I third_party -o test_randx_cpp17 test_randx_cpp17.cpp && ./test_randx_cpp17
```

> **OS 熵链接**：v1.3 起 ChaCha20 CSPRNG 依赖 OS 密码学熵源，编译时需链接平台系统库：
>
> | 平台      | 链接参数                  | 系统 API              |
> | ------- | --------------------- | ------------------- |
> | Windows | `-lbcrypt`            | BCryptGenRandom      |
> | macOS   | `-framework Security` | SecRandomCopyBytes   |
> | Linux   | 无                     | getrandom（libc 内置）   |
>
> 例：Windows 上 `g++ -std=c++23 -Wall -Wextra -I third_party -o test_randx test_randx.cpp -lbcrypt`。使用 CMake / Conan / vcpkg 时链接自动处理。

## CI

每次 push 自动在以下环境编译 + 运行测试：

| 编译器         | 平台           | 标准                    |
| ----------- | ------------ | --------------------- |
| GCC 14      | Ubuntu 24.04 | C++17 / C++20 / C++23 |
| Clang 18    | Ubuntu 24.04 | C++17 / C++20 / C++23 |
| MSVC (v145) | Windows 2025 | C++17 / C++23         |

C++20 矩阵专门覆盖 `char8_t` 条件编译路径。覆盖率任务使用 lcov，阈值 80%。

另有 **PractRand nightly job**（`.github/workflows/practrand-nightly.yml`）：每天 03:00 UTC 自动运行，验证 8 引擎统计质量（7 统计 PRNG 各 1TB + ChaCha20 256GB）。失败时自动创建 issue 通知。支持 `workflow_dispatch` 手动触发并指定测试长度。

## 变更记录

完整版本演进见 [CHANGELOG.md](CHANGELOG.md)。

## 致谢

- 算法设计：David Blackman & Sebastiano Vigna
- 原始 C++ 封装：Ryo Suzuki ([Xoshiro-cpp](https://github.com/Reputeless/Xoshiro-cpp), MIT License)
