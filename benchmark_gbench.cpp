//----------------------------------------------------------------------------------------
//
//	benchmark_gbench.cpp — RandX 结构化基准套件（Google Benchmark）
//
//	四大基准组：
//	  1. 引擎吞吐量（7 非 CSPRNG × 3 操作 + ChaCha20 专用 3 项）
//	  2. 分布开销（8 代表分布）
//	  3. RandSample 交叉点验证（三路径参数化扫描）
//	  4. jump() 与 SecureRandomBytes
//
//	构建：cmake -DRANDX_BUILD_BENCHMARK=ON && cmake --build build --target benchmark_gbench
//	运行：./benchmark_gbench --benchmark_format=json --benchmark_repetitions=5
//
//----------------------------------------------------------------------------------------

#include <benchmark/benchmark.h>
#include "RandX.hpp"

#include <cstdint>
#include <list>
#include <vector>

// ============================================================================
//	具名常量（消除魔法数字）
// ============================================================================

// 内层循环次数：避免快引擎（SFC64/RomuDuoJr ~1ns/call）被 state machine 循环开销主导
static constexpr int kEngineInnerLoop = 1000;

// RandFill 单次填充元素数
static constexpr int kRandFillN = 1'000'000;

// RandSample 容器大小
static constexpr int kSampleSize = 1'000'000;

// RandSample 切换点：n * HashSetThresholdK < size，即 n < size/64 = 15625
// 引用源码头常量避免漂移（与 RandX.hpp detail::HashSetThresholdK 一致）
static constexpr int kSampleCrossoverN =
    static_cast<int>(kSampleSize / static_cast<int>(RandX::detail::HashSetThresholdK));

// ChaCha20 reseed 阈值（字节）：引用 RandX.hpp 中的 detail::ChaCha20ReseedThreshold
static constexpr std::uint64_t kChaCha20ReseedBytes = RandX::detail::ChaCha20ReseedThreshold;

// reseed 阈值对应的 operator() 调用次数（每次 8 字节）
static constexpr std::uint64_t kChaCha20CallsPerReseed = kChaCha20ReseedBytes / 8;

// SecureRandomBytes 缓冲区大小（字节）
static constexpr int kSecureBufSize = 1024;

// ============================================================================
//	基准组 1：引擎吞吐量
// ============================================================================

// 1a. raw operator() 吞吐模板（仅非 CSPRNG 引擎）
template <class Engine>
static void BM_EngineRaw(benchmark::State& state)
{
    Engine rng{42};
    for (auto _ : state)
    {
        for (int i = 0; i < kEngineInnerLoop; ++i)
            benchmark::DoNotOptimize(rng());
    }
    state.SetItemsProcessed(state.iterations() * kEngineInnerLoop);
    state.SetBytesProcessed(state.iterations() * kEngineInnerLoop
                            * static_cast<std::int64_t>(sizeof(typename Engine::result_type)));
}

BENCHMARK_TEMPLATE(BM_EngineRaw, RandX::SplitMix64);
BENCHMARK_TEMPLATE(BM_EngineRaw, RandX::Xoshiro256StarStar);
BENCHMARK_TEMPLATE(BM_EngineRaw, RandX::Xoroshiro128StarStar);
BENCHMARK_TEMPLATE(BM_EngineRaw, RandX::SFC64);
BENCHMARK_TEMPLATE(BM_EngineRaw, RandX::RomuDuoJr);
BENCHMARK_TEMPLATE(BM_EngineRaw, RandX::Xoshiro128StarStar);
BENCHMARK_TEMPLATE(BM_EngineRaw, RandX::Xoroshiro64StarStar);

// 1b. RandInt 便捷 API 吞吐模板
template <class Engine>
static void BM_EngineRandInt(benchmark::State& state)
{
    Engine rng{42};
    for (auto _ : state)
    {
        for (int i = 0; i < kEngineInnerLoop; ++i)
            benchmark::DoNotOptimize(RandX::RandInt(rng, 0, 999999));
    }
    state.SetItemsProcessed(state.iterations() * kEngineInnerLoop);
}

