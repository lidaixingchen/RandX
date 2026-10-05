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

## 统计分布（17 种）

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
| `RandTriangular` | `T RandTriangular(T min, T peak, T max)` | 无 | 以 peak 为众数的三角分布 |
| `RandBernoulli` | `bool RandBernoulli(double p)` | p=0.5 | 伯努利（RandBool 别名） |
| `RandWeighted` | `size_type RandWeighted(const WeightContainer& weights)` | 无 | 按权重选取索引 |

浮点分布模板参数为 `std::floating_point T = double`。

指定引擎重载模式：`T RandNormal(Engine& engine, T mean = T{0}, T stddev = T{1})`。

### 整数分布类型契约

三个整数分布默认返回 `int`，默认与显式引擎重载采用相同的类型政策：

| 接口 | 返回类型候选 |
| --- | --- |
| `RandPoisson`、`RandBinomial` | cv 未限定的 `short`、`int`、`long`、`long long` 及其对应 unsigned 类型 |
| `RandGeometric` | cv 未限定的 integral 类型，排除 `bool`，且 `std::numeric_limits<T>::digits <= std::numeric_limits<std::uint64_t>::digits`；满足该位宽的字符类型依语言标准支持保留 |

标准计数分布调用使用上述八种整型；需要字符结果时，先获得支持的计数类型，再由调用方检查范围后转换。几何分布使用有效位数满足上限的类型。移除 cv 限定可保留对应值类型；布尔随机用途使用 `RandBool` 或 `RandBernoulli`，由调用方确认概率模型。候选集收紧属于破坏性变更，发布按主版本管理；支持类型的算法、状态和输出序列保持原有契约。

### RandTriangular

```cpp
template <typename T>
[[nodiscard]] T RandTriangular(T min, T peak, T max);

template <typename Engine, typename T>
[[nodiscard]] T RandTriangular(Engine& engine, T min, T peak, T max);
```

三个参数须为同一种 `float`、`double` 或 `long double` 类型。`peak` 是众数，可以等于任一端点。所有参数都须有限并满足 `min <= peak <= max`；参数无效，或非退化区间的宽度在 T 中溢出时，抛出 `std::invalid_argument`，且不调用引擎。默认重载先检查参数和退化条件，仅在有效非退化输入时获取线程局部默认引擎。

有效的非退化输入返回有限的 `[min, max)` 值，正常采样调用一次 `RandCanonical<T>`。退化输入 `min == peak == max` 原样返回该值且不消耗引擎。浮点舍入可能使结果等于下界；相邻端点之间若没有其他可表示值，结果为下界。底层引擎输出次数遵循 `RandCanonical<T>` 的位宽规则：满位宽 64 位引擎生成 float 或 double 各调用一次，满位宽 32 位引擎生成 float 调用一次、double 调用两次；long double 依照其既有 canonical 精度规则。

数值精度契约假定舍入到最近值并启用渐进下溢；使用 GCC／Clang 的标准浮点选项或 MSVC `/fp:precise`。`fast-math`、FTZ／DAZ 与其他舍入模式不在本次精度保证内，函数不会修改调用方的浮点控制状态。

底层引擎异常直接传播；数值转换无法形成有限区间内结果时抛出 `std::runtime_error`。这些异常可能发生在本次均匀采样已消耗引擎输出之后。

### RandGeometric

`RandGeometric` 生成首次成功前的失败次数，将结果分为块编号与块内整数余数：块编号采用稳定的逆变换，块内余数采用整数拒绝采样，以保留大整数的低位随机性。`p=1` 返回零且不消耗引擎输出；参数无效或过小时抛出 `std::invalid_argument`，抽样值超出返回类型范围时抛出 `std::overflow_error`。抽样超限时，引擎已消耗本次抽样所需的输出。

### RandBeta

