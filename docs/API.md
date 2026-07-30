# RandX API 详细参考

> 本页为完整 API 签名与参数说明。快速概览见 [README.md](../README.md) 的 API 速查表。
> 所有函数默认使用线程局部 `Xoshiro256StarStar` 引擎，也支持传入自定义引擎：`RandInt(rng, min, max)`。

---

## 基础生成

### RandInt

```cpp
template <std::integral T = int>
[[nodiscard]] inline T RandInt(T min, T max);

template <std::integral T = int>
[[nodiscard]] inline T RandInt(T max);  // [0, max]

template <std::integral T, class Engine>
[[nodiscard]] inline T RandInt(Engine& engine, T min, T max);
```

| 参数 | 说明 |
|------|------|
| `min` | 下界（含） |
| `max` | 上界（含） |
| `engine` | 自定义引擎 |

返回均匀分布于 [min, max] 的随机整数。内部使用 `std::uniform_int_distribution<T>`。

### RandReal

```cpp
template <std::floating_point T = double>
[[nodiscard]] inline T RandReal(T min = T{0}, T max = T{1});

template <std::floating_point T, class Engine>
[[nodiscard]] inline T RandReal(Engine& engine, T min = T{0}, T max = T{1});
```

返回均匀分布于 [min, max) 的随机浮点数。

### RandBool

```cpp
[[nodiscard]] inline bool RandBool(double p = 0.5);

template <class Engine>
[[nodiscard]] inline bool RandBool(Engine& engine, double p = 0.5);
```

以概率 p 返回 true。`RandBernoulli` 为其别名（对齐 `<random>` 命名）。

### RandChar

```cpp
// 范围版 [min, max]
template <detail::Character CharT>
[[nodiscard]] inline CharT RandChar(CharT min, CharT max);

// 单上界版 [CharT{}, max]
template <detail::Character CharT = char>
[[nodiscard]] inline CharT RandChar(CharT max);

// 预设字符集版
[[nodiscard]] inline char RandChar(CharSet cs);

// 指定引擎版
template <detail::Character CharT, class Engine>
[[nodiscard]] inline CharT RandChar(Engine& engine, CharT min, CharT max);

template <class Engine>
[[nodiscard]] inline char RandChar(Engine& engine, CharSet cs);
```

`detail::Character` 涵盖 `char` / `wchar_t` / `char16_t` / `char32_t` / `char8_t`（C++20+）。

### RandBits

```cpp
template <int N, std::integral T = std::uint64_t>
    requires (N > 0 && N <= 64)
[[nodiscard]] inline T RandBits() noexcept;
```

返回 [0, 2^N) 的随机整数。N==64 时直接返回 `rng()`，否则掩码截取低 N 位。

---

## 统计分布（16 种）

每个分布均有默认引擎版和指定引擎版（`Engine&` 重载）。

| 函数 | 签名 | 默认参数 | 说明 |
|------|------|----------|------|
| `RandNormal` | `T RandNormal(T mean, T stddev)` | mean=0, stddev=1 | 正态分布 |
| `RandExp` | `T RandExp(T lambda)` | lambda=1 | 指数分布，均值=1/λ |
| `RandPoisson` | `T RandPoisson(double mean)` | mean=1.0 | 泊松分布（整数） |
| `RandGamma` | `T RandGamma(T alpha, T beta)` | alpha=1, beta=1 | 伽马分布 |
| `RandBinomial` | `T RandBinomial(T t, double p)` | t=1, p=0.5 | 二项分布（整数） |
| `RandLogNormal` | `T RandLogNormal(T mean, T stddev)` | mean=0, stddev=1 | 对数正态 |
| `RandGeometric` | `T RandGeometric(double p)` | p=0.5 | 几何分布（整数） |
| `RandCauchy` | `T RandCauchy(T a, T b)` | a=0, b=1 | 柯西分布 |
| `RandWeibull` | `T RandWeibull(T a, T b)` | a=1, b=1 | 韦布尔分布 |
| `RandExtremeValue` | `T RandExtremeValue(T a, T b)` | a=0, b=1 | 极值分布 |
| `RandChiSquared` | `T RandChiSquared(T n)` | n=1 | 卡方分布 |
| `RandStudentT` | `T RandStudentT(T n)` | n=1 | 学生 t 分布 |
| `RandFisherF` | `T RandFisherF(T m, T n)` | m=1, n=1 | Fisher F 分布 |
| `RandBeta` | `T RandBeta(T a, T b)` | a=1, b=1 | Beta 分布（自实现） |
| `RandBernoulli` | `bool RandBernoulli(double p)` | p=0.5 | 伯努利（RandBool 别名） |
| `RandWeighted` | `size_type RandWeighted(const WeightContainer& weights)` | 无 | 按权重选取索引 |