BENCHMARK_TEMPLATE(BM_EngineRandInt, RandX::SplitMix64);
BENCHMARK_TEMPLATE(BM_EngineRandInt, RandX::Xoshiro256StarStar);
BENCHMARK_TEMPLATE(BM_EngineRandInt, RandX::Xoroshiro128StarStar);
BENCHMARK_TEMPLATE(BM_EngineRandInt, RandX::SFC64);
BENCHMARK_TEMPLATE(BM_EngineRandInt, RandX::RomuDuoJr);
BENCHMARK_TEMPLATE(BM_EngineRandInt, RandX::Xoshiro128StarStar);
BENCHMARK_TEMPLATE(BM_EngineRandInt, RandX::Xoroshiro64StarStar);

// 1c. RandFill 容器填充吞吐模板
template <class Engine>
static void BM_EngineRandFill(benchmark::State& state)
{
    Engine rng{42};
    std::vector<int> buf(static_cast<std::size_t>(kRandFillN));
    for (auto _ : state)
    {
        RandX::RandFill(rng, buf.begin(), buf.end(), 0, 999999);
        benchmark::DoNotOptimize(buf.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kRandFillN);
}

BENCHMARK_TEMPLATE(BM_EngineRandFill, RandX::SplitMix64);
BENCHMARK_TEMPLATE(BM_EngineRandFill, RandX::Xoshiro256StarStar);
BENCHMARK_TEMPLATE(BM_EngineRandFill, RandX::Xoroshiro128StarStar);
BENCHMARK_TEMPLATE(BM_EngineRandFill, RandX::SFC64);
BENCHMARK_TEMPLATE(BM_EngineRandFill, RandX::RomuDuoJr);
BENCHMARK_TEMPLATE(BM_EngineRandFill, RandX::Xoshiro128StarStar);
BENCHMARK_TEMPLATE(BM_EngineRandFill, RandX::Xoroshiro64StarStar);

// 1d. ChaCha20 专用基准（reseed 行为隔离）

// ChaCha20 纯算法吞吐：在 reseed 阈值内测量，避免 OS 熵调用污染
static void BM_ChaCha20RawNoReseed(benchmark::State& state)
{
    RandX::ChaCha20 rng{42};
    for (auto _ : state)
    {
        for (std::uint64_t i = 0; i < kChaCha20CallsPerReseed; ++i)
            benchmark::DoNotOptimize(rng());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kChaCha20CallsPerReseed));
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kChaCha20ReseedBytes));
}
BENCHMARK(BM_ChaCha20RawNoReseed);

// ChaCha20 真实吞吐（含周期性 reseed）：反映生产环境实际性能
static void BM_ChaCha20RawWithReseed(benchmark::State& state)
{
    RandX::ChaCha20 rng{};  // 默认构造启用自动 reseed
    for (auto _ : state)
    {
        for (int i = 0; i < kEngineInnerLoop; ++i)
            benchmark::DoNotOptimize(rng());
    }
    state.SetItemsProcessed(state.iterations() * kEngineInnerLoop);
    state.SetBytesProcessed(state.iterations() * kEngineInnerLoop
                            * sizeof(RandX::ChaCha20::result_type));
}
BENCHMARK(BM_ChaCha20RawWithReseed);

// ChaCha20 RandInt 吞吐（含 reseed）
static void BM_ChaCha20RandInt(benchmark::State& state)
{
    RandX::ChaCha20 rng{42};
    for (auto _ : state)
    {
        for (int i = 0; i < kEngineInnerLoop; ++i)
            benchmark::DoNotOptimize(RandX::RandInt(rng, 0, 999999));
    }
    state.SetItemsProcessed(state.iterations() * kEngineInnerLoop);
}
BENCHMARK(BM_ChaCha20RandInt);

// ============================================================================
//	基准组 2：分布开销
// ============================================================================

static void BM_RandNormal(benchmark::State& state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(RandX::RandNormal(0.0, 1.0));
}
BENCHMARK(BM_RandNormal);

static void BM_RandExp(benchmark::State& state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(RandX::RandExp(2.0));
}
BENCHMARK(BM_RandExp);

static void BM_RandGamma(benchmark::State& state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(RandX::RandGamma(2.0, 1.0));
}
BENCHMARK(BM_RandGamma);

static void BM_RandBeta(benchmark::State& state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(RandX::RandBeta(2.0, 5.0));
}
BENCHMARK(BM_RandBeta);

static void BM_RandBinomial(benchmark::State& state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(RandX::RandBinomial(100, 0.5));
}
BENCHMARK(BM_RandBinomial);

static void BM_RandLogNormal(benchmark::State& state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(RandX::RandLogNormal(0.0, 1.0));
}
BENCHMARK(BM_RandLogNormal);

