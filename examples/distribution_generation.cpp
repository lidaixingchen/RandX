#if defined(RANDX_DISTRIBUTION_EXAMPLE_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <random>
#include <set>
#include <vector>

namespace
{
constexpr std::uint64_t SimulationSeed = 20261004;
constexpr double SampleMean = 0.0;
constexpr double SampleStddev = 1.0;
constexpr std::size_t SampleCount = 32;
constexpr std::size_t FirstBatchCount = 7;
constexpr std::size_t EmptyBatchCount = 0;
constexpr std::size_t ThreadSampleCount = 4;
constexpr int RepeatedValue = 1;
constexpr std::size_t CategoryCount = 3;
constexpr double FirstCategoryWeight = 1.0;
constexpr double SecondCategoryWeight = 2.0;
constexpr double ThirdCategoryWeight = 7.0;
constexpr double BetaAlpha = 2.0;
constexpr double BetaBeta = 5.0;
constexpr double TriangularMinimum = 0.0;
constexpr double TriangularPeak = 2.0;
constexpr double TriangularMaximum = 6.0;
}

int main()
{
    RandX::Xoshiro256StarStar engine{SimulationSeed};
    std::normal_distribution<double> normal{SampleMean, SampleStddev};
    const double first = normal(engine);
    const double second = normal(engine);

    // 生成器捕获引用，连续调用和分批输出共享同一分布的缓存。
    const auto nextNormal = [&normal, &engine]() { return normal(engine); };
    std::array<double, SampleCount> buffer{};
    const auto finish = std::generate_n(buffer.begin(), buffer.size(), nextNormal);

    std::vector<double> samples;
    samples.reserve(SampleCount);
    std::generate_n(std::back_inserter(samples), FirstBatchCount, nextNormal);
    std::generate_n(
        std::back_inserter(samples), SampleCount - FirstBatchCount, nextNormal);

    std::multiset<double> ordered;
    std::generate_n(std::inserter(ordered, ordered.end()), SampleCount, nextNormal);

    // set 的重复插入仍消耗本次样本，生成数量与容器大小分别表达。
    std::uniform_int_distribution<int> repeated{RepeatedValue, RepeatedValue};
    std::set<int> unique;
    std::generate_n(std::inserter(unique, unique.end()), SampleCount,
                    [&repeated, &engine]() { return repeated(engine); });

    const std::array<double, CategoryCount> weights{
        FirstCategoryWeight, SecondCategoryWeight, ThirdCategoryWeight};
    std::discrete_distribution<std::size_t> weighted{weights.begin(), weights.end()};
    std::array<std::size_t, SampleCount> selections{};
    std::generate_n(selections.begin(), selections.size(), [&weighted, &engine]() {
        return RandX::RandWeighted(engine, weighted);
    });

    // RandX 的具名分布函数也可以直接作为标准生成算法的取样来源。
    std::array<double, SampleCount> betaSamples{};
    std::generate_n(betaSamples.begin(), betaSamples.size(), [&engine]() {
        return RandX::RandBeta(engine, BetaAlpha, BetaBeta);
    });
    std::array<double, SampleCount> triangularSamples{};
    std::generate_n(triangularSamples.begin(), triangularSamples.size(), [&engine]() {
        return RandX::RandTriangular(
            engine, TriangularMinimum, TriangularPeak, TriangularMaximum);
    });

    // 零数量时生成器不被调用，默认引擎的首次访问留在生成器内部。
    std::normal_distribution<double> threadNormal{SampleMean, SampleStddev};
    std::vector<double> threadSamples;
    std::generate_n(std::back_inserter(threadSamples), EmptyBatchCount,
                    [&threadNormal]() { return threadNormal(RandX::DefaultEngine()); });
    const std::size_t emptyBatchSize = threadSamples.size();

    // 正数量批次可以先获取一次默认引擎，供整个批次复用。
    if (ThreadSampleCount != EmptyBatchCount)
    {
        auto& threadEngine = RandX::DefaultEngine();
        threadSamples.reserve(ThreadSampleCount);
        std::generate_n(std::back_inserter(threadSamples), ThreadSampleCount,
                        [&threadNormal, &threadEngine]() { return threadNormal(threadEngine); });
    }

    std::cout << "连续正态样本: " << first << ' ' << second << '\n'
              << "array 写入数量: " << std::distance(buffer.begin(), finish) << '\n'
              << "vector 分批追加数量: " << samples.size() << '\n'
              << "multiset 插入数量: " << ordered.size() << '\n'
              << "set 采样数量与元素数量: " << SampleCount << ' ' << unique.size() << '\n'
              << "复用权重分布选取数量: " << selections.size() << '\n'
              << "Beta 与三角分布样本: " << betaSamples.front() << ' '
              << triangularSamples.front() << '\n'
              << "默认引擎零批次数量: " << emptyBatchSize << '\n'
              << "默认引擎正批次数量: " << threadSamples.size() << '\n';
}
