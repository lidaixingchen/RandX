# Pseudo-random-number-generator-based-on-Xoshiro

基于 **xoshiro/xoroshiro** 算法族的纯头文件伪随机数生成器库。

原始算法：[David Blackman & Sebastiano Vigna](http://prng.di.unimi.it/)
原始 C++ 封装：[Ryo Suzuki (Xoshiro-cpp)](https://github.com/Reputeless/Xoshiro-cpp)

## 版本选择

本仓库提供两个头文件，按需选用：

| 头文件 | 标准 | 命名空间 | 说明 |
|--------|------|----------|------|
| **Random.hpp** | C++23 | `xoshiro` | 增强版：concepts 约束、便捷 API（`RandInt`/`RandReal`/`RandBool`/`RandElement`）、线程局部默认引擎、`discard(n)`、Lemire 无偏 `RandIntCE`、多流接口、14 引擎、中文注释 |
| **XoshiroCpp.hpp** | C++17 / C++20 | `XoshiroCpp` | 兼容版：SFINAE 约束、便捷 API、`discard(n)`、14 引擎、中文注释 |

> 如果你的编译器支持 C++23，推荐使用 `Random.hpp`；否则使用 `XoshiroCpp.hpp`。
> 两者算法实现完全一致，输出序列相同（相同种子下），便捷 API 签名一致。

## 特性

- 满足 `std::uniform_random_bit_generator` 概念（Random.hpp 含 `static_assert` 编译期验证）
- 全 `constexpr`，编译期可用
- 便捷 API：基础生成（`RandInt`/`RandReal`/`RandBool`/`RandBits`）、分布（`RandNormal`/`RandExp`/`RandPoisson`/`RandGamma`/`RandWeighted`）、容器（`RandElement`/`RandSample`/`RandShuffle`/`RandPermutation`）、字符串（`RandString`/`RandUUID`）
- 线程局部默认引擎（`DefaultEngine()`），零配置即用
- `constexpr` 编译期随机（仅 Random.hpp）：`RandIntCE<Seed>(min, max)`
- 支持 `jump()` / `longJump()` 并行子序列、`discard(n)` 跳过、`serialize()` / `deserialize()` 状态持久化
- Lemire 有界法消除模偏差（`RandIntCE`）
- 全零吸收态防御（`assert` 检测）
- 多流接口 `MakeStreamEngine<Engine>(streamId, seed)` 用于并行计算
- 新增 SFC64 / RomuDuoJr 高速引擎
- 14 个引擎全覆盖
- 编译期洗牌 `ShuffleCE` / `ShuffledArray`（`std::shuffle` 的 constexpr 替代）
- `Reseed(seed)` 重置默认引擎，方便测试复现
- `std::seed_seq` 播种支持（所有 14 引擎）
- 硬件种子：`RandomSeed()` 优先使用 RDRAND（x86_64）
- MSVC 兼容：`RandIntCE` 条件编译（`__uint128_t` / 拒绝采样回退）
- CMake 安装支持：`find_package(xoshiro)` / `FetchContent`
- 包管理器支持：vcpkg / Conan
- GitHub Actions CI：GCC 14 / Clang 18 / MSVC 三编译器矩阵

## 引擎一览

| 引擎 | 输出 | 周期 | 状态 | 适用场景 |
|------|------|------|------|----------|
| Xoshiro256StarStar | 64-bit | 2^256-1 | 32B | 通用首选，统计质量最优 |
| Xoshiro256PlusPlus | 64-bit | 2^256-1 | 32B | 通用，略快于 ** |
| Xoshiro256Plus | 64-bit | 2^256-1 | 32B | 最快，低位质量稍弱 |
| Xoroshiro128PlusPlus | 64-bit | 2^128-1 | 16B | 内存受限场景 |
| Xoroshiro128StarStar | 64-bit | 2^128-1 | 16B | 同上，统计更优 |
| Xoroshiro128Plus | 64-bit | 2^128-1 | 16B | 同上，最快 |
| Xoshiro128PlusPlus | 32-bit | 2^128-1 | 16B | 32 位平台 |
| Xoshiro128StarStar | 32-bit | 2^128-1 | 16B | 32 位平台，统计更优 |
| Xoshiro128Plus | 32-bit | 2^128-1 | 16B | 32 位平台，最快 |
| Xoroshiro64StarStar | 32-bit | 2^64-1 | 8B | 极端内存受限 |
| Xoroshiro64Star | 32-bit | 2^64-1 | 8B | 极端内存受限，最快 |
| SplitMix64 | 64-bit | 2^64 | 8B | 种子扩展 / 哈希，非通用 PRNG |
| SFC64 | 64-bit | >= 2^64 | 32B | 速度极快，通过 PractRand，无 jump |
| RomuDuoJr | 64-bit | >= 2^51 (估计) | 16B | 极简极快，非关键模拟，无 jump |

## 快速上手

```cpp
#include "Random.hpp"
#include <iostream>

int main()
{
    // 最简用法：便捷函数（内部使用线程局部 Xoshiro256StarStar）
    std::cout << xoshiro::RandInt(1, 6) << '\n';   // 掷骰子 [1, 6]
    std::cout << xoshiro::RandReal() << '\n';       // [0.0, 1.0)
    std::cout << xoshiro::RandBool(0.3) << '\n';    // 30% 概率为 true
    std::cout << xoshiro::RandNormal() << '\n';     // 正态分布 N(0,1)
}
```

## API 一览

| 分类 | 函数 | 说明 |
|------|------|------|
| 基础生成 | `RandInt(min, max)` | [min, max] 闭区间整数 |
| | `RandReal(min, max)` | [min, max) 浮点数 |
| | `RandBool(p)` | 概率 p 为 true |
| | `RandBits<N>()` | N 位随机整数 [0, 2^N) |
| 分布 | `RandNormal(mean, stddev)` | 正态分布 |
| | `RandExp(lambda)` | 指数分布 |
| | `RandPoisson(mean)` | 泊松分布 |
| | `RandGamma(alpha, beta)` | 伽马分布 |
| | `RandWeighted(weights)` | 按权重选取索引 |
| 容器 | `RandElement(container)` | 随机取一个元素 |
| | `RandSample(container, n)` | 无放回抽样 n 个 |
| | `RandShuffle(container)` | 随机打乱 |
| | `RandPermutation(n)` | [0, n) 随机排列 |
| 字符串/ID | `RandString(len, charset)` | 随机字符串 |
| | `RandUUID()` | UUID v4 |
| 编译期 | `RandIntCE<Seed>(min, max)` | 编译期随机整数（仅 Random.hpp） |
| | `ShuffleCE(first, last)` | 编译期洗牌 |
| | `ShuffledArray<T,N,Seed>(arr)` | 编译期洗牌数组版 |

所有函数默认使用线程局部 `Xoshiro256StarStar` 引擎，也支持传入自定义引擎：`RandInt(rng, min, max)`。

```cpp
// 示例
xoshiro::RandInt(1, 6);              // 掷骰子
xoshiro::RandExp(2.0);               // 指数分布 λ=2
xoshiro::RandSample(pool, 3);        // 无放回抽 3 个
xoshiro::RandString(16);             // 16 位随机 token
xoshiro::RandUUID();                 // "a1b2c3d4-e5f6-4789-abcd-..."
constexpr auto v = xoshiro::RandIntCE(0, 100);  // 编译期确定
```

## 手动管理引擎

```cpp
#include "Random.hpp"
#include <random>
#include <iostream>

int main()
{
    // 用真随机种子创建引擎
    xoshiro::Xoshiro256StarStar rng{ xoshiro::RandomSeed() };

    // 指定引擎的便捷函数
    std::cout << xoshiro::RandInt(rng, 0, 99) << '\n';

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

## 编译期随机（仅 Random.hpp）

```cpp
#include "Random.hpp"

// 编译期确定结果（固定种子）
constexpr int compileTimeDice = xoshiro::RandIntCE(1, 6);
static_assert(compileTimeDice >= 1 && compileTimeDice <= 6);

// 自定义种子
constexpr auto v = xoshiro::RandIntCE<int, 42>(0, 100);
```

## 按权重随机选取

```cpp
#include "Random.hpp"
#include <vector>
#include <iostream>

int main()
{
    // 权重：A=1, B=2, C=7 → C 被选中概率约 70%
    std::vector<double> weights = { 1.0, 2.0, 7.0 };
    auto idx = xoshiro::RandWeighted(weights);
    std::cout << "选中索引: " << idx << '\n';
}
```

## 多流并行

```cpp
#include "Random.hpp"
#include <iostream>

int main()
{
    // 创建 4 个不重叠的子序列引擎（每个间隔 2^128 步）
    auto rng0 = xoshiro::MakeStreamEngine<xoshiro::Xoshiro256StarStar>(0);
    auto rng1 = xoshiro::MakeStreamEngine<xoshiro::Xoshiro256StarStar>(1);
    auto rng2 = xoshiro::MakeStreamEngine<xoshiro::Xoshiro256StarStar>(2);
    auto rng3 = xoshiro::MakeStreamEngine<xoshiro::Xoshiro256StarStar>(3);

    // 各流独立生成，互不重叠
    std::cout << xoshiro::RandInt(rng0, 0, 99) << '\n';
    std::cout << xoshiro::RandInt(rng1, 0, 99) << '\n';
}
```

## 编译期洗牌

```cpp
#include "Random.hpp"
#include <array>
#include <iostream>

int main()
{
    // 编译期 Fisher-Yates 洗牌
    constexpr auto shuffled = [] {
        std::array<int, 10> a = {0,1,2,3,4,5,6,7,8,9};
        xoshiro::ShuffleCE(a.begin(), a.end());
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
FetchContent_Declare(xoshiro
    GIT_REPOSITORY https://github.com/lidaixingchen/Pseudo-random-number-generator-based-on-Xoshiro.git
    GIT_TAG master
)
FetchContent_MakeAvailable(xoshiro)
target_link_libraries(myapp PRIVATE xoshiro::xoshiro)
```

### find_package

```bash
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
cmake --install .
```

```cmake
find_package(xoshiro REQUIRED)
target_link_libraries(myapp PRIVATE xoshiro::xoshiro)
```

### 构建测试

```bash
cmake -B build -DXOSHIRO_BUILD_TESTS=ON -DXOSHIRO_BUILD_BENCHMARK=ON
cmake --build build
ctest --test-dir build
```

## 包管理器

### vcpkg

```bash
# 自定义注册表方式
vcpkg install xoshiro-cpp --overlay-ports=path/to/ports
```

### Conan

```bash
conan create . --version=2.0.0
```

```python
# conanfile.txt
[requires]
xoshiro-cpp/2.0.0

[generators]
CMakeDeps
CMakeToolchain
```

## 编译要求

| 头文件 | 最低标准 | 推荐编译器 |
|--------|----------|------------|
| Random.hpp | C++23 | GCC 14+ / Clang 18+ / MSVC 17.10+ |
| XoshiroCpp.hpp | C++17 | GCC 9+ / Clang 10+ / MSVC 16.8+ |

- 无外部依赖，纯头文件，复制即用

## 测试

仓库包含确定性单元测试（固定种子 → 断言已知输出序列）：

```bash
# C++23 测试
g++ -std=c++23 -Wall -Wextra -o test_random test_random.cpp && ./test_random

# C++17 测试
g++ -std=c++17 -Wall -Wextra -o test_xoshirocpp test_xoshirocpp.cpp && ./test_xoshirocpp
```

## CI

每次 push 自动在以下环境编译 + 运行测试：

| 编译器 | 平台 | 标准 |
|--------|------|------|
| GCC 14 | Ubuntu 24.04 | C++17 / C++23 |
| Clang 18 | Ubuntu 24.04 | C++17 / C++23 |
| MSVC (v145) | Windows 2025 | C++17 / C++23 |

## 致谢

- 算法设计：David Blackman & Sebastiano Vigna
- 原始 C++ 封装：Ryo Suzuki ([Xoshiro-cpp](https://github.com/Reputeless/Xoshiro-cpp), MIT License)