`RandBeta` 校验有限且为正的形状参数，普通参数路径稳定归一化两个 Gamma 样本，极端参数路径使用对数域采样与归一化。无效参数抛出 `std::invalid_argument`；无法形成有效样本时按数值契约抛出 `std::domain_error`。

---

## 分布复用与标准库批量生成

同一组参数用于重复采样时，可由调用方持有标准库分布对象并直接调用 `dist(engine)`。使用 `std::generate_n` 时，通过引用捕获保留原分布和引擎的身份；生成器自身按值传递不会复制被引用的分布。

```cpp
constexpr std::uint64_t SimulationSeed = 20261004;
constexpr double SampleMean = 0.0;
constexpr double SampleStddev = 1.0;
constexpr std::size_t SampleCount = 32;

RandX::Xoshiro256StarStar engine{SimulationSeed};
std::normal_distribution<double> normal{SampleMean, SampleStddev};
std::vector<double> samples;
samples.reserve(SampleCount);
std::generate_n(std::back_inserter(samples), SampleCount,
                [&normal, &engine]() { return normal(engine); });
```

此片段需要 `<algorithm>`、`<cstddef>`、`<cstdint>`、`<iterator>`、`<random>`、`<vector>` 和对应版本的 RandX 头文件。完整双版本来源见[分布复用示例](../examples/distribution_generation.cpp)。

- 标准分布的构造和参数有效性由调用方负责。例如 `std::normal_distribution` 要求标准差为正；直接调用标准分布遵循标准库前置条件。RandX 具名分布函数的参数检查及异常规则见各自接口。
- 缓存属于分布对象。重播种引擎后，要从起点重现序列还须重置或重建分布；要从中途恢复序列，须恢复当时的引擎和分布状态。
- 单次采样所需的引擎输出次数由分布决定，分布缓存可以使某次采样不读取引擎。标准分布算法由标准库实现决定，跨工具链的样本序列可能不同。
- `std::generate_n` 成功完成时，执行请求数量的生成和赋值，返回推进后的输出位置。有限范围须有足够的可写元素；`std::back_inserter` 用于追加，`std::inserter` 遵循容器插入规则，set 合并重复样本后元素数量可以小于生成数量。
- 生成器、输出操作或迭代器操作抛出异常时，异常向调用方传播，已经发生的对象状态变化按参与类型自身的保证保留；算法异常路径没有返回的输出位置。事务性输出由调用方管理。
- 将 `DefaultEngine()` 放在生成器内部，零数量调用不会执行生成器。要在批次内复用一次取得的默认引擎引用，应先确认数量为正，再获取引用并按引用捕获。
- 线程中的分布、引擎和输出目标须具有合适的生命周期；共享分布或输出容器的并发访问由调用方同步。

`RandBeta`、`RandTriangular` 等具名函数可在生成器中直接调用，保留相应函数的参数校验和数值契约。

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

