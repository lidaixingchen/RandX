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

#if defined(RANDX_BENCHMARK_CPP17)
#include <RandX_Cpp17.hpp>
#else
#include <RandX.hpp>
#endif

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <list>
#include <memory_resource>
#include <set>
#include <utility>
#include <vector>

static constexpr std::uint64_t kSamplingSeed = 0x5A17C0DEULL;

class SamplingAllocationProbe final : public std::pmr::memory_resource
{
public:
    std::size_t bytes() const noexcept { return bytes_; }
    std::size_t alignment() const noexcept { return alignment_; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        void* allocation = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        bytes_ += (bytes + alignment - 1) / alignment * alignment;
        alignment_ = (std::max)(alignment_, alignment);
        return allocation;
    }
    void do_deallocate(void* allocation, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(allocation, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
    std::size_t bytes_ = 0;
    std::size_t alignment_ = alignof(std::max_align_t);
};

struct SamplingListLayout
{
    std::size_t emptyBytes;
    std::size_t elementBytes;
    std::size_t alignment;
};

static SamplingListLayout ProbeSamplingListLayout()
{
    // 空链表哨兵和元素节点的容量来自实际标准库分配请求。
    SamplingAllocationProbe resource;
    std::pmr::list<int> probe(&resource);
    const std::size_t emptyBytes = resource.bytes();
    probe.push_back(0);
    return {emptyBytes, resource.bytes() - emptyBytes, resource.alignment()};
}

static std::size_t SamplingListStorageSize(std::size_t rangeSize)
{
    static const SamplingListLayout layout = ProbeSamplingListLayout();
    return layout.emptyBytes + rangeSize * layout.elementBytes + layout.alignment - 1;
}

class SamplingList
{
public:
    explicit SamplingList(std::size_t rangeSize)
        : storage_(SamplingListStorageSize(rangeSize)),
          resource_(storage_.data(), storage_.size(), std::pmr::null_memory_resource()),
          values_(&resource_)
    {
        for (std::size_t index = 0; index < rangeSize; ++index)
            values_.push_back(static_cast<int>(index));
    }
    auto cbegin() const noexcept { return values_.cbegin(); }
    auto cend() const noexcept { return values_.cend(); }

private:
    // 资源在链表之后销毁，连续存储在资源之后释放。
    std::vector<std::byte> storage_;
    std::pmr::monotonic_buffer_resource resource_;
    std::pmr::list<int> values_;
};

static SamplingList MakeSamplingList(std::size_t rangeSize)
{
    return SamplingList(rangeSize);
}

static void PrepareDefaultSamplingEngine()
{
    RandX::Reseed(kSamplingSeed);
    benchmark::DoNotOptimize(RandX::RandInt(0, 1));
    RandX::Reseed(kSamplingSeed);
}

#if defined(RANDX_BENCHMARK_SAMPLING_ONLY)

static constexpr std::size_t kSamplingSmallRangeMultiplier = 4;
static constexpr std::size_t kSamplingSmallRangeSize =
    static_cast<std::size_t>(RandX::detail::HashSetThresholdK * kSamplingSmallRangeMultiplier);
static constexpr std::size_t kSamplingBitmapWordBoundaryRangeMultiplier =
    kSamplingSmallRangeMultiplier;
static constexpr std::size_t kSamplingBitmapWordBoundaryNeighbor = 1;
static constexpr std::size_t kSamplingBitmapWordBoundaryAlignedRangeSize =
    static_cast<std::size_t>(RandX::detail::SampleBitmapWordBits
                             * kSamplingBitmapWordBoundaryRangeMultiplier);
static constexpr std::size_t kSamplingBitmapWordBoundaryLowerRangeSize =
    kSamplingBitmapWordBoundaryAlignedRangeSize - kSamplingBitmapWordBoundaryNeighbor;
static constexpr std::size_t kSamplingBitmapWordBoundaryUpperRangeSize =
    kSamplingBitmapWordBoundaryAlignedRangeSize + kSamplingBitmapWordBoundaryNeighbor;
static constexpr std::size_t kSamplingLargeRangeSize = 1'000'000;
static constexpr std::size_t kSamplingMediumDensityDivisor =
    static_cast<std::size_t>(RandX::detail::SampleBitmapDensityDivisor * 2);
static constexpr std::size_t kSamplingHighDensityDivisor = 4;

using SamplingCase = std::pair<std::size_t, std::size_t>;

static std::set<std::size_t> MakeSamplingRequests(std::size_t rangeSize)
{
    const std::size_t hashSetBoundary = static_cast<std::size_t>(
        (rangeSize - 1) / RandX::detail::HashSetThresholdK);
    const std::size_t bitmapBoundary = static_cast<std::size_t>(
        (rangeSize - 1) / RandX::detail::SampleBitmapThresholdK);
    const std::size_t halfDensityBoundary = static_cast<std::size_t>(
        rangeSize / RandX::detail::SampleBitmapDensityDivisor);

    std::set<std::size_t> requests{
        0,
        1,
        hashSetBoundary,
        hashSetBoundary + 1,
        bitmapBoundary,
        bitmapBoundary + 1,
        rangeSize / kSamplingMediumDensityDivisor,
        halfDensityBoundary,
        halfDensityBoundary + 1,
        rangeSize - rangeSize / kSamplingHighDensityDivisor,
        rangeSize,
        rangeSize + 1
    };
    if (hashSetBoundary > 0)
        requests.insert(hashSetBoundary - 1);
    if (bitmapBoundary > 0)
        requests.insert(bitmapBoundary - 1);
    return requests;
}

static std::set<SamplingCase> MakeRandomAccessSamplingCases()
{
    std::set<SamplingCase> cases;
    for (const std::size_t rangeSize : {kSamplingSmallRangeSize, kSamplingLargeRangeSize})
    {
        for (const std::size_t request : MakeSamplingRequests(rangeSize))
            cases.emplace(rangeSize, request);
    }
    cases.emplace(0, 1);
    cases.emplace(1, 1);
    cases.emplace(1, 2);
    return cases;
}

static std::set<SamplingCase> MakeBitmapWordBoundarySamplingCases()
{
    const std::set<std::size_t> rangeSizes{
        kSamplingBitmapWordBoundaryLowerRangeSize,
        kSamplingBitmapWordBoundaryAlignedRangeSize,
        kSamplingBitmapWordBoundaryUpperRangeSize
    };
    std::set<SamplingCase> cases;
    for (const std::size_t rangeSize : rangeSizes)
    {
        const std::size_t request = static_cast<std::size_t>(
            rangeSize / RandX::detail::SampleBitmapDensityDivisor);
        cases.emplace(rangeSize, request);
    }
    return cases;
}

static std::set<SamplingCase> MakeContainerSamplingCases()
{
    std::set<SamplingCase> cases = MakeRandomAccessSamplingCases();
    const std::set<SamplingCase> bitmapWordBoundaryCases = MakeBitmapWordBoundarySamplingCases();
    cases.insert(bitmapWordBoundaryCases.begin(), bitmapWordBoundaryCases.end());
    return cases;
}

static std::set<SamplingCase> MakeReservoirSamplingCases()
{
    std::set<SamplingCase> cases;
    for (const std::size_t request : MakeSamplingRequests(kSamplingSmallRangeSize))
        cases.emplace(kSamplingSmallRangeSize, request);

    const std::size_t largeRangeHashSetBoundary = static_cast<std::size_t>(
        (kSamplingLargeRangeSize - 1) / RandX::detail::HashSetThresholdK);
    const std::set<std::size_t> largeRangeRequests{
        0,
        1,
        largeRangeHashSetBoundary,
        largeRangeHashSetBoundary + 1,
        kSamplingLargeRangeSize / kSamplingMediumDensityDivisor,
        kSamplingLargeRangeSize / RandX::detail::SampleBitmapDensityDivisor,
        kSamplingLargeRangeSize - kSamplingLargeRangeSize / kSamplingHighDensityDivisor,
        kSamplingLargeRangeSize,
        kSamplingLargeRangeSize + 1
    };
    for (const std::size_t request : largeRangeRequests)
        cases.emplace(kSamplingLargeRangeSize, request);

    cases.emplace(0, 1);
    cases.emplace(1, 1);
    cases.emplace(1, 2);
    return cases;
}

static void ApplySamplingCases(benchmark::internal::Benchmark* benchmarkCase,
                               const std::set<SamplingCase>& cases)
{
    for (const auto& [rangeSize, request] : cases)
    {
        benchmarkCase->Args({static_cast<std::int64_t>(rangeSize),
                             static_cast<std::int64_t>(request)});
    }
}

static void AddRandomAccessSamplingCases(benchmark::internal::Benchmark* benchmarkCase)
{
    ApplySamplingCases(benchmarkCase, MakeRandomAccessSamplingCases());
}

static void AddContainerSamplingCases(benchmark::internal::Benchmark* benchmarkCase)
{
    ApplySamplingCases(benchmarkCase, MakeContainerSamplingCases());
}

static void AddReservoirSamplingCases(benchmark::internal::Benchmark* benchmarkCase)
{
    ApplySamplingCases(benchmarkCase, MakeReservoirSamplingCases());
}

static std::vector<int> MakeSamplingVector(std::size_t rangeSize)
{
    std::vector<int> values(rangeSize);
    for (std::size_t index = 0; index < rangeSize; ++index)
        values[index] = static_cast<int>(index);
    return values;
}

template <class T>
static inline void ObserveSamplingResult(std::vector<T>& result)
{
    auto resultSize = result.size();
    auto data = result.data();
    benchmark::DoNotOptimize(resultSize);
    benchmark::DoNotOptimize(data);
    if (!result.empty()) benchmark::ClobberMemory();
}

static void BM_RandSampleIteratorDefault(benchmark::State& state)
{
    const auto rangeSize = static_cast<std::size_t>(state.range(0));
    auto request = static_cast<std::ptrdiff_t>(state.range(1));
    auto values = MakeSamplingVector(rangeSize);
    PrepareDefaultSamplingEngine();

    benchmark::DoNotOptimize(request);
    for (auto _ : state)
    {
        auto result = RandX::RandSample(values.cbegin(), values.cend(), request);
        ObserveSamplingResult(result);
    }
}
BENCHMARK(BM_RandSampleIteratorDefault)
    ->Apply(AddRandomAccessSamplingCases)
    ->ArgNames({"range_size", "request"});

template <class Engine>
static void BM_RandSampleIteratorExplicit(benchmark::State& state)
{
    const auto rangeSize = static_cast<std::size_t>(state.range(0));
    auto request = static_cast<std::ptrdiff_t>(state.range(1));
    auto values = MakeSamplingVector(rangeSize);
    Engine engine{kSamplingSeed};

    benchmark::DoNotOptimize(request);
    for (auto _ : state)
    {
        auto result = RandX::RandSample(engine, values.cbegin(), values.cend(), request);
        ObserveSamplingResult(result);
    }
}
BENCHMARK_TEMPLATE(BM_RandSampleIteratorExplicit, RandX::Xoshiro256StarStar)
    ->Apply(AddRandomAccessSamplingCases)
    ->ArgNames({"range_size", "request"});
BENCHMARK_TEMPLATE(BM_RandSampleIteratorExplicit, RandX::Xoshiro128StarStar)
    ->Apply(AddRandomAccessSamplingCases)
    ->ArgNames({"range_size", "request"});

static void BM_RandSampleContainerDefault(benchmark::State& state)
{
    const auto rangeSize = static_cast<std::size_t>(state.range(0));
    auto request = static_cast<std::size_t>(state.range(1));
    auto values = MakeSamplingVector(rangeSize);
    PrepareDefaultSamplingEngine();

    benchmark::DoNotOptimize(request);
    for (auto _ : state)
    {
        auto result = RandX::RandSample(values, request);
        ObserveSamplingResult(result);
    }
}
BENCHMARK(BM_RandSampleContainerDefault)
    ->Apply(AddContainerSamplingCases)
    ->ArgNames({"range_size", "request"});

template <class Engine>
static void BM_RandSampleContainerExplicit(benchmark::State& state)
{
    const auto rangeSize = static_cast<std::size_t>(state.range(0));
    auto request = static_cast<std::size_t>(state.range(1));
    auto values = MakeSamplingVector(rangeSize);
    Engine engine{kSamplingSeed};

    benchmark::DoNotOptimize(request);
    for (auto _ : state)
    {
        auto result = RandX::RandSample(engine, values, request);
        ObserveSamplingResult(result);
    }
}
BENCHMARK_TEMPLATE(BM_RandSampleContainerExplicit, RandX::Xoshiro256StarStar)
    ->Apply(AddContainerSamplingCases)
    ->ArgNames({"range_size", "request"});
BENCHMARK_TEMPLATE(BM_RandSampleContainerExplicit, RandX::Xoshiro128StarStar)
    ->Apply(AddContainerSamplingCases)
    ->ArgNames({"range_size", "request"});

static void BM_RandSampleReservoirDefault(benchmark::State& state)
{
    const auto rangeSize = static_cast<std::size_t>(state.range(0));
    auto request = static_cast<std::ptrdiff_t>(state.range(1));
    auto values = MakeSamplingList(rangeSize);
    PrepareDefaultSamplingEngine();

    benchmark::DoNotOptimize(request);
    for (auto _ : state)
    {
        auto first = values.cbegin();
        auto last = values.cend();
        auto result = RandX::RandSample(first, last, request);
        ObserveSamplingResult(result);
    }
}
BENCHMARK(BM_RandSampleReservoirDefault)
    ->Apply(AddReservoirSamplingCases)
    ->ArgNames({"range_size", "request"});

template <class Engine>
static void BM_RandSampleReservoirExplicit(benchmark::State& state)
{
    const auto rangeSize = static_cast<std::size_t>(state.range(0));
    auto request = static_cast<std::ptrdiff_t>(state.range(1));
    auto values = MakeSamplingList(rangeSize);
    Engine engine{kSamplingSeed};

    benchmark::DoNotOptimize(request);
    for (auto _ : state)
    {
        auto first = values.cbegin();
        auto last = values.cend();
        auto result = RandX::RandSample(engine, first, last, request);
        ObserveSamplingResult(result);
    }
}
BENCHMARK_TEMPLATE(BM_RandSampleReservoirExplicit, RandX::Xoshiro256StarStar)
    ->Apply(AddReservoirSamplingCases)
    ->ArgNames({"range_size", "request"});
BENCHMARK_TEMPLATE(BM_RandSampleReservoirExplicit, RandX::Xoshiro128StarStar)
    ->Apply(AddReservoirSamplingCases)
    ->ArgNames({"range_size", "request"});

#else

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
        for (std::uint64_t i = 0; i < kChaCha20CallsPerReseed; ++i)
            benchmark::DoNotOptimize(rng());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kChaCha20CallsPerReseed));
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kChaCha20ReseedBytes));
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

// reservoir 路径使用标准库链表；共同夹具控制节点布局和默认引擎播种。
static void BM_RandSampleReservoir(benchmark::State& state)
{
    const int n = static_cast<int>(state.range(0));
    auto data = MakeSamplingList(kSampleSize);
    PrepareDefaultSamplingEngine();

    for (auto _ : state)
    {
        auto s = RandX::RandSample(data.cbegin(), data.cend(), n);
        benchmark::DoNotOptimize(s.data());
        benchmark::ClobberMemory();
    }
}
// Algorithm R 遍历输入一次，时间复杂度 O(N)，保留结果需要 O(n) 空间。
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
#endif

BENCHMARK_MAIN();
