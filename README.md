# RandX

基于 **xoshiro/xoroshiro** 算法族的纯头文件伪随机数生成器库。

原始算法：[David Blackman & Sebastiano Vigna](http://prng.di.unimi.it/)
原始 C++ 封装：[Ryo Suzuki (Xoshiro-cpp)](https://github.com/Reputeless/Xoshiro-cpp)

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
- 7 个引擎全覆盖
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

> **CharSet 枚举**：`Alphanumeric` / `Alpha` / `Lower` / `Upper` / `Digit` / `Hex` / `Printable` / `Base64`，覆盖常见字符集场景。

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

### vcpkg

```bash
# 自定义注册表方式
vcpkg install randx --overlay-ports=path/to/ports
```

### Conan

```bash
conan create . --version=1.2.0
```

```python
# conanfile.txt
[requires]
randx/1.2.0

[generators]
CMakeDeps
CMakeToolchain
```

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

## CI

每次 push 自动在以下环境编译 + 运行测试：

| 编译器         | 平台           | 标准                    |
| ----------- | ------------ | --------------------- |
| GCC 14      | Ubuntu 24.04 | C++17 / C++20 / C++23 |
| Clang 18    | Ubuntu 24.04 | C++17 / C++20 / C++23 |
| MSVC (v145) | Windows 2025 | C++17 / C++23         |

C++20 矩阵专门覆盖 `char8_t` 条件编译路径。覆盖率任务使用 lcov，阈值 80%。

## 致谢

- 算法设计：David Blackman & Sebastiano Vigna
- 原始 C++ 封装：Ryo Suzuki ([Xoshiro-cpp](https://github.com/Reputeless/Xoshiro-cpp), MIT License)
