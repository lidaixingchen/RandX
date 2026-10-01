#ifndef RANDX_TESTS_IMPLEMENTATION_CONTRACTS_HPP
#define RANDX_TESTS_IMPLEMENTATION_CONTRACTS_HPP

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
#include "../common/fixtures.hpp"

TEST_SUITE("内部/分布辅助计算")
{
    TEST_CASE("NormalizeBetaSample 正值归一化与非法输入")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        CHECK(RandX::detail::NormalizeBetaSample(1.0, 1.0) == 0.5);
        CHECK(doctest::Approx(RandX::detail::NormalizeBetaSample(1e308, 1e308)).epsilon(1e-12) == 0.5);
        double ratio = RandX::detail::NormalizeBetaSample(1e308, 5e307);
        CHECK(doctest::Approx(ratio).epsilon(1e-12) == (1.0 / 1.5));
        CHECK(RandX::detail::NormalizeBetaSample(0.0, 5.0) == 0.0);
        CHECK(RandX::detail::NormalizeBetaSample(5.0, 0.0) == 1.0);

        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(0.0, 0.0), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(-1.0, 1.0), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(1.0, -1.0), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(std::numeric_limits<double>::infinity(), 1.0), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(1.0, std::numeric_limits<double>::infinity()), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(std::numeric_limits<double>::quiet_NaN(), 1.0), std::domain_error);


    }
    TEST_CASE("RandBeta 对数尺度辅助函数处理极大有限值")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);        // 验证对数域 helper 避免除法直接溢出 (d_a = 2/3, d_b = DBL_MAX)
        double helper_val = RandX::detail::ComputeBetaSampleFromLogScale(
            2.0 / 3.0, 0.0, std::numeric_limits<double>::max(), 0.0);
        CHECK(std::isfinite(helper_val));
        CHECK(helper_val > 0.0);


    }
}

TEST_SUITE("内部/序列化格式")
{
#if defined(__GLIBCXX__)
    TEST_CASE("构造期间获取 fill 抛出异常时可被捕获且不导致进程终止")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::locale loc(std::locale::classic(), new RandXTest::StreamFormatFixtures::ThrowingWidenFacet);
        RandXTest::StreamFormatFixtures::ThrowingFillStream os(loc);

        bool threw = false;
        try
        {
            RandX::detail::StreamFormatGuard<wchar_t, std::char_traits<wchar_t>> guard(os);
        }
        catch (const std::runtime_error& e)
        {
            threw = true;
            CHECK(std::string(e.what()) == "ctype widen failure");
        }
        CHECK(threw);

    }
#endif
}

TEST_SUITE("内部/抽样策略")
{
    TEST_CASE("抽样内核仅在需要随机抽取时获取一次引擎")
    {
        using Diff = std::ptrdiff_t;
        constexpr Diff PopulationSize = RandX::detail::HashSetThresholdK * 2;
        constexpr Diff SparseCount = 1;
        constexpr Diff DenseCount = PopulationSize / 2;
        std::vector<int> population(static_cast<std::size_t>(PopulationSize));
        for (Diff i = 0; i < PopulationSize; ++i)
            population[static_cast<std::size_t>(i)] = static_cast<int>(i);
        RandX::Xoshiro256StarStar engine(RandX::DefaultSeed);
        std::size_t acquisitions = 0;
        const auto getter = [&]() -> RandX::Xoshiro256StarStar&
        {
            ++acquisitions;
            return engine;
        };
        auto first = population.begin();
        const auto checkRandomAccess = [&](Diff size, Diff count, std::size_t expectedAcquisitions)
        {
            acquisitions = 0;
            const auto result = RandX::detail::SampleRandomAccess<
                int, Diff, RandX::detail::SampleDistributionLifetime::Selection>(first, size, count, getter);
            CHECK(acquisitions == expectedAcquisitions);
            CHECK(result.size() == static_cast<std::size_t>((std::max)(Diff{0}, (std::min)(count, size))));
        };
        checkRandomAccess(PopulationSize, 0, 0);
        checkRandomAccess(0, SparseCount, 0);
        checkRandomAccess(PopulationSize, PopulationSize, 0);
        checkRandomAccess(PopulationSize, SparseCount, 1);
        checkRandomAccess(PopulationSize, DenseCount, 1);

        const auto checkReservoir = [&](const std::string& source, Diff count, std::size_t expectedAcquisitions)
        {
            acquisitions = 0;
            std::istringstream stream(source);
            std::istream_iterator<int> current(stream);
            std::istream_iterator<int> last;
            (void)RandX::detail::SampleReservoir<int, Diff>(current, last, count, getter);
            CHECK(acquisitions == expectedAcquisitions);
        };
        checkReservoir("1 2 3", 0, 0);
        checkReservoir("", SparseCount, 0);
        checkReservoir("1", SparseCount, 0);
        checkReservoir("1 2 3", SparseCount, 1);
    }

    TEST_CASE("蓄水池获取引擎时输入已完成初填")
    {
        using Diff = std::ptrdiff_t;
        constexpr Diff SampleCount = 2;
        constexpr int NextValue = 3;
        std::istringstream stream("1 2 3 4");
        std::istream_iterator<int> first(stream);
        std::istream_iterator<int> last;
        const auto getter = [&]() -> RandX::Xoshiro256StarStar&
        {
            CHECK(*first == NextValue);
            throw std::runtime_error("engine acquisition failure");
        };
        CHECK_THROWS_AS((RandX::detail::SampleReservoir<int, Diff>(first, last, SampleCount, getter)),
                        std::runtime_error);
        CHECK(*first == NextValue);
    }

    TEST_CASE("RandSample 容器策略阈值分支")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr std::size_t PopulationSize = RandX::detail::SampleBitmapThresholdK * 2;
        constexpr std::size_t SparseLimit = (PopulationSize - 1) / RandX::detail::SampleBitmapThresholdK;
        const std::size_t sampleSizes[] = {
            SparseLimit, SparseLimit + 1, PopulationSize / 2, PopulationSize / 2 + 1,
            PopulationSize, (std::numeric_limits<std::size_t>::max)()
        };
        std::vector<std::size_t> population(PopulationSize);
        for (std::size_t i = 0; i < PopulationSize; ++i) population[i] = i;
        const auto checkEngine = [&](auto engine)
        {
            for (const std::size_t n : sampleSizes)
            {
                const auto sample = RandX::RandSample(engine, population, n);
                const std::size_t expected = (std::min)(n, PopulationSize);
                REQUIRE(sample.size() == expected);
                const std::set<std::size_t> selected(sample.begin(), sample.end());
                REQUIRE(selected.size() == expected);
                CHECK(*selected.rbegin() < PopulationSize);
            }
        };
        checkEngine(RandX::Xoshiro256StarStar{RandX::DefaultSeed});
        checkEngine(RandX::Xoshiro128StarStar{RandX::DefaultSeed});
        for (const std::size_t n : sampleSizes)
        {
            RandX::Xoshiro256StarStar engine(RandX::DefaultSeed);
            RandX::Reseed(RandX::DefaultSeed);
            CHECK(RandX::RandSample(population, n) == RandX::RandSample(engine, population, n));
            CHECK(RandX::DefaultEngine() == engine);
        }

    }
}

#endif
