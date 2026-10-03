
#include <benchmark/benchmark.h>
#if __cplusplus >= 202302L
#include <RandX.hpp>
#else
#include <RandX_Cpp17.hpp>
#endif
#include <iostream>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>

constexpr std::uint64_t ExperimentSeed = 20261004;
constexpr std::int64_t SmallPopulation = 256;
constexpr std::int64_t LargePopulation = 65536;
constexpr std::int64_t DenseRequestDivisor = 2;
constexpr std::int64_t DenseRequestOffset = 1;

static void Cases(benchmark::internal::Benchmark* benchmark) {
    for (const auto size : {SmallPopulation, LargePopulation})
        benchmark->Args({size, size / DenseRequestDivisor + DenseRequestOffset});
}

template<class Engine>
static void BM_Dense(benchmark::State& state) {
    const auto size = static_cast<std::size_t>(state.range(0));
    auto request = static_cast<std::size_t>(state.range(1));
    std::vector<int> values(size);
    std::iota(values.begin(), values.end(), 0);
    Engine engine{ExperimentSeed};
    benchmark::DoNotOptimize(request);
    for (auto iteration : state) {
        auto result = RandX::RandSample(engine, values, request);
        auto count = result.size();
        auto data = result.data();
        benchmark::DoNotOptimize(count);
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_Dense, RandX::Xoshiro128StarStar)->Apply(Cases);
BENCHMARK_TEMPLATE(BM_Dense, RandX::Xoshiro256StarStar)->Apply(Cases);
BENCHMARK_TEMPLATE(BM_Dense, std::mt19937_64)->Apply(Cases);

template<class Engine>
static void Verify() {
    for (const auto size : {SmallPopulation, LargePopulation}) {
        std::vector<int> values(static_cast<std::size_t>(size));
        std::iota(values.begin(), values.end(), 0);
        Engine engine{ExperimentSeed};
        const auto request = static_cast<std::size_t>(size / DenseRequestDivisor + DenseRequestOffset);
        const auto result = RandX::RandSample(engine, values, request);
        for (const auto value : result) std::cout << value << ' ';
        std::cout << '\n';
        if constexpr (std::is_same_v<Engine, std::mt19937_64>) {
            std::cout << engine << '\n';
        } else {
            for (const auto value : engine.serialize()) std::cout << value << ' ';
            std::cout << '\n';
        }
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--verify") {
        Verify<RandX::Xoshiro128StarStar>();
        Verify<RandX::Xoshiro256StarStar>();
        Verify<std::mt19937_64>();
        return 0;
    }
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
