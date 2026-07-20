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
- 便捷 API：`RandInt` / `RandReal` / `RandBool` / `RandElement` / `RandNormal` / `RandShuffle` / `RandWeighted`
- 线程局部默认引擎（`DefaultEngine()`），零配置即用
- `constexpr` 编译期随机（仅 Random.hpp）：`RandIntCE<Seed>(min, max)`
- 支持 `jump()` / `longJump()` 并行子序列、`discard(n)` 跳过、`serialize()` / `deserialize()` 状态持久化
- Lemire 有界法消除模偏差（`RandIntCE`）
- 全零吸收态防御（`assert` 检测）
- 多流接口 `MakeStreamEngine<Engine>(streamId, seed)` 用于并行计算
- 新增 SFC64 / RomuDuoJr 高速引擎
- 14 个引擎全覆盖

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

## 便捷 API 一览

| 函数 | 说明 |
|------|------|
| `RandInt(min, max)` | [min, max] 闭区间整数 |
| `RandInt(max)` | [0, max] 闭区间整数 |
| `RandReal(min, max)` | [min, max) 浮点数 |
| `RandBool(p)` | 概率 p 为 true |
| `RandElement(container)` | 从容器随机取一个元素 |
| `RandNormal(mean, stddev)` | 正态分布 |
| `RandShuffle(container)` | 随机打乱容器 |
| `RandWeighted(weights)` | 按权重选取索引 |
| `RandInt(rng, min, max)` | 指定引擎版本 |
| `RandReal(rng, min, max)` | 指定引擎版本 |
| `RandIntCE<Seed>(min, max)` | 编译期随机（仅 Random.hpp） |

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

## 基准测试

编译并运行 benchmark.cpp：

```bash
g++ -std=c++23 -O2 benchmark.cpp -o benchmark && ./benchmark
```

输出各引擎的吞吐量（Mops/s、MB/s）、jump 开销和 RandInt API 性能。

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

## 致谢

- 算法设计：David Blackman & Sebastiano Vigna
- 原始 C++ 封装：Ryo Suzuki ([Xoshiro-cpp](https://github.com/Reputeless/Xoshiro-cpp), MIT License)
