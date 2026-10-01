#ifndef RANDX_TESTS_COMMON_STATISTICAL_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_STATISTICAL_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "doctest.h"
#include "fixtures.hpp"

TEST_SUITE("公共/基础/统计")
{
    TEST_CASE("RandChar(CharSet::Hex) 均匀性 ±3σ")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // Hex 字符集 = "0123456789abcdef" 共 16 个字符（标准小写 hex）
        // 各字符期望频率 1/16，N=22000 采样，每字符期望 1375 次
        // σ = sqrt(N * p * (1-p)) = sqrt(22000 * 1/16 * 15/16) ≈ 35.90
        const std::string hexSet = "0123456789abcdef";
        const std::size_t cat = hexSet.size();
        constexpr int SampleCount = 22'000;
        constexpr double SigmaMultiplier = 3.0;
        const int expected = SampleCount / static_cast<int>(cat);
        const double p = 1.0 / static_cast<double>(cat);
        const double sigma = std::sqrt(static_cast<double>(SampleCount) * p * (1.0 - p));
        std::vector<int> counts(cat, 0);
        RandX::Xoshiro256StarStar rng{ 0xC0FFEE };
        for (int i = 0; i < SampleCount; ++i)
        {
            char c = RandX::RandChar(rng, RandX::CharSet::Hex);
            auto idx = hexSet.find(c);
            CHECK(idx != std::string::npos);
            ++counts[idx];
        }
        for (std::size_t i = 0; i < cat; ++i)
        {
            const double dev = std::abs(counts[i] - expected);
            CHECK(dev < SigmaMultiplier * sigma);
        }

    }
    TEST_CASE("RandGeometric 理论均值与概率频率")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr int SampleCount = 100'000;
        constexpr std::size_t ExactBins = 4;
        constexpr double SigmaLimit = 6.0;
        constexpr double CountRoundingAllowance = 1.0;
        const double probabilities[] = {0.8, 0.5, 0.25, 0.01, 1e-8};
        for (const double p : probabilities)
        {
            RandX::Xoshiro256StarStar engine(RandX::DefaultSeed);
            std::array<int, ExactBins + 1> counts{};
            double sum = 0.0;
            for (int i = 0; i < SampleCount; ++i)
            {
                const auto value = RandX::RandGeometric<RandX::Xoshiro256StarStar, std::uint64_t>(engine, p);
                sum += static_cast<double>(value);
                ++counts[value < ExactBins ? static_cast<std::size_t>(value) : ExactBins];
            }
            const double expectedMean = (1.0 - p) / p;
            const double meanError = std::sqrt(1.0 - p) / (p * std::sqrt(static_cast<double>(SampleCount)));
            INFO(p);
            CHECK(std::abs(sum / SampleCount - expectedMean) < SigmaLimit * meanError);
            for (std::size_t bin = 0; bin <= ExactBins; ++bin)
            {
                const double mass = std::pow(1.0 - p, static_cast<double>(bin))
                    * (bin == ExactBins ? 1.0 : p);
                const double expected = SampleCount * mass;
                const double countError = std::sqrt(SampleCount * mass * (1.0 - mass));
                CHECK(std::abs(counts[bin] - expected) <= SigmaLimit * countError + CountRoundingAllowance);
            }
        }

    }
    TEST_CASE("RandSample 固定位宽边界的入选频率")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr std::size_t PopulationSize = std::numeric_limits<std::uint64_t>::digits + 1;
        constexpr std::size_t SampleSize = PopulationSize / 2;
        constexpr int Trials = 4000;
        constexpr double SigmaLimit = 6.0;
        std::vector<std::size_t> population(PopulationSize);
        std::array<int, PopulationSize> counts{};
        for (std::size_t i = 0; i < PopulationSize; ++i) population[i] = i;
        RandX::Xoshiro256StarStar engine(RandX::DefaultSeed);
        for (int trial = 0; trial < Trials; ++trial)
        {
            const auto sample = RandX::RandSample(engine, population, SampleSize);
            REQUIRE(sample.size() == SampleSize);
            const std::set<std::size_t> selected(sample.begin(), sample.end());
            REQUIRE(selected.size() == SampleSize);
            for (const std::size_t index : selected)
            {
                REQUIRE(index < PopulationSize);
                ++counts[index];
            }
        }
        const double inclusionProbability = static_cast<double>(SampleSize) / PopulationSize;
        const double expected = Trials * inclusionProbability;
        const double countError = std::sqrt(Trials * inclusionProbability * (1.0 - inclusionProbability));
        for (const int count : counts)
            CHECK(std::abs(count - expected) < SigmaLimit * countError);

    }
    TEST_CASE("RandSample 输入迭代器路径前后半段均衡")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // N=200, n=10, TRIALS=10000；期望各半段被选 50000 次
        // 水塘抽样算法在完整遍历时各段均匀分布
        constexpr int PopulationSize = 200;
        constexpr int SampleSize = 10;
        constexpr int TrialCount = RandXTest::TestConstants::kMonteCarloTrials10000;
        constexpr int HalfSelectionMinimum = 45'000;
        constexpr int HalfSelectionMaximum = 55'000;
        std::list<int> v;
        for (int i = 0; i < PopulationSize; ++i) v.push_back(i);
        int firstHalf = 0, secondHalf = 0;
        for (int t = 0; t < TrialCount; ++t)
        {
            auto s = RandX::RandSample(v.begin(), v.end(), SampleSize);
            for (int x : s)
            {
                if (x < PopulationSize / 2) ++firstHalf;
                else ++secondHalf;
            }
        }
        // ±10% 容差，统计波动范围检查
        CHECK(firstHalf > HalfSelectionMinimum);
        CHECK(firstHalf < HalfSelectionMaximum);
        CHECK(secondHalf > HalfSelectionMinimum);
        CHECK(secondHalf < HalfSelectionMaximum);

    }
    TEST_CASE("RandSample 随机访问路径前后半段均衡")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // N=200, n=10, TRIALS=10000；期望各半段被选 50000 次
        // 水塘抽样算法在完整遍历时各段均匀分布
        constexpr int PopulationSize = 200;
        constexpr int SampleSize = 10;
        constexpr int TrialCount = RandXTest::TestConstants::kMonteCarloTrials10000;
        constexpr int HalfSelectionMinimum = 45'000;
        constexpr int HalfSelectionMaximum = 55'000;
        std::vector<int> v(static_cast<std::size_t>(PopulationSize));
        for (int i = 0; i < PopulationSize; ++i) v[static_cast<std::size_t>(i)] = i;
        int firstHalf = 0, secondHalf = 0;
        for (int t = 0; t < TrialCount; ++t)
        {
            auto s = RandX::RandSample(v.begin(), v.end(), SampleSize);
            for (int x : s)
            {
                if (x < PopulationSize / 2) ++firstHalf;
                else ++secondHalf;
            }
        }
        // ±10% 容差，统计波动范围检查
        CHECK(firstHalf > HalfSelectionMinimum);
        CHECK(firstHalf < HalfSelectionMaximum);
        CHECK(secondHalf > HalfSelectionMinimum);
        CHECK(secondHalf < HalfSelectionMaximum);

    }
    TEST_CASE("RomuDuoJr 原始输出均匀性 df=127")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr double EXPECTED = static_cast<double>(RandXTest::TestConstants::kStatisticalSampleCount) / RandXTest::TestConstants::kStatisticalBins128;

        RandX::RomuDuoJr rng{ 54321 };
        std::array<int, RandXTest::TestConstants::kStatisticalBins128> counts{};
        for (int i = 0; i < RandXTest::TestConstants::kStatisticalSampleCount; ++i)
            ++counts[rng() & 127];

        double chi2 = 0.0;
        for (int i = 0; i < RandXTest::TestConstants::kStatisticalBins128; ++i)
        {
            const double diff = counts[i] - EXPECTED;
            chi2 += diff * diff / EXPECTED;
        }
        CHECK(chi2 < RandXTest::TestConstants::kChiSquareCriticalDof127);

    }
    TEST_CASE("SFC64 原始输出均匀性 df=127")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr double EXPECTED = static_cast<double>(RandXTest::TestConstants::kStatisticalSampleCount) / RandXTest::TestConstants::kStatisticalBins128;

        RandX::SFC64 rng{ 54321 };
        std::array<int, RandXTest::TestConstants::kStatisticalBins128> counts{};
        for (int i = 0; i < RandXTest::TestConstants::kStatisticalSampleCount; ++i)
            ++counts[rng() & 127];

        double chi2 = 0.0;
        for (int i = 0; i < RandXTest::TestConstants::kStatisticalBins128; ++i)
        {
            const double diff = counts[i] - EXPECTED;
            chi2 += diff * diff / EXPECTED;
        }
        CHECK(chi2 < RandXTest::TestConstants::kChiSquareCriticalDof127);

    }
    TEST_CASE("Xoshiro256StarStar 均匀性 df=99")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr double EXPECTED = static_cast<double>(RandXTest::TestConstants::kStatisticalSampleCount) / RandXTest::TestConstants::kStatisticalBins100;

        RandX::Xoshiro256StarStar rng{ 98765 };
        std::array<int, RandXTest::TestConstants::kStatisticalBins100> counts{};
        for (int i = 0; i < RandXTest::TestConstants::kStatisticalSampleCount; ++i)
            ++counts[RandX::RandInt(rng, 0, RandXTest::TestConstants::kStatisticalBins100 - 1)];

        double chi2 = 0.0;
        for (int i = 0; i < RandXTest::TestConstants::kStatisticalBins100; ++i)
        {
            const double diff = counts[i] - EXPECTED;
            chi2 += diff * diff / EXPECTED;
        }
        CHECK(chi2 < RandXTest::TestConstants::kChiSquareCriticalDof99);

    }
    TEST_CASE("固定种子多标准误统计实验")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr int SampleCount = 100'000;
        constexpr int ExpectedSelectedCount = SampleCount / 2;
        constexpr int FiveSigmaCountAllowance = 790;
        RandX::Xoshiro256StarStar rng1{ 12345 };
        std::vector<double> huge_w = { 1e308, 1e308 };
        int count0 = 0;
        for (int i = 0; i < SampleCount; ++i)
        {
            if (RandX::RandWeighted(rng1, huge_w) == 0)
                ++count0;
        }
        // 5σ allowance for a binomial count with p = 0.5.
        CHECK(std::abs(count0 - ExpectedSelectedCount) < FiveSigmaCountAllowance);

        RandX::Xoshiro256StarStar rng2{ 12345 };
        std::vector<double> norm_w = { 1.0, 1.0 };
        int norm_count0 = 0;
        for (int i = 0; i < SampleCount; ++i)
        {
            if (RandX::RandWeighted(rng2, norm_w) == 0)
                ++norm_count0;
        }
        CHECK(std::abs(norm_count0 - ExpectedSelectedCount) < FiveSigmaCountAllowance);

    }
    TEST_CASE("均匀性（hash-set）：前后半段均衡")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr int PopulationSize = 10'000;
        constexpr int SampleSize = 5;
        constexpr int TrialCount = RandXTest::TestConstants::kMonteCarloTrials10000;
        constexpr int ThresholdMultiplier = 64;
        constexpr int RelativeTolerancePercent = 15;
        constexpr int ExpectedHalfSelectionCount = TrialCount * SampleSize / 2;
        constexpr int HalfSelectionTolerance = ExpectedHalfSelectionCount * RelativeTolerancePercent / 100;
        constexpr int HalfSelectionLowerBound = ExpectedHalfSelectionCount - HalfSelectionTolerance;
        constexpr int HalfSelectionUpperBound = ExpectedHalfSelectionCount + HalfSelectionTolerance;
        static_assert(SampleSize * ThresholdMultiplier < PopulationSize);
        std::vector<int> v(static_cast<std::size_t>(PopulationSize));
        for (int i = 0; i < PopulationSize; ++i) v[static_cast<std::size_t>(i)] = i;
        int firstHalf = 0, secondHalf = 0;
        // RandSample 以 SampleSize * ThresholdMultiplier < PopulationSize 选择 hash-set 分支。
        for (int t = 0; t < TrialCount; ++t)
        {
            auto s = RandX::RandSample(v.begin(), v.end(), SampleSize);
            for (int x : s)
            {
                if (x < PopulationSize / 2) ++firstHalf;
                else ++secondHalf;
            }
        }
        CHECK(firstHalf > HalfSelectionLowerBound);
        CHECK(firstHalf < HalfSelectionUpperBound);
        CHECK(secondHalf > HalfSelectionLowerBound);
        CHECK(secondHalf < HalfSelectionUpperBound);

    }
}

#endif
