# RandX

基于 **xoshiro/xoroshiro** 算法族的纯头文件伪随机数生成器库。

原始算法：[David Blackman & Sebastiano Vigna](http://prng.di.unimi.it/)
原始 C++ 封装：[Ryo Suzuki (Xoshiro-cpp)](https://github.com/Reputeless/Xoshiro-cpp)

> **⚠️ 安全声明**
>
> xoshiro / xoroshiro / SFC64 / RomuDuoJr / SplitMix64 引擎**均非 CSPRNG**，不可用于密码、密钥、会话 token 等安全场景。
> 安全场景请使用从 OS 熵初始化的 **`ChaCha20()`** 或 **`SecureRandomBytes()`**。显式 64 位种子构造的 ChaCha20 用于测试与复现；直接提供 key／nonce 时，调用方负责其安全来源。

## 版本选择

| 头文件 | 标准 | 命名空间 | 说明 |
|--------|------|----------|------|
| **RandX.hpp** | C++23 | `RandX` | Concepts 约束、8 引擎、便捷 API、编译期随机与 ranges 接口 |
| **RandX_Cpp17.hpp** | C++17 | `RandX` | SFINAE 约束、相同的 8 引擎及共同便捷 API |

两版本的共同算法由同一维护来源生成。相同种子和状态下，引擎原始输出一致；共同便捷 API 的跨标准行为由契约测试与 parity 验证。依赖标准库分布的结果还受标准库实现影响。

## API 参考文档

完整签名见 [Doxygen API 参考](https://lidaixingchen.github.io/RandX/)，调用方式与参数行为见 [API 详细参考](docs/API.md)。

本地生成：`doxygen Doxyfile`，输出到 `docs/api/html/`。

## 特性

**引擎**

- 8 引擎全覆盖（7 统计 PRNG + 1 ChaCha20 CSPRNG），含 SFC64 / RomuDuoJr 高速引擎
- 满足统一随机位生成器要求；C++23 使用 Concepts，C++17 使用 SFINAE
- 状态构造与恢复遵循各引擎的状态政策，Debug／Release 行为一致

**便捷 API**

- 基础生成 / 17 种统计分布 / 容器操作 / 字符串与 UUID / ranges 风格（仅 C++23）
- 线程局部默认引擎 `DefaultEngine()`，零配置即用；也支持传入自定义引擎
- `RandCanonical<T>` 提供 `[0,1)` 浮点采样；float／double 对满位宽引擎使用直通位提取，`RandReal(0,1)` 复用该路径
- `RandTriangular(min, peak, max)` 按最小值、众数和最大值采样三角分布，支持显式引擎重载

**编译期**

- PRNG 支持 `constexpr` 操作；`RandIntCE`、`ShuffleCE` / `ShuffledArray` 提供编译期采样与洗牌（仅 RandX.hpp）

**并行与状态**

- 支持跳跃的引擎提供 `jump()` / `longJump()` 与 `MakeStreamEngine` 多流接口；流间隔及每流消耗预算取决于引擎
- PRNG 提供 `serialize()` / `deserialize()`；数组状态引擎另提供 `operator<<` / `operator>>`
- `discard(n)` 跳过

**安全**

- ChaCha20（RFC 8439）：默认构造从 OS 熵播种，自动模式输出达到重播种阈值后重新获取 OS 熵
- `SecureRandomBytes()` / `SecureSeed()` 直接使用跨平台 OS 密码学熵源：BCryptGenRandom / getrandom / SecRandomCopyBytes，读取失败抛异常

**播种**

- `RandomSeed()` 用于通用 PRNG 播种，依次尝试硬件随机、OS 熵、`std::random_device` 与最终兜底
- 7 个 PRNG 支持 `std::seed_seq`；`Reseed(seed)` 重置当前线程默认引擎以复现测试

**工程化**

- 消费者无需第三方库，CMake `find_package` / `FetchContent` 自动处理平台链接依赖；提供 vcpkg overlay 与本地 xmake 仓库
- 共同源码生成、共享契约测试、独立熵源故障测试与集中性能门禁；验证配置见[开发与验证](docs/开发与验证.md)

## 引擎一览

| 引擎 | 输出 | 状态数据 | 主要能力与场景 |
|------|------|----------|----------------|
| Xoshiro256StarStar | 64-bit | 32B | 默认通用 PRNG；支持 jump／longJump |
| Xoroshiro128StarStar | 64-bit | 16B | 较小状态；支持 jump／longJump |
| Xoshiro128StarStar | 32-bit | 16B | 32 位输出；支持 jump／longJump |
| Xoroshiro64StarStar | 32-bit | 8B | 小状态的 32 位输出 |
| SplitMix64 | 64-bit | 8B | 种子扩展与轻量随机序列 |
| SFC64 | 64-bit | 32B | 计数器结构的高速 PRNG |
| RomuDuoJr | 64-bit | 16B | 精简结构的高速 PRNG |
| **ChaCha20** | 64-bit | 48B 状态＋64B 缓存 | OS 熵播种与密码学随机输出 |

状态数据不等于对象大小；ChaCha20 还包含缓存位置、重播种计数及生命周期标记，实际对象布局由平台决定。各引擎实例需由单线程独立使用，默认引擎为线程局部对象。

## 快速上手

```cpp
#include "RandX.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

int main()
{
    constexpr int DieFirstFace = 1;
    constexpr int DieLastFace = 6;
    constexpr std::size_t LabelLength = 16;
    constexpr std::size_t KeyLength = 32;
    constexpr std::uint64_t PrimaryStreamId = 0;
    constexpr std::uint64_t SecondaryStreamId = 1;

    // 普通模拟与随机标签
    std::cout << RandX::RandInt(DieFirstFace, DieLastFace) << '\n';
    std::cout << RandX::RandNormal() << '\n';        // 正态分布 N(0,1)
    std::cout << RandX::RandString(LabelLength) << '\n';
    std::cout << RandX::RandUUID() << '\n';          // UUID v4

    // 手动管理引擎
    RandX::Xoshiro256StarStar rng{ RandX::RandomSeed() };
    std::cout << RandX::RandInt(rng, DieFirstFace, DieLastFace) << '\n';
    rng.jump();  // 前进 2^128 步（并行子序列）

    // 多流并行
    auto stream0 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(PrimaryStreamId);
    auto stream1 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(SecondaryStreamId);
    std::cout << stream0() << ' ' << stream1() << '\n';

    // 编译期随机（仅 C++23）
    constexpr int dice = RandX::RandIntCE(DieFirstFace, DieLastFace);
    static_assert(dice >= DieFirstFace && dice <= DieLastFace);

    // 密码学安全
    RandX::ChaCha20 csprng;  // OS 熵自动播种
    std::cout << RandX::RandUUID(csprng) << '\n';
    std::array<std::uint8_t, KeyLength> key{};
    RandX::SecureRandomBytes(key.data(), key.size());
}
```

更多示例见 [examples/](examples/) 目录（引擎管理、编译期随机、多流并行、三角分布场景模拟、ChaCha20、按权重选取）。

## API 速查

| 分类 | 函数 | 说明 |
|------|------|------|
| 基础生成 | `RandInt(min, max)` | [min, max] 闭区间整数 |
| | `RandCanonical<T>()` / `RandCanonicalDouble()` / `RandCanonicalFloat()` | [0,1) 浮点数 |
| | `RandReal(min, max)` | [min, max) 浮点数 |
| | `RandBool(p)` | 概率 p 为 true |
| | `RandChar(min, max)` / `RandChar(CharSet)` | 随机字符 / 预设字符集 |
| | `RandBits<N>()` | N 位随机整数 |
| 分布 | `RandNormal` `RandExp` `RandPoisson` `RandGamma` `RandBeta` `RandBinomial` `RandBernoulli` `RandLogNormal` `RandGeometric` `RandCauchy` `RandWeibull` `RandExtremeValue` `RandChiSquared` `RandStudentT` `RandFisherF` `RandWeighted` `RandTriangular` | 17 种统计分布与权重选取 |
| 容器 | `RandElement` / `RandSample` / `RandShuffle` / `RandPermutation` / `RandFill` / `RandVector` | 容器版 + 迭代器版 |
| ranges | `ranges::RandElement` / `RandSample` / `RandShuffle` / `RandFill` | 仅 C++23 |
| 字符串 | `RandString(len, charset)` / `RandUUID()` | 随机字符串 / UUID v4 |
| 编译期 | `RandIntCE<T, Seed>(min, max)` / `ShuffleCE` / `ShuffledArray` | 仅 C++23 |
| 序列化 | `serialize()` / `deserialize()` | PRNG 状态持久化 |
| | `operator<<` / `operator>>` | 数组状态引擎的流式输入输出 |
| CSPRNG | `ChaCha20()` / `reseed()` / `SecureRandomBytes()` / `SecureSeed()` / `IsOsCryptoEntropyAvailable()` | 密码学安全 |
| 引擎控制 | `jump()` / `longJump()` / `discard(n)` / `MakeStreamEngine` / `Reseed(seed)` / `RandomSeed()` | 并行与播种 |

> CharSet 枚举：`Alphanumeric` / `Alpha` / `Lower` / `Upper` / `Digit` / `Hex` / `Printable` / `Base64` / `Base64UrlSafe`。
> 普通运行时采样的默认重载使用线程局部 `Xoshiro256StarStar`，也提供显式引擎重载，例如 `RandInt(rng, min, max)`。安全接口直接使用 OS 熵；编译期随机使用模板种子。

`RandTriangular(min, peak, max)` 将 `peak` 作为众数，要求三个有限参数满足 `min <= peak <= max`。非退化结果位于 `[min, max)`；三个参数相等时原样返回且不消耗引擎。完整参数边界、浮点条件及构建配置见 [API 参考](docs/API.md)。

完整签名与参数说明见 [Doxygen API 参考](https://lidaixingchen.github.io/RandX/)。

## 安全选型

| 场景 | 推荐 | 理由 |
|------|------|------|
| 模拟 / 游戏 / 蒙特卡洛 | `Xoshiro256StarStar` | 默认通用引擎 |
| 大量普通随机生成 | `SFC64` / `RomuDuoJr` | 根据目标工作负载测量选型 |
| 并行子序列 | `MakeStreamEngine` + jump | 按引擎的间隔与消耗预算划分状态子序列 |
| **密码 / 密钥 / token** | **OS 熵初始化的 `ChaCha20()`** 或 **`SecureRandomBytes()`** | 可信熵源初始化与密码学随机输出 |

> 核心判别：如果"输出可预测"会造成损害 → 必须用 CSPRNG；否则用统计 PRNG。
> `IsOsCryptoEntropyAvailable()` 是编译期平台能力查询，不检测运行时熵源健康状况。返回 false 表示当前目标缺少支持的 OS 密码学接口；安全接口读取失败抛 `std::runtime_error`。通用 `RandomSeed()` 的回退链用于普通 PRNG 播种。

ChaCha20 默认构造启用自动重播种，阈值为输出 2^20 字节；显式种子和直接 key／nonce 构造关闭自动重播种。`reseed()` 从 OS 熵更新密钥和 nonce，保持实例原有的自动重播种设置。默认 `RandString()`、`RandUUID()` 使用普通 PRNG，安全场景传入可信初始化的 ChaCha20。

ChaCha20 生命周期中的敏感材料擦除由头文件后端处理。CMake 的 `RANDX_USE_PORTABLE_SECURE_WIPE` 与 `RANDX_USE_APPLE_MEMSET_S` 默认为 `OFF`；启用后选项定义随对应 RandX INTERFACE 目标传播。一个程序内所有包含 RandX 的翻译单元必须使用相同设置；Apple 的 `memset_s` 选项还统一定义 `__STDC_WANT_LIB_EXT1__=1`。直接包含头文件时应在整个程序的构建配置中一致设置这些宏，详细说明见 [API 参考](docs/API.md)。

## 安装与集成

### CMake FetchContent

选择与头文件对应的 CMake 目标，自动传播语言标准及平台链接依赖：

```cmake
include(FetchContent)
FetchContent_Declare(RandX
    GIT_REPOSITORY https://github.com/lidaixingchen/RandX.git
    GIT_TAG v1.5.0
)
FetchContent_MakeAvailable(RandX)
target_link_libraries(myapp PRIVATE RandX::Cpp23)
```

使用 `RandX_Cpp17.hpp` 时选择 `RandX::Cpp17`。通用目标 `RandX::RandX` 的最低标准为 C++17，使用 `RandX.hpp` 的消费者需另外启用 C++23。

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
find_package(RandX CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE RandX::Cpp23)
```

维护者发布流程见 [docs/RELEASING.md](docs/RELEASING.md)。

## 开发与测试

维护流程与可执行命令见[开发与验证](docs/开发与验证.md)，覆盖共同源码生成、两标准测试、固定重排、契约注册核对、熵源故障、覆盖率、性能门禁与消费者构建。统计质量测试由 [PractRand 工作流](.github/workflows/practrand-nightly.yml)执行。

生产头文件从 `src/header_sources/` 生成，仓库提交包含可独立使用的两份产物。维护工具使用 Python；消费者无需生成器。功能规划与完成状态见[路线图](docs/ROADMAP.md)。

## 编译要求

| 头文件 | 最低语言标准 | 编译模式 |
|--------|--------------|----------|
| RandX.hpp | C++23 | GCC／Clang 的 C++23 模式；MSVC 使用支持当前特性的 C++23 模式 |
| RandX_Cpp17.hpp | C++17 | 对应编译器的 C++17 或更高标准模式 |

验证工具链以 [CI 配置](.github/workflows/ci.yml)为准，MSVC 的项目 C++23 测试使用 `/std:c++23preview`。直接集成时 Windows 链接 bcrypt，macOS 链接 Security framework，Linux 无额外链接依赖；CMake 目标自动处理这些依赖。

## 变更记录

完整版本演进见 [CHANGELOG.md](CHANGELOG.md)。

## 致谢

- 算法设计：David Blackman & Sebastiano Vigna
- 原始 C++ 封装：Ryo Suzuki ([Xoshiro-cpp](https://github.com/Reputeless/Xoshiro-cpp), MIT License)