template <class Engine>
[[nodiscard]] inline std::string RandString(Engine& engine, std::size_t n, std::string_view charset);
```

`charset` 可由调用方提供自定义字符集合；空字符集抛出 `std::invalid_argument`。该重载按字符位置等概率选取，重复字符会按出现次数影响其概率。默认引擎与显式引擎两种形式均可使用自定义字符集。

### RandUUID

```cpp
[[nodiscard]] inline std::string RandUUID();
template <class Engine>
[[nodiscard]] inline std::string RandUUID(Engine& engine);
// 返回 UUID v4: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
```

`RandUUID()` 使用线程局部普通 PRNG。传入 `RandUUID(engine)` 可控制引擎与序列；安全敏感标识符应传入默认构造、由 OS 熵初始化的 `ChaCha20`，或改用 `SecureRandomBytes()`。显式传入普通伪随机引擎时，UUID 的格式不改变其安全属性。

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

默认构造从 OS 熵取得 32 字节 key 与 12 字节 nonce，并从 block counter 0 开始。`ChaCha20(std::uint64_t seed)` 将确定性 64 位种子扩展为 key 和 nonce，只用于测试或复现；它不提供密码学熵。`SecureSeed()` 返回一个由 OS 熵填充的 64 位种子值，适合作为普通引擎的种子；64 位返回范围不等同于 ChaCha20 所需的完整密码学初始化材料。

直接 key／nonce 构造要求非空指针、32 字节 key、12 字节 nonce，`counter` 是初始 32 位 block counter。ChaCha20 每个 block 为 64 字节。同一 key 与 nonce 下，各次使用必须分配互不重叠的 block counter 区间；重复或重叠位置会产生相同的密钥流字节，破坏保密性。该构造关闭自动重播种，key、nonce 与 counter 的唯一性和安全来源由调用方负责。

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

### 敏感材料擦除后端配置

ChaCha20 生命周期使用头文件内的安全擦除实现。CMake 提供 `RANDX_USE_PORTABLE_SECURE_WIPE` 和 `RANDX_USE_APPLE_MEMSET_S` 两个布尔选项，默认均为 `OFF`。启用兼容后端时，`RANDX_USE_PORTABLE_SECURE_WIPE=1` 随 `RandX`、`RandX::Cpp17` 与 `RandX::Cpp23` INTERFACE 目标传播；Apple 的 `RANDX_USE_APPLE_MEMSET_S=1` 同时传播 `__STDC_WANT_LIB_EXT1__=1`。

这类宏会影响头文件内联函数的定义，应用程序的所有翻译单元、静态库和预编译头须使用相同后端及扩展声明配置。直接包含头文件的构建应在目标级或全局编译选项中统一定义所需宏；Apple 扩展宏须在包含系统头文件前生效。混用不同后端的翻译单元不符合配置契约。

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

同一种子下，三种内置流引擎的映射为 `L^high32 J^low32`：先按 ID 高 32 位应用 `longJump()`，再按低 32 位应用 `jump()`。共同实现使用离线生成的跳跃幂材料，定位成本随编号的置位数增长；全部 64 位编号均可定位，零编号及单次跳跃有直接路径。材料同时用于运行时和 `constexpr`，不需要运行时建表。

| 引擎 | 周期 | `jump()` 距离 | `longJump()` 距离 | 对任意互异 ID 适用的统一每流预算 |
| --- | --- | --- | --- | --- |
| `Xoshiro256StarStar` | `2^256−1` | `2^128` | `2^192` | 至多 `2^128` 次引擎调用 |
| `Xoroshiro128StarStar` | `2^128−1` | `2^64` | `2^96` | 严格少于 `2^64` 次引擎调用 |
| `Xoshiro128StarStar` | `2^128−1` | `2^64` | `2^96` | 严格少于 `2^64` 次引擎调用 |

预算以引擎的 `operator()` 调用次数计；组合位宽和分布的拒绝采样会消耗额外调用。128 位状态引擎的最大编号起点为 `2^128−2^64`，到周期回绕只有 `2^64−1` 次调用；统一预算因此保留该边界。256 位状态引擎的所有编号起点均小于 `2^224`，周期回绕距离远大于短跳跃间隔。

这些条件用于分隔状态子序列，不能据此推断统计独立性；任意两个输出值相等也不能作为子序列重叠的判据。任务持有自身引擎，保持 ID 与 seed 分配稳定并遵守调用预算。连续低位编号可从保存的流起点复制后逐次 `jump()`；低 32 位进位时，从保存的高位起点执行 `longJump()`。

自定义引擎具有 `longJump()` 时沿用高、低位循环和调用顺序；只有 `jump()` 时执行完整 ID 次短跳跃。自定义跳跃的异常向调用方传播，其距离、周期和复杂度由该引擎契约规定。

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
| `detail::StreamEngine<E>` | 具有 jump 能力的流引擎约束 |
| `detail::RandFillable<It, T>` | output_iterator 且 T 为 integral 或 floating_point |