浮点分布模板参数为 `std::floating_point T = double`，整数分布为 `std::integral T = int`。

指定引擎重载模式：`T RandNormal(Engine& engine, T mean = T{0}, T stddev = T{1})`。

`RandBeta` 无 STL 对应，自实现 Gamma(a)/(Gamma(a)+Gamma(b))，含除零保护。

---

## 容器操作

### RandElement

```cpp
// 容器版 — 返回引用
template <class Container>
[[nodiscard]] inline decltype(auto) RandElement(Container&& c);
// 异常: std::invalid_argument（容器为空）

// 迭代器版 — 随机访问 O(1)，返回迭代器
template <std::random_access_iterator It>
[[nodiscard]] inline It RandElement(It first, It last);

// 迭代器版 — 输入迭代器 O(n) reservoir sampling
template <std::input_iterator It>
    requires (!std::random_access_iterator<It>)
[[nodiscard]] inline It RandElement(It first, It last);

// 指定引擎版
template <std::random_access_iterator It, class Engine>
[[nodiscard]] inline It RandElement(Engine& engine, It first, It last);
```

### RandSample

```cpp
// 容器版
template <class Container>
[[nodiscard]] inline auto RandSample(const Container& c, typename Container::size_type n);
// n >= size 时返回全部副本

// 随机访问迭代器版（hash-set / 索引数组双分支）
template <std::random_access_iterator It>
[[nodiscard]] inline std::vector<std::iter_value_t<It>>
RandSample(It first, It last, std::iter_difference_t<It> n);

// 输入迭代器版（reservoir sampling, Algorithm R）
template <std::input_iterator It>
    requires (!std::random_access_iterator<It>)
[[nodiscard]] inline std::vector<std::iter_value_t<It>>
RandSample(It first, It last, std::iter_difference_t<It> n);

// 指定引擎版
template <std::random_access_iterator It, class Engine>
[[nodiscard]] inline std::vector<std::iter_value_t<It>>
RandSample(Engine& engine, It first, It last, std::iter_difference_t<It> n);
```

随机访问版策略：`n * 64 < size` 时用 hash-set（O(n) 内存），否则用索引数组（O(N)）。

### RandShuffle

```cpp
template <class Container>
inline void RandShuffle(Container&& c);  // 原地打乱
```

### RandPermutation

```cpp
[[nodiscard]] inline std::vector<std::size_t> RandPermutation(std::size_t n);
// 返回 [0, n) 的随机排列
```

### RandFill

```cpp
// 整数版 [min, max]
template <class It, class T>
    requires detail::RandFillable<It, T>
inline void RandFill(It first, It last, T min, T max);

// 浮点版 [min, max)
template <class It, std::floating_point T>
    requires std::output_iterator<It, T>
inline void RandFill(It first, It last, T min, T max);

// 指定引擎版
template <class It, class T, class Engine>
inline void RandFill(Engine& engine, It first, It last, T min, T max);
```

T 从 min/max 推导，非从迭代器 value_type 推导。

### RandVector

```cpp
template <std::integral T = int>
[[nodiscard]] inline std::vector<T> RandVector(T min, T max, std::size_t n);

template <std::floating_point T = double>
[[nodiscard]] inline std::vector<T> RandVector(T min, T max, std::size_t n);

// 指定引擎版
template <std::integral T = int, class Engine>
[[nodiscard]] inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n);
```

---

## Ranges 风格（仅 C++23）

```cpp
namespace RandX::ranges
{
    // 随机选取一个元素（返回值拷贝，非迭代器）
    template <std::ranges::input_range R>
        requires std::ranges::sized_range<R> || std::ranges::forward_range<R>
    [[nodiscard]] inline std::ranges::range_value_t<R> RandElement(R&& r);

    // 无放回抽样
    template <std::ranges::input_range R>
    [[nodiscard]] inline std::vector<std::ranges::range_value_t<R>>
    RandSample(R&& r, std::ranges::range_difference_t<R> n);

    // 随机打乱
    template <std::ranges::random_access_range R>
        requires std::ranges::sized_range<R>
    inline void RandShuffle(R&& r);

    // 随机数填充
    template <class T, std::ranges::output_range<const T&> R>
    inline void RandFill(R&& r, T min, T max);
}
```

语义差异：迭代器版 `RandElement` 返回迭代器（可修改原元素），ranges 版返回值拷贝。

---

## 字符串与 ID

### CharSet 枚举

| 枚举值 | 字符集 | 数量 |
|--------|--------|------|
| `Alphanumeric` | [A-Za-z0-9] | 62 |
| `Alpha` | [A-Za-z] | 52 |
| `Lower` | [a-z] | 26 |
| `Upper` | [A-Z] | 26 |
| `Digit` | [0-9] | 10 |
| `Hex` | [0-9a-fA-F] | 16 |
| `Printable` | [!-~] | 94 |
| `Base64` | [A-Za-z0-9+/] | 64 |
| `Base64UrlSafe` | [A-Za-z0-9-_] | 64 |

