# RandX API 详细参考

> 本页按功能说明常用调用方式与参数行为；全量声明与重载见 [Doxygen API 参考](https://lidaixingchen.github.io/RandX/)，快速概览见 [README](../README.md)。下文约束采用 C++23 表达，C++17 的共同接口以 SFINAE 表达。
> 普通运行时采样的默认重载使用线程局部 `Xoshiro256StarStar`，显式引擎调用例如 `RandInt(rng, min, max)`。安全接口直接读取 OS 熵，编译期随机使用模板种子。

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

返回均匀分布于 [min, max) 的随机浮点数；相等端点返回该值。参数须有限、按序排列，宽度须在返回类型中可表示，否则抛出 `std::invalid_argument`。`[0,1)` 区间使用 `RandCanonical<T>` 路径。

### RandCanonical

调用方式：`RandCanonical<T>()`、`RandCanonical<T>(engine)`；`RandCanonicalDouble()` 和 `RandCanonicalFloat()` 分别调用对应返回类型的默认重载。

返回 `[0,1)` 浮点数。float／double 对满位宽 64 位引擎分别提取高 24／53 位，各取一次输出；满位宽 32 位引擎生成 float 取一次输出，生成 double 取两次并按高低位拼接。其他引擎及 long double 使用现有 `std::generate_canonical` 路径。

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
[[nodiscard]] inline T RandBits();
```

T 须为整数类型且不为 bool，N 还须不超过 `std::numeric_limits<T>::digits`。返回 [0, 2^N) 的随机整数，支持 `RandBits<N, T>(engine)`；满位宽 32 位引擎在 N 超过 32 时拼接两次输出，其他引擎按其取值范围进行映射。

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

`RandGeometric` 生成首次成功前的失败次数，将结果分为块编号与块内整数余数：块编号采用稳定的逆变换，块内余数采用整数拒绝采样，以保留大整数的低位随机性。`p=1` 返回零且不消耗引擎输出；参数无效或过小时抛出 `std::invalid_argument`，抽样值超出返回类型范围时抛出 `std::overflow_error`。抽样超限时，引擎已消耗本次抽样所需的输出。

`RandBeta` 校验有限且为正的形状参数，普通参数路径稳定归一化两个 Gamma 样本，极端参数路径使用对数域采样与归一化。无效参数抛出 `std::invalid_argument`；无法形成有效样本时按数值契约抛出 `std::domain_error`。

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
[[nodiscard]] inline auto RandSample(const Container& c, std::size_t n);
// n >= size 时返回全部副本

// 随机访问迭代器版
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

随机访问迭代器与容器入口复用共同抽样内核，按范围长度与请求数量选择稀疏集合、位图或索引数组路径。容器版只复制选中的元素，支持带只读成员的可复制构造类型；`n=0` 返回空结果，`n` 不小于容器大小时按原顺序返回全部元素的副本。

随机访问范围的长度须同时能用 `std::uint64_t` 和 `std::size_t` 表示；正数量抽样超出该长度限制时抛出 `std::length_error`，不消耗引擎输出。

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
| `Hex` | [0-9a-f] | 16 |
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
[[nodiscard]] inline constexpr T RandIntCE(T min, T max);

template <std::integral T = int, std::uint64_t Seed = DefaultSeed>
[[nodiscard]] inline constexpr T RandIntCE(T max);  // [0, max]
```

结果由 Seed 模板参数决定，显式写法为 `RandIntCE<T, Seed>(min, max)`。支持宽乘法时使用 Lemire 有界法，其他环境使用拒绝采样，均保持无模偏差。非法区间抛出 `std::invalid_argument`；在常量求值中该调用不能形成有效常量表达式。

### ShuffleCE

```cpp
template <std::random_access_iterator It, std::uint64_t Seed = DefaultSeed>
constexpr void ShuffleCE(It first, It last);
```

`std::shuffle` 在 C++23 中仍非 constexpr，故自实现 Fisher-Yates。

### ShuffledArray

```cpp
template <class T, std::size_t N, std::uint64_t Seed = DefaultSeed>
[[nodiscard]] constexpr std::array<T, N> ShuffledArray(std::array<T, N> arr);
```

---

## 序列化

### serialize / deserialize（引擎成员函数）

```cpp
[[nodiscard]] constexpr state_type serialize() const noexcept;
constexpr void deserialize(const state_type& state) noexcept;  // 数组状态引擎
constexpr void deserialize(state_type state) noexcept;         // SplitMix64
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

### 状态输入与恢复

xoshiro／xoroshiro 与 RomuDuoJr 的全零数组状态会陷入吸收态。状态构造与 `deserialize()` 将这类输入的首个状态字置为 1，Debug／Release 行为一致。

SFC64 的计数器使全零完整快照可以正常推进，状态构造和 `deserialize()` 保留该快照；种子构造另有状态初始化与预热规则。SplitMix64 的标量零状态合法。ChaCha20 不提供状态构造或序列化入口。

状态构造、反序列化与播种是不同契约，不能将某一入口的处理泛化到所有入口。共同回归测试位于 `tests/common/engine_contracts.hpp` 和 `tests/common/serialization_contracts.hpp`。

### operator<< / operator>>

```cpp
template <class CharT, class Traits, detail::SerializableEngine Engine>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, const Engine& engine);

template <class CharT, class Traits, detail::SerializableEngine Engine>
std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits>& is, Engine& engine);
```

格式为空格分隔的十进制状态字。SplitMix64 通过标量 `serialize()`／`deserialize()` 保存恢复状态，ChaCha20 不提供这些流接口。流式输入失败时设置 failbit 并保留原引擎状态；状态有效性按各引擎政策判断。

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
默认构造启用自动重播种，输出达到 2^20 字节后从 OS 熵获取新材料；显式种子与直接 key／nonce 构造关闭自动重播种。手动 `reseed()` 保持原自动设置。默认构造、重播种和自动重播种均可能因 OS 熵读取失败抛出 `std::runtime_error`。

单实例由单线程独立使用。移动后的源实例生成或 discard 时抛出 `std::logic_error`；确定性模式耗尽 block 计数器后抛出 `std::overflow_error`。

### SecureRandomBytes

```cpp
inline void SecureRandomBytes(void* buf, std::size_t n);
// 异常: std::runtime_error（OS 熵源不可用）
```

直接读取 OS 密码学熵源，失败抛异常；不经过普通 `RandomSeed()` 回退链。长度为零时直接返回，非零请求需提供有效目标存储。

### SecureSeed

```cpp
[[nodiscard]] inline std::uint64_t SecureSeed();
```

### IsOsCryptoEntropyAvailable

```cpp
[[nodiscard]] inline constexpr bool IsOsCryptoEntropyAvailable() noexcept;
```

查询目标平台及头文件环境是否支持 BCryptGenRandom／getrandom／SecRandomCopyBytes。true 表示编译期能力具备，false 表示缺少支持的 OS 密码学接口；运行时读取失败仍由安全接口抛异常。

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

仅支持具有 jump 能力的引擎。当前实现将流 ID 的高位部分映射为 longJump 次数、低位部分映射为 jump 次数；小 ID 的相邻流间隔为 2^128 步（Xoshiro256StarStar）或 2^64 步（Xoroshiro128StarStar／Xoshiro128StarStar）。流数量、周期与每流消耗预算共同决定状态子序列的分隔条件，任意两个输出值相等不能作为子序列重叠的判据。

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
// 线程局部，首次调用时通过 RandomSeed() 播种
```

引用的生命周期属于当前线程；异步任务或其他线程应持有自己的引擎。

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
