#include <benchmark/benchmark.h>

#if defined(RANDX_BENCHMARK_CPP17)
#include <RandX_Cpp17.hpp>
#else
#include <RandX.hpp>
#endif

#include <cstdint>
#include <array>
#include <limits>
#include <string>

namespace
{
constexpr std::uint64_t kBenchmarkSeed = 0x4D595DF4D0F33173ULL;
constexpr double kUnitMinimum = 0.0;
constexpr double kUnitMaximum = 1.0;
constexpr double kSymmetricPeak = 0.5;
constexpr double kLeftSkewedPeak = 0.25;
constexpr double kRightSkewedPeak = 0.75;
constexpr double kDegenerateValue = 2.0;

enum class TriangularScenario
{
    Symmetric,
    LeftSkewed,
    RightSkewed,
    MinimumEndpoint,
    MaximumEndpoint,
    Degenerate
};

template <typename Engine>
class CountingEngine : public Engine
{
public:
    using result_type = typename Engine::result_type;

    explicit CountingEngine(std::uint64_t seed)
        : Engine(seed)
    {
    }

    result_type operator()()
    {
        ++m_callCount;
        return Engine::operator()();
    }

    static constexpr result_type min() noexcept
    {
        return Engine::min();
    }

    static constexpr result_type max() noexcept
    {
        return Engine::max();
    }

    std::uint64_t callCount() const noexcept
    {
        return m_callCount;
    }

private:
    std::uint64_t m_callCount{};
};

template <typename T, TriangularScenario Scenario>
struct ScenarioParameters
{
    static constexpr T minimum = static_cast<T>(kUnitMinimum);
    static constexpr T maximum = static_cast<T>(kUnitMaximum);

    static constexpr T min()
    {
        if constexpr (Scenario == TriangularScenario::Degenerate)
            return static_cast<T>(kDegenerateValue);
        return minimum;
    }

    static constexpr T peak()
    {
        if constexpr (Scenario == TriangularScenario::Symmetric)
            return static_cast<T>(kSymmetricPeak);
        if constexpr (Scenario == TriangularScenario::LeftSkewed)
            return static_cast<T>(kLeftSkewedPeak);
        if constexpr (Scenario == TriangularScenario::RightSkewed)
            return static_cast<T>(kRightSkewedPeak);
        if constexpr (Scenario == TriangularScenario::MinimumEndpoint)
            return minimum;
        if constexpr (Scenario == TriangularScenario::MaximumEndpoint)
            return maximum;
        return static_cast<T>(kDegenerateValue);
    }

    static constexpr T max()
    {
        if constexpr (Scenario == TriangularScenario::Degenerate)
            return static_cast<T>(kDegenerateValue);
        return maximum;
    }
};

template <typename Engine, typename T, TriangularScenario Scenario>
static void BM_RandTriangular(benchmark::State& state)
{
    CountingEngine<Engine> engine{kBenchmarkSeed};
    std::array<T, 3> parameters{
        ScenarioParameters<T, Scenario>::min(),
        ScenarioParameters<T, Scenario>::peak(),
        ScenarioParameters<T, Scenario>::max()};
    benchmark::DoNotOptimize(parameters);
    constexpr std::uint64_t engineBits = std::numeric_limits<typename Engine::result_type>::digits;
    constexpr std::uint64_t sampleBits = std::numeric_limits<T>::digits;
    constexpr std::uint64_t expectedCallsPerSample = Scenario == TriangularScenario::Degenerate
        ? 0 : (sampleBits + engineBits - 1) / engineBits;
    const std::uint64_t initialCallCount = engine.callCount();
    for (auto _ : state)
    {
        T sample = RandX::RandTriangular(engine, parameters[0], parameters[1], parameters[2]);
        benchmark::DoNotOptimize(sample);
    }

    const std::uint64_t engineCalls = engine.callCount() - initialCallCount;
    if (engineCalls != static_cast<std::uint64_t>(state.iterations()) * expectedCallsPerSample)
    {
        state.SkipWithError("三角分布基准的引擎消耗与场景契约不符");
        return;
    }
    state.SetItemsProcessed(state.iterations());
    if (state.iterations() > 0)
        state.counters["engine_calls_per_sample"] =
            static_cast<double>(engineCalls) / static_cast<double>(state.iterations());
}

template <typename Engine, typename T, TriangularScenario Scenario>
static void RegisterTriangularBenchmark(const char* engineWidth, const char* typeName, const char* scenarioName)
{
    const std::string benchmarkName = std::string("BM_RandTriangular/")
        + engineWidth + "/" + typeName + "/" + scenarioName;
    benchmark::RegisterBenchmark(
        benchmarkName.c_str(),
        &BM_RandTriangular<Engine, T, Scenario>);
}

template <typename Engine, typename T>
static void RegisterTriangularScenarios(const char* engineWidth, const char* typeName)
{
    RegisterTriangularBenchmark<Engine, T, TriangularScenario::Symmetric>(engineWidth, typeName, "symmetric");
    RegisterTriangularBenchmark<Engine, T, TriangularScenario::LeftSkewed>(engineWidth, typeName, "left_skewed");
    RegisterTriangularBenchmark<Engine, T, TriangularScenario::RightSkewed>(engineWidth, typeName, "right_skewed");
    RegisterTriangularBenchmark<Engine, T, TriangularScenario::MinimumEndpoint>(engineWidth, typeName, "minimum_endpoint");
    RegisterTriangularBenchmark<Engine, T, TriangularScenario::MaximumEndpoint>(engineWidth, typeName, "maximum_endpoint");
    RegisterTriangularBenchmark<Engine, T, TriangularScenario::Degenerate>(engineWidth, typeName, "degenerate");
}

[[maybe_unused]] static const bool kTriangularBenchmarksRegistered = []() {
    using Engine32 = RandX::Xoshiro128StarStar;
    using Engine64 = RandX::Xoshiro256StarStar;
    RegisterTriangularScenarios<Engine32, float>("engine32", "float");
    RegisterTriangularScenarios<Engine32, double>("engine32", "double");
    RegisterTriangularScenarios<Engine64, float>("engine64", "float");
    RegisterTriangularScenarios<Engine64, double>("engine64", "double");
    return true;
}();
}

BENCHMARK_MAIN();