### RandString

```cpp
// 自定义字符集版
[[nodiscard]] inline std::string RandString(
    std::size_t length,
    std::string_view charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
// 异常: std::invalid_argument（charset 为空）

// 预设字符集版
[[nodiscard]] inline std::string RandString(std::size_t n, CharSet cs);

// 指定引擎版
template <class Engine>
[[nodiscard]] inline std::string RandString(Engine& engine, std::size_t n, CharSet cs);
```

### RandUUID

```cpp
[[nodiscard]] inline std::string RandUUID();
// 返回 UUID v4: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
```

---

## 编译期随机（仅 RandX.hpp）

### RandIntCE

```cpp
template <std::integral T = int, std::uint64_t Seed = DefaultSeed>
[[nodiscard]] inline constexpr T RandIntCE(T min, T max) noexcept;

template <std::integral T = int, std::uint64_t Seed = DefaultSeed>
[[nodiscard]] inline constexpr T RandIntCE(T max) noexcept;  // [0, max]
```

使用 Lemire 有界法，无模偏差。结果由 Seed 模板参数决定。MSVC 条件编译：无 `__uint128_t` 时回退到拒绝采样。

### ShuffleCE

```cpp
template <std::random_access_iterator It, std::uint64_t Seed = DefaultSeed>
constexpr void ShuffleCE(It first, It last) noexcept;
```

`std::shuffle` 在 C++23 中仍非 constexpr，故自实现 Fisher-Yates。

### ShuffledArray

```cpp
template <class T, std::size_t N, std::uint64_t Seed = DefaultSeed>
[[nodiscard]] constexpr std::array<T, N> ShuffledArray(std::array<T, N> arr) noexcept;
```

---

## 序列化

### serialize / deserialize（引擎成员函数）

```cpp
[[nodiscard]] constexpr state_type serialize() const noexcept;
constexpr void deserialize(state_type state) noexcept;
```

| 引擎 | state_type |
|------|-----------|
| SplitMix64 | `std::uint64_t` |
| Xoshiro256StarStar | `std::array<std::uint64_t, 4>` |
| Xoroshiro128StarStar | `std::array<std::uint64_t, 2>` |
| Xoshiro128StarStar | `std::array<std::uint32_t, 4>` |
| Xoroshiro64StarStar | `std::array<std::uint32_t, 2>` |
| SFC64 | `std::array<std::uint64_t, 4>` |
| RomuDuoJr | `std::array<std::uint64_t, 2>` |
| ChaCha20 | **不提供**（CSPRNG 安全约束） |

### 全零状态静默修正

全零状态是 xoshiro/xoroshiro 算法族的吸收态（输出永远为 0）。为防止引擎陷入吸收态，
数组状态引擎（Xoshiro256StarStar / Xoroshiro128StarStar / Xoshiro128StarStar /
Xoroshiro64StarStar / SFC64 / RomuDuoJr）在以下入口会**静默修正**全零输入，
Debug 与 Release 行为一致，无断言触发、无返回值、无可查询标志：

| 入口 | 修正规则 |
|------|--------|
| `EngineBase(state_type)` 状态构造 | 若全零则置 `s_[0] = 1`，其余不变 |
| `deserialize(state_type)` | 若全零则置 `s_[0] = 1`，其余不变 |
| SeedSeq / 单值播种构造 | 播种后若仍全零则置 `s_[0] = 1` |
| `SFC64(seed)` / `SFC64(SeedSeq&)` | 若 3 个状态字均为 0，则置 `s_[0] = 0x9E3779B97F4A7C15`（counter 固定为 1） |

> `SplitMix64` 为标量状态、非吸收态，不参与此修正；`ChaCha20` 不提供状态构造 / deserialize。

修正后的输出序列完全确定（与种子无关），且 C++23 与 C++17 两个头文件一致。以全零输入为例，
各引擎修正后前 3 个输出为：

| 引擎 | 修正后前 3 个输出 |
|------|----------------|
| Xoshiro256StarStar | `0, 5760, 5760` |
| Xoroshiro128StarStar | `5760, 97014257280, 16610091813126018688` |
| Xoshiro128StarStar | `0, 5760, 5760` |
| Xoroshiro64StarStar | `3802928447, 3134575995, 1411955750` |
| SFC64 | `1, 1, 11` |
| RomuDuoJr | `1, 0, 3205649788950522037` |

该行为已在 `test_randx.cpp` 与 `test_randx_cpp17.cpp` 的「全零输入 → 修正后确定序列」断言中回归。

### operator<< / operator>>