static void BM_RandWeibull(benchmark::State& state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(RandX::RandWeibull(1.0, 1.5));
}
BENCHMARK(BM_RandWeibull);

static void BM_RandCauchy(benchmark::State& state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(RandX::RandCauchy(0.0, 1.0));
}
BENCHMARK(BM_RandCauchy);

// ============================================================================
//	基准组 3：RandSample 交叉点验证
// ============================================================================

// 随机访问迭代器路径（hash-set / 索引数组双分支）
static void BM_RandSampleIter(benchmark::State& state)
{
    const int n = static_cast<int>(state.range(0));
    std::vector<int> data(static_cast<std::size_t>(kSampleSize));
    for (int i = 0; i < kSampleSize; ++i)
        data[static_cast<std::size_t>(i)] = i;

    for (auto _ : state)
    {
        auto s = RandX::RandSample(data.begin(), data.end(), n);
        benchmark::DoNotOptimize(s.data());
        benchmark::ClobberMemory();
    }
}
// 显式 Arg 列表：精确控制采样点，交叉点 15625 附近密集采样
BENCHMARK(BM_RandSampleIter)
    ->Arg(50)->Arg(100)->Arg(200)->Arg(400)->Arg(800)
    ->Arg(1600)->Arg(3200)->Arg(6400)
    ->Arg(10000)->Arg(12500)
    ->Arg(kSampleCrossoverN)   // 15625：精确交叉点
    ->Arg(20000)->Arg(25600);

// 容器版对比基线
static void BM_RandSampleContainer(benchmark::State& state)
{
    const int n = static_cast<int>(state.range(0));
    std::vector<int> data(static_cast<std::size_t>(kSampleSize));
    for (int i = 0; i < kSampleSize; ++i)
        data[static_cast<std::size_t>(i)] = i;

    for (auto _ : state)
    {
        auto s = RandX::RandSample(data, static_cast<std::size_t>(n));
        benchmark::DoNotOptimize(s.data());
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_RandSampleContainer)
    ->Arg(50)->Arg(100)->Arg(200)->Arg(400)->Arg(800)
    ->Arg(1600)->Arg(3200)->Arg(6400)
    ->Arg(10000)->Arg(12500)
    ->Arg(kSampleCrossoverN)
    ->Arg(20000)->Arg(25600);

// reservoir 路径（std::list 输入迭代器）
// 内存占用：std::list<int> 1M 节点 × ~24 字节/节点 ≈ 24 MB
static void BM_RandSampleReservoir(benchmark::State& state)
{
    const int n = static_cast<int>(state.range(0));
    std::list<int> data;
    for (int i = 0; i < kSampleSize; ++i)
        data.push_back(i);

    for (auto _ : state)
    {
        auto s = RandX::RandSample(data.begin(), data.end(), n);
        benchmark::DoNotOptimize(s.data());
        benchmark::ClobberMemory();
    }
}
// reservoir 仅扫小 n：大 n 极慢（O(N·log n) 哈希查找）
BENCHMARK(BM_RandSampleReservoir)
    ->Arg(50)->Arg(100)->Arg(200)->Arg(400)->Arg(800);

// ============================================================================
//	基准组 4：jump() 与 SecureRandomBytes
// ============================================================================

// jump() 跳跃开销模板（仅 3 个 xoshiro 系列引擎有 jump）
template <class Engine>
static void BM_EngineJump(benchmark::State& state)
{
    Engine rng{42};
    for (auto _ : state)
    {
        rng.jump();
        benchmark::DoNotOptimize(rng());
    }
}

BENCHMARK_TEMPLATE(BM_EngineJump, RandX::Xoshiro256StarStar);
BENCHMARK_TEMPLATE(BM_EngineJump, RandX::Xoroshiro128StarStar);
BENCHMARK_TEMPLATE(BM_EngineJump, RandX::Xoshiro128StarStar);

// SecureRandomBytes 延迟与吞吐（OS 熵调用）
static void BM_SecureRandomBytes(benchmark::State& state)
{
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(kSecureBufSize));
    for (auto _ : state)
    {
        RandX::SecureRandomBytes(buf.data(), buf.size());
        benchmark::DoNotOptimize(buf[0]);
    }
    state.SetBytesProcessed(state.iterations() * kSecureBufSize);
}
BENCHMARK(BM_SecureRandomBytes);

// ============================================================================

BENCHMARK_MAIN();
