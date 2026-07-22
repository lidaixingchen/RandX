# RandX

基于 **xoshiro/xoroshiro** 算法族的纯头文件伪随机数生成器库。

原始算法：[David Blackman & Sebastiano Vigna](http://prng.di.unimi.it/)
原始 C++ 封装：[Ryo Suzuki (Xoshiro-cpp)](https://github.com/Reputeless/Xoshiro-cpp)

> **⚠️ 安全声明**
>
> xoshiro / xoroshiro / SFC64 / RomuDuoJr / SplitMix64 引擎**均非 CSPRNG**，不可用于密码、密钥、会话 token 等安全场景。
> 安全场景请使用 **`ChaCha20`** 引擎（RFC 8439）或 **`SecureRandomBytes()`**。

## 版本选择

| 头文件 | 标准 | 命名空间 | 说明 |
|--------|------|----------|------|
| **RandX.hpp** | C++23 | `RandX` | 增强版：concepts 约束、编译期随机、ranges 风格 API、多流接口 |
| **RandX_Cpp17.hpp** | C++17 | `RandX` | 兼容版：SFINAE 约束、便捷 API、7 引擎 |

两者算法实现完全一致，相同种子下输出序列相同。

## API 参考文档

完整 Doxygen API 参考：**https://lidaixingchen.github.io/RandX/**

本地生成：`doxygen Doxyfile`，输出到 `docs/api/html/`。

## 特性

**引擎**
- 8 引擎全覆盖（7 统计 PRNG + 1 ChaCha20 CSPRNG），含 SFC64 / RomuDuoJr 高速引擎
- 满足 `std::uniform_random_bit_generator` 概念（编译期 `static_assert` 验证）
- Lemire 有界法消除模偏差，全零吸收态 `assert` 防御

**便捷 API**
- 基础生成 / 16 种统计分布 / 容器操作 / 字符串与 UUID / ranges 风格（仅 C++23）
- 线程局部默认引擎 `DefaultEngine()`，零配置即用；也支持传入自定义引擎

**编译期**
- 全 `constexpr` 引擎 + `RandIntCE`（Lemire 无偏）+ `ShuffleCE` / `ShuffledArray`（仅 RandX.hpp）

**并行与状态**
- `jump()` / `longJump()` 不重叠子序列 + `MakeStreamEngine` 多流接口
- `serialize()` / `deserialize()` 状态持久化 + `operator<<` / `operator>>` 流式语法糖
- `discard(n)` 跳过

**安全**
- ChaCha20 CSPRNG（RFC 8439）：OS 熵自动播种，2^20 字节自动 reseed 前向安全
- `SecureRandomBytes()` 跨平台 OS 熵源（BCryptGenRandom / getrandom / SecRandomCopyBytes），RDRAND 可选加速

**播种**
- `RandomSeed()` 硬件种子（优先 RDRAND）、`std::seed_seq` 支持（所有 7 引擎）、`Reseed(seed)` 测试复现

**工程化**
- 纯头文件零依赖，CMake `find_package` / `FetchContent`，vcpkg / Conan 包管理器
- GitHub Actions CI：GCC 14 / Clang 18 / MSVC 三编译器矩阵；MSVC `RandIntCE` 条件编译兼容

## 引擎一览

| 引擎 | 输出 | 周期 | 状态 | 适用场景 |
|------|------|------|------|----------|
| Xoshiro256StarStar | 64-bit | 2^256-1 | 32B | 通用首选，统计质量最优 |
| Xoroshiro128StarStar | 64-bit | 2^128-1 | 16B | 内存受限，统计更优 |
| Xoshiro128StarStar | 32-bit | 2^128-1 | 16B | 32 位平台 |
| Xoroshiro64StarStar | 32-bit | 2^64-1 | 8B | 极端内存受限 |
| SplitMix64 | 64-bit | 2^64 | 8B | 种子扩展 / 哈希 |
| SFC64 | 64-bit | >= 2^64 | 32B | 速度极快，无 jump |
| RomuDuoJr | 64-bit | >= 2^51 | 16B | 极简极快，无 jump |
| **ChaCha20** | 64-bit | 无周期 | 56B | **密码学安全**，RFC 8439 |

## 快速上手

```cpp
#include "RandX.hpp"
#include <iostream>

int main()
{
    // 便捷函数（内部使用线程局部 Xoshiro256StarStar）
    std::cout << RandX::RandInt(1, 6) << '\n';       // 掷骰子
    std::cout << RandX::RandNormal() << '\n';        // 正态分布 N(0,1)
    std::cout << RandX::RandString(16) << '\n';      // 16 位随机 token
    std::cout << RandX::RandUUID() << '\n';          // UUID v4

    // 手动管理引擎
    RandX::Xoshiro256StarStar rng{ RandX::RandomSeed() };
    std::cout << RandX::RandInt(rng, 0, 99) << '\n';
    rng.jump();  // 前进 2^128 步（并行子序列）

    // 多流并行
    auto stream0 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(0);
    auto stream1 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(1);

    // 编译期随机（仅 C++23）
    constexpr int dice = RandX::RandIntCE(1, 6);
    static_assert(dice >= 1 && dice <= 6);

    // 密码学安全
    RandX::ChaCha20 csprng;  // OS 熵自动播种
    std::uint8_t key[32];
    RandX::SecureRandomBytes(key, sizeof(key));
}
```

更多示例见 [examples/](examples/) 目录（引擎管理、编译期随机、多流并行、ChaCha20、按权重选取）。

## 安全选型

| 场景 | 推荐 | 理由 |
|------|------|------|
| 模拟 / 游戏 / 蒙特卡洛 | `Xoshiro256StarStar` | 统计质量最优，速度极快 |
| 性能极致 / 非关键模拟 | `SFC64` / `RomuDuoJr` | 更快，统计质量足够 |
| 并行子序列 | `MakeStreamEngine` + jump | 不重叠子序列 |
| **密码 / 密钥 / token** | **`ChaCha20`** 或 **`SecureRandomBytes()`** | CSPRNG，状态不可逆推 |

> 核心判别：如果"输出可预测"会造成损害 → 必须用 CSPRNG；否则用统计 PRNG。
> 降级检测：`IsOsCryptoEntropyAvailable()` 返回 false 时表示走 `std::random_device` 兜底，非密码学安全。

## 安装与集成

### CMake FetchContent（推荐）

```cmake
include(FetchContent)
FetchContent_Declare(RandX
    GIT_REPOSITORY https://github.com/lidaixingchen/RandX.git
    GIT_TAG v1.3.1
)
FetchContent_MakeAvailable(RandX)
target_link_libraries(myapp PRIVATE RandX::RandX)
```

### vcpkg（overlay 模式）

```bash
vcpkg install randx --overlay-ports=path/to/this/repo/ports
```

### xrepo / xmake

```bash
xrepo add-repo local-randx path/to/this/repo/packaging/xmake-repo
xrepo install randx
```

### find_package

```cmake
find_package(RandX REQUIRED)
target_link_libraries(myapp PRIVATE RandX::RandX)
```

维护者发布流程见 [docs/RELEASING.md](docs/RELEASING.md)。

## 基准测试

```bash
# 零依赖快速检查
g++ -std=c++23 -O2 benchmark.cpp -o benchmark && ./benchmark

# 结构化基准套件（Google Benchmark，含 CI 性能回归）
cmake -B build -DRANDX_BUILD_BENCHMARK=ON
cmake --build build --target benchmark_gbench
./build/benchmark_gbench --benchmark_format=json --benchmark_repetitions=5
```

## 开发与测试

```bash
# C++23 测试
g++ -std=c++23 -Wall -Wextra -I third_party -o test_randx test_randx.cpp -lbcrypt && ./test_randx

# C++17 测试
g++ -std=c++17 -Wall -Wextra -I third_party -o test_randx_cpp17 test_randx_cpp17.cpp -lbcrypt && ./test_randx_cpp17
```

> Windows 需链接 `-lbcrypt`；macOS 需 `-framework Security`；Linux 无需额外链接。使用 CMake 时自动处理。

CI 矩阵：GCC 14 / Clang 18 / MSVC (v145)，覆盖 C++17 / C++20 / C++23。另有 PractRand nightly job 验证 8 引擎统计质量。

## 编译要求

| 头文件 | 最低标准 | 推荐编译器 |
|--------|----------|------------|
| RandX.hpp | C++23 | GCC 14+ / Clang 18+ / MSVC 17.10+ |
| RandX_Cpp17.hpp | C++17 | GCC 9+ / Clang 10+ / MSVC 16.8+ |

无外部依赖，纯头文件，复制即用。

## 变更记录

完整版本演进见 [CHANGELOG.md](CHANGELOG.md)。

## 致谢

- 算法设计：David Blackman & Sebastiano Vigna
- 原始 C++ 封装：Ryo Suzuki ([Xoshiro-cpp](https://github.com/Reputeless/Xoshiro-cpp), MIT License)
