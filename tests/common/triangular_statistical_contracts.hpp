#ifndef TESTS_COMMON_TRIANGULAR_STATISTICAL_CONTRACTS_HPP
#define TESTS_COMMON_TRIANGULAR_STATISTICAL_CONTRACTS_HPP

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

namespace RandXTest::TriangularStatistics
{
constexpr std::size_t SampleCount = 65'536;
constexpr std::size_t BinCount = 16;
constexpr std::array<long double, 5> Peaks{0.0L, 0.25L, 0.5L, 0.75L, 1.0L};
constexpr std::size_t TypeCount = 3;
constexpr std::size_t EngineCount = 2;
constexpr std::size_t ComparisonCount = TypeCount * EngineCount * Peaks.size() * (BinCount + 2);
constexpr long double FamilyErrorProbability = 1e-6L;
constexpr std::uint64_t Seed = 0x725EF19BULL;

inline long double Cdf(long double x, long double peak)
{
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    if (x < peak) return x * x / peak;
    const long double tail = 1 - x;
    return 1 - tail * tail / (1 - peak);
}

inline long double BernsteinBound(long double variance, long double range)
{
    const long double confidence = std::log(2 * ComparisonCount / FamilyErrorProbability);
    const long double count = static_cast<long double>(SampleCount);
    return std::sqrt(2 * variance * confidence / count)
        + (2.0L / 3.0L) * range * confidence / count;
}

template<class T, class Engine>
void CheckMomentsAndBins()
{
    const long double grid = std::ldexp(1.0L, -std::numeric_limits<T>::digits);
    const long double rounding = std::numeric_limits<T>::epsilon();
    const long double momentBias = grid + rounding;
    const long double binBias = 2 * grid + 4 * rounding;
    for (const long double peak : Peaks)
    {
        INFO("众数 = " << peak << ", 有效位 = " << std::numeric_limits<T>::digits);
        Engine engine{Seed};
        const long double mean = (1 + peak) / 3;
        const long double variance = (1 - peak + peak * peak) / 18;
        const long double rawSecond = (1 + peak + peak * peak) / 6;
        const long double rawThird = (1 + peak + peak * peak + peak * peak * peak) / 10;
        const long double rawFourth = (1 + peak + peak * peak + peak * peak * peak
            + peak * peak * peak * peak) / 15;
        const long double fourth = rawFourth - 4 * mean * rawThird
            + 6 * mean * mean * rawSecond - 3 * mean * mean * mean * mean;
        long double sum = 0;
        long double centralSecond = 0;
        std::array<std::size_t, BinCount> counts{};
        for (std::size_t index = 0; index < SampleCount; ++index)
        {
            const T value = RandX::RandTriangular(engine, T{0}, static_cast<T>(peak), T{1});
            REQUIRE(std::isfinite(value));
            REQUIRE(value >= T{0});
            REQUIRE(value < T{1});
            const long double sample = static_cast<long double>(value);
            sum += sample;
            const long double distance = sample - mean;
            centralSecond += distance * distance;
            const auto bin = static_cast<std::size_t>(sample * BinCount);
            ++counts[bin];
        }
        const long double count = static_cast<long double>(SampleCount);
        CHECK(std::abs(sum / count - mean) <= BernsteinBound(variance, 1) + momentBias);
        CHECK(std::abs(centralSecond / count - variance)
            <= BernsteinBound(fourth - variance * variance, 1) + 2 * momentBias);
        for (std::size_t bin = 0; bin < BinCount; ++bin)
        {
            const long double lower = static_cast<long double>(bin) / BinCount;
            const long double upper = static_cast<long double>(bin + 1) / BinCount;
            const long double probability = Cdf(upper, peak) - Cdf(lower, peak);
            CHECK(std::abs(static_cast<long double>(counts[bin]) / count - probability)
                <= BernsteinBound(probability * (1 - probability), 1) + binBias);
        }
    }
}
}

TEST_SUITE("公共/基础/统计")
{
    TEST_CASE("RandTriangular 均值、方差与独立 CDF 分箱")
    {
        using namespace RandXTest::TriangularStatistics;
        CheckMomentsAndBins<float, RandX::Xoshiro256StarStar>();
        CheckMomentsAndBins<double, RandX::Xoshiro256StarStar>();
        CheckMomentsAndBins<long double, RandX::Xoshiro256StarStar>();
        CheckMomentsAndBins<float, RandX::Xoshiro128StarStar>();
        CheckMomentsAndBins<double, RandX::Xoshiro128StarStar>();
        CheckMomentsAndBins<long double, RandX::Xoshiro128StarStar>();
    }
}

#endif