```cpp
template <class CharT, class Traits, detail::SerializableEngine Engine>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, const Engine& engine);

template <class CharT, class Traits, detail::SerializableEngine Engine>
std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits>& is, Engine& engine);
```

格式：空格分隔的十进制数序列（兼容 `std::random_engine`）。SplitMix64（标量 state_type）和 ChaCha20 不支持。

---

## CSPRNG

### ChaCha20 构造函数

```cpp
// 方式 1: OS 熵自动播种（密码学安全，推荐）
ChaCha20();

// 方式 2: 显式 64 位种子（仅测试/复现，非密码学安全）
explicit ChaCha20(std::uint64_t seed);

// 方式 3: 直接指定 key + nonce + counter（KAT/高级用法）
ChaCha20(const std::uint8_t* key, std::size_t keyLen,
         const std::uint8_t* nonce, std::size_t nonceLen,
         std::uint32_t counter = 0);
// 异常: std::invalid_argument（keyLen!=32 或 nonceLen!=12）
```

### ChaCha20 成员函数

```cpp
result_type operator()();            // 64-bit 密码学安全随机数
void discard(unsigned long long n);  // 跳过 n 个输出
void reseed();                       // 从 OS 熵重新播种
static constexpr result_type min() noexcept;  // 0
static constexpr result_type max() noexcept;  // UINT64_MAX
```

不提供 serialize/deserialize、operator<</>>、jump/longJump（CSPRNG 安全约束）。
输出 2^20 字节后自动 reseed（前向安全）。非线程安全。

### SecureRandomBytes

```cpp
inline void SecureRandomBytes(void* buf, std::size_t n);
// 异常: std::runtime_error（OS 熵源不可用）
```

### SecureSeed

```cpp
[[nodiscard]] inline std::uint64_t SecureSeed();
```

### IsOsCryptoEntropyAvailable

```cpp
[[nodiscard]] inline bool IsOsCryptoEntropyAvailable() noexcept;
// true = BCryptGenRandom/getrandom/SecRandomCopyBytes 可用
// false = std::random_device 兜底，不保证密码学安全
```

---

## 引擎控制

### jump / longJump

| 引擎 | jump() 步数 | longJump() 步数 |
|------|------------|----------------|
| Xoshiro256StarStar | 2^128 | 2^192 |
| Xoroshiro128StarStar | 2^64 | 2^96 |
| Xoshiro128StarStar | 2^64 | 2^96 |
| 其余 5 引擎 | 无 | 无 |

```cpp
constexpr void jump() noexcept;
constexpr void longJump() noexcept;
```

### discard

```cpp
constexpr void discard(unsigned long long n) noexcept;  // 非 CSPRNG 引擎
void discard(unsigned long long n);                     // ChaCha20
```

### MakeStreamEngine

```cpp
template <class Engine>
    requires detail::StreamEngine<Engine>
[[nodiscard]] inline constexpr Engine
MakeStreamEngine(std::uint64_t streamId, std::uint64_t seed = DefaultSeed);
```

各流间隔 2^128 步（xoshiro256）或 2^64 步（xoroshiro128/xoshiro128）。仅支持满足 `StreamEngine` concept 的引擎。

### Reseed / ReseedRandom

```cpp
inline void Reseed(std::uint64_t seed);   // 重置默认引擎（测试复现）
inline void ReseedRandom();               // 重置为真随机种子
```

### RandomSeed

```cpp
[[nodiscard]] inline std::uint64_t RandomSeed();
// 优先级: RDRAND (x86_64) → OS 熵 → std::random_device → 时间戳
// 永不抛异常
```

### DefaultEngine

```cpp
[[nodiscard]] inline Xoshiro256StarStar& DefaultEngine();
// 线程局部，首次调用时 std::random_device 播种
```

---

## 辅助工具

```cpp
template <std::same_as<std::uint32_t> Uint32>
[[nodiscard]] inline constexpr float FloatFromBits(Uint32 i) noexcept;  // [0.0f, 1.0f)

template <std::same_as<std::uint64_t> Uint64>
[[nodiscard]] inline constexpr double DoubleFromBits(Uint64 i) noexcept;  // [0.0, 1.0)
```

## 常量

```cpp
inline constexpr std::uint64_t DefaultSeed = 1234567890ULL;
```

## Concepts（detail 命名空间）

| Concept | 描述 |
|---------|------|
| `detail::Character<T>` | char / wchar_t / char16_t / char32_t / char8_t |
| `detail::SerializableEngine<E>` | state_type 为可索引容器 + 有 serialize/deserialize |
| `detail::JumpableEngine<E>` | 有 `jump() -> void` |
| `detail::StreamEngine<E>` | 当前等价于 JumpableEngine（为 Philox 预留） |
| `detail::RandFillable<It, T>` | output_iterator 且 T 为 integral 或 floating_point |
