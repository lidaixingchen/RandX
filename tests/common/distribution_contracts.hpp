#ifndef RANDX_TESTS_COMMON_DISTRIBUTION_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_DISTRIBUTION_CONTRACTS_HPP

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

#include "integer_distribution_type_contracts.hpp"
#include "triangular_distribution_contracts.hpp"

TEST_SUITE("公共/基础/分布")
{
    TEST_CASE("RandBernoulli 与 RandBool 引擎重载等价")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandBernoulli(rng1, 0.3) == RandX::RandBool(rng2, 0.3));

    }
    TEST_CASE("RandBeta 敏感区间 (1e-3, 0.05) 连续采样稳定性无崩溃")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng(42);
        const double test_params[] = { 0.001001, 0.002, 0.005, 0.01, 0.05, 0.5 };
        for (double p : test_params)
        {
            for (int i = 0; i < 1000; ++i)
            {
                double val = RandX::RandBeta(rng, p, p);
                CHECK(std::isfinite(val));
                CHECK(val >= 0.0);
                CHECK(val <= 1.0);
            }
        }

    }
    TEST_CASE("RandBeta 极不对称参数与浮点下溢边界")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 12345 };

        // 验证 1.0 与 1e303, 1e306, 1e308 的输出为可表示的非零正数
        for (int i = 0; i < 50; ++i)
        {
            double val303 = RandX::RandBeta(rng, 1.0, 1e303);
            CHECK(std::isfinite(val303));
            CHECK(val303 > 0.0);
            CHECK(val303 < 1e-300);

            double val306 = RandX::RandBeta(rng, 1.0, 1e306);
            CHECK(std::isfinite(val306));
            CHECK(val306 > 0.0);
            CHECK(val306 < 1e-300);

            double val308 = RandX::RandBeta(rng, 1.0, 1e308);
            CHECK(std::isfinite(val308));
            CHECK(val308 > 0.0);
            CHECK(val308 < 1e-300);
        }

        // 验证双小尺度参数走对数尺度不抛异常且输出合法
        for (int i = 0; i < 20; ++i)
        {
            double val_small1 = RandX::RandBeta(rng, 1e-10, 1e-10);
            CHECK(std::isfinite(val_small1));
            CHECK(val_small1 >= 0.0);
            CHECK(val_small1 <= 1.0);

            double val_small2 = RandX::RandBeta(rng, 1e-100, 1e-100);
            CHECK(std::isfinite(val_small2));
            CHECK(val_small2 >= 0.0);
            CHECK(val_small2 <= 1.0);
        }

#if defined(LDBL_MAX_10_EXP) && (LDBL_MAX_10_EXP > 308)
        // 若平台支持扩展精度 long double，验证更大指数范围
        for (int i = 0; i < 20; ++i)
        {
            long double val400 = RandX::RandBeta(rng, 1.0L, 1e400L);
            CHECK(std::isfinite(val400));
            CHECK(val400 > 0.0L);
        }
#endif

    }
    TEST_CASE("RandBeta 极小参数采样分布验证（消除虚假0.5）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng(12345);
        const double small_params[] = {1e-308, 1e-310, 1e-320};

        for (double p : small_params)
        {
            double sum = 0.0;
            double sq_sum = 0.0;
            int count_half = 0;
            constexpr int SampleCount = 2000;
            for (int i = 0; i < SampleCount; ++i)
            {
                double val = RandX::RandBeta(rng, p, p);
                CHECK(std::isfinite(val));
                CHECK(val >= 0.0);
                CHECK(val <= 1.0);
                if (val == 0.5)
                {
                    ++count_half;
                }
                sum += val;
                sq_sum += val * val;
            }
            CHECK(count_half == 0);
            const double mean = sum / SampleCount;
            const double var = (sq_sum / SampleCount) - (mean * mean);
            CHECK(mean > RandXTest::TestConstants::kRandBetaMeanLowerBound);
            CHECK(mean < RandXTest::TestConstants::kRandBetaMeanUpperBound);
            CHECK(var > RandXTest::TestConstants::kRandBetaMinimumVariance);
        }

    }
    TEST_CASE("RandBeta 极端参数数值有效性验证")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 100 };
        for (int i = 0; i < 1000; ++i)
        {
            try
            {
                double beta_small = RandX::RandBeta(rng, 0.001, 0.001);
                CHECK(std::isfinite(beta_small));
                CHECK(beta_small >= 0.0);
                CHECK(beta_small <= 1.0);
            }
            catch (const std::domain_error&)
            {
                // 极小参数下两个 Gamma 采样值均下溢为 0.0 时允许抛出合法数值异常
            }

            double beta_large = RandX::RandBeta(rng, 1e6, 1e6);
            CHECK(std::isfinite(beta_large));
            CHECK(beta_large >= 0.0);
            CHECK(beta_large <= 1.0);
        }

    }
    TEST_CASE("RandBeta 极端非正规数参数保持正值输出")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng(123);
        double betaVal = RandX::RandBeta(rng, 1.0, 1e-300);
        CHECK(betaVal > 0.0);

    }
    TEST_CASE("RandBeta 端到端数值行为与大参数验证")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 12345 };
        double sum = 0.0;
        constexpr int SampleCount = 1000;
        for (int i = 0; i < SampleCount; ++i)
        {
            double val = RandX::RandBeta(rng, 2.0, 2.0);
            CHECK(val >= 0.0);
            CHECK(val <= 1.0);
            sum += val;
        }
        double mean = sum / SampleCount;
        CHECK(doctest::Approx(mean).epsilon(RandXTest::TestConstants::kRandBetaMeanTolerance) == 0.5);

        const double largeShape = (std::numeric_limits<double>::max)();
        double big_sample = RandX::RandBeta(rng, largeShape, largeShape);
        CHECK(std::isfinite(big_sample));
        CHECK(big_sample == 0.5);

        double skewed_sample = RandX::RandBeta(rng, 2.0, largeShape);
        CHECK(std::isfinite(skewed_sample));
        CHECK(skewed_sample >= 0.0);
        CHECK(skewed_sample <= 1.0);

    }
    TEST_CASE("RandBeta 范围 [0,1] 与均值")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // a=2, b=2：理论均值 0.5，方差 0.05
        double sum = 0;
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials10000; ++i)
        {
            const double v = RandX::RandBeta(2.0, 2.0);
            CHECK(v >= 0.0);
            CHECK(v <= 1.0);
            sum += v;
        }
        const double mean = sum / RandXTest::TestConstants::kMonteCarloTrials10000;
        CHECK(mean > RandXTest::TestConstants::kRandBetaMeanLowerBound);
        CHECK(mean < RandXTest::TestConstants::kRandBetaMeanUpperBound);

    }
    TEST_CASE("RandBeta 非法形状参数拒绝")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);        CHECK_THROWS_AS((void)RandX::RandBeta(0.0, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(1.0, 0.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(-1.0, 2.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(std::numeric_limits<double>::quiet_NaN(), 1.0), std::invalid_argument);

    }
    TEST_CASE("RandBinomial 范围与均值")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // t=60, p=0.5：理论均值 30，方差 15
        double sum = 0;
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials10000; ++i)
        {
            int v = RandX::RandBinomial(60, 0.5);
            CHECK(v >= 0);
            CHECK(v <= 60);
            sum += v;
        }
        const double mean = sum / RandXTest::TestConstants::kMonteCarloTrials10000;
        CHECK(mean > RandXTest::TestConstants::kRandBinomialMeanLowerBound);
        CHECK(mean < RandXTest::TestConstants::kRandBinomialMeanUpperBound);

    }
    TEST_CASE("RandBool(0.0) 始终 false / RandBool(1.0) 始终 true")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(42);
        for (int i = 0; i < 100; ++i)
            CHECK_FALSE(RandX::RandBool(0.0));
        for (int i = 0; i < 100; ++i)
            CHECK(RandX::RandBool(1.0));

    }
    TEST_CASE("RandCauchy 有限值占绝大多数")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // Cauchy 重尾，理论上 P(|x|<1e6)≈0.99968，inf 极罕见
        int finiteCount = 0;
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            const double v = RandX::RandCauchy(0.0, 1.0);
            if (std::isfinite(v)) ++finiteCount;
        }
        CHECK(finiteCount >= RandXTest::TestConstants::kConvenienceTrials100 - 5);

    }
    TEST_CASE("RandChiSquared 非负")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandChiSquared(4.0) >= 0.0);

    }
    TEST_CASE("RandExtremeValue 有限值")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            const double v = RandX::RandExtremeValue(0.0, 1.0);
            CHECK(std::isfinite(v));
        }

    }
    TEST_CASE("RandFisherF 非负")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandFisherF(5.0, 5.0) >= 0.0);

    }
    TEST_CASE("RandGeometric 64 位类型边界溢出概率")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto checkType = [](auto type)
        {
            using T = decltype(type);
            constexpr int SampleCount = 1000;
            constexpr double SigmaLimit = 6.0;
            const double upperExclusive = std::ldexp(1.0, std::numeric_limits<T>::digits);
            const double probability = 1.0 / upperExclusive;
            RandX::Xoshiro256StarStar engine(RandX::DefaultSeed);
            const auto original = engine;
            CHECK_THROWS_AS(
                (void)(RandX::RandGeometric<RandX::Xoshiro256StarStar, T>(
                    engine, std::nextafter(probability, 0.0))), std::invalid_argument);
            CHECK(engine == original);
            CHECK((RandX::RandGeometric<RandX::Xoshiro256StarStar, T>(engine, 1.0)) == T{0});
            CHECK(engine == original);
            int overflowCount = 0;
            for (int i = 0; i < SampleCount; ++i)
            {
                try
                {
                    const T value = RandX::RandGeometric<RandX::Xoshiro256StarStar, T>(engine, probability);
                    CHECK(value >= T{0});
                }
                catch (const std::overflow_error&)
                {
                    ++overflowCount;
                }
            }
            const double overflowMass = std::exp(std::log1p(-probability) * upperExclusive);
            const double countError = std::sqrt(SampleCount * overflowMass * (1.0 - overflowMass));
            CHECK(std::abs(overflowCount - SampleCount * overflowMass) < SigmaLimit * countError);
        };
        checkType(std::int64_t{});
        checkType(std::uint64_t{});

    }
    TEST_CASE("RandGeometric 大整数区间与低位分布")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto checkEngine = [](auto engine)
        {
            using Engine = decltype(engine);
            constexpr int SampleCount = 100'000;
            constexpr int IntegerSpacingBits = 4;
            constexpr std::size_t ResidueCount = std::size_t{1} << IntegerSpacingBits;
            constexpr int LowerBits = std::numeric_limits<double>::digits + IntegerSpacingBits - 1;
            constexpr auto Lower = std::uint64_t{1} << LowerBits;
            constexpr auto Upper = Lower << 1;
            constexpr double Probability = 1e-17;
            constexpr double SigmaLimit = 6.0;
            // 自由度 15、alpha=0.001 的卡方临界值。
            constexpr double ChiSquareCritical = 37.697;
            std::array<int, ResidueCount> counts{};
            int intervalCount = 0;
            for (int i = 0; i < SampleCount; ++i)
            {
                const auto value = RandX::RandGeometric<Engine, std::uint64_t>(engine, Probability);
                if (value >= Lower && value < Upper)
                {
                    ++intervalCount;
                    ++counts[value % ResidueCount];
                }
            }
            const double logFailure = std::log1p(-Probability);
            const double intervalMass = std::exp(logFailure * static_cast<double>(Lower))
                - std::exp(logFailure * static_cast<double>(Upper));
            const double expectedCount = SampleCount * intervalMass;
            const double countError = std::sqrt(SampleCount * intervalMass * (1.0 - intervalMass));
            CHECK(std::abs(intervalCount - expectedCount) < SigmaLimit * countError);
            REQUIRE(intervalCount > 0);
            const double expectedResidue = static_cast<double>(intervalCount) / ResidueCount;
            double chiSquare = 0.0;
            for (const int count : counts)
            {
                const double difference = count - expectedResidue;
                chiSquare += difference * difference / expectedResidue;
            }
            CHECK(chiSquare < ChiSquareCritical);
        };
        checkEngine(RandX::Xoshiro256StarStar{RandX::DefaultSeed});
        checkEngine(RandX::Xoshiro128StarStar{RandX::DefaultSeed});

    }
    TEST_CASE("RandGeometric 小概率参数与大整数类型安全转换")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng(12345);
        for (int i = 0; i < 50; ++i)
        {
            auto v64 = RandX::RandGeometric<RandX::Xoshiro256StarStar, std::int64_t>(rng, 1e-17);
            CHECK(v64 >= 0);
            auto u64 = RandX::RandGeometric<RandX::Xoshiro256StarStar, std::uint64_t>(rng, 1e-8);
            CHECK(u64 >= 0);
        }
        auto v_default = RandX::RandGeometric<std::int64_t>(1e-17);
        CHECK(v_default >= 0);

    }
    TEST_CASE("RandGeometric 整数边界采样保持引擎状态与溢出语义")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto checkType = [](auto valueType)
        {
            using T = decltype(valueType);
            constexpr int SampleCount = 1000;
            constexpr auto Maximum = (std::numeric_limits<T>::max)();
            const double probability = 1.0 / (static_cast<double>(Maximum) + 1.0);
            RandX::Xoshiro256StarStar wideEngine(RandX::DefaultSeed);
            RandX::Xoshiro256StarStar narrowEngine(RandX::DefaultSeed);
            int overflowCount = 0;
            int successCount = 0;
            for (int i = 0; i < SampleCount; ++i)
            {
                const auto expected = RandX::RandGeometric<RandX::Xoshiro256StarStar, std::uint64_t>(
                    wideEngine, probability);
                if (expected > static_cast<std::uint64_t>(Maximum))
                {
                    CHECK_THROWS_AS(
                        (void)(RandX::RandGeometric<RandX::Xoshiro256StarStar, T>(narrowEngine, probability)),
                        std::overflow_error);
                    ++overflowCount;
                }
                else
                {
                    CHECK((RandX::RandGeometric<RandX::Xoshiro256StarStar, T>(narrowEngine, probability))
                        == static_cast<T>(expected));
                    ++successCount;
                }
                REQUIRE(narrowEngine == wideEngine);
            }
            CHECK(overflowCount > 0);
            CHECK(successCount > 0);
        };
        checkType(std::int16_t{});
        checkType(std::uint16_t{});
        checkType(std::int8_t{});
        checkType(std::uint8_t{});
        checkType(std::int32_t{});
        checkType(std::uint32_t{});

    }
    TEST_CASE("RandGeometric 边界参数 p=1 稳定返回零且不触发断言")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 42 };
        CHECK(RandX::RandGeometric(1.0) == 0);
        CHECK(RandX::RandGeometric(rng, 1.0) == 0);
        CHECK(RandX::RandGeometric<std::uint32_t>(1.0) == 0U);
        CHECK_THROWS_AS((void)RandX::RandGeometric(0.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandGeometric(1.5), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandGeometric<int>(1e-15), std::invalid_argument);

    }
    TEST_CASE("RandGeometric 非负")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandGeometric(0.3) >= 0);

    }
    TEST_CASE("RandInt / RandReal 范围闭/半开区间")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            int v = RandX::RandInt(1, 6);
            CHECK(v >= 1);
            CHECK(v <= 6);
        }
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            double v = RandX::RandReal();
            CHECK(v >= 0.0);
            CHECK(v < 1.0);
        }

    }
    TEST_CASE("RandInt 8-bit 整型与 char 支持")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 42 };
        std::uint8_t u8 = RandX::RandInt<std::uint8_t>(rng, 10, 20);
        CHECK(u8 >= 10);
        CHECK(u8 <= 20);

        std::int8_t i8 = RandX::RandInt<std::int8_t>(rng, -50, 50);
        CHECK(i8 >= -50);
        CHECK(i8 <= 50);

        char c = RandX::RandInt<char>(rng, 'a', 'z');
        CHECK(c >= 'a');
        CHECK(c <= 'z');

    }
    TEST_CASE("RandInt 全范围 [min, max] 极值安全")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(42);
        auto v = RandX::RandInt<std::uint64_t>(0, (std::numeric_limits<std::uint64_t>::max)());
        CHECK(v >= 0);  // 始终成立，极值调用验证
        auto v2 = RandX::RandInt<std::int32_t>((std::numeric_limits<std::int32_t>::min)(), (std::numeric_limits<std::int32_t>::max)());
        CHECK(v2 >= (std::numeric_limits<std::int32_t>::min)());

    }
    TEST_CASE("RandInt(min, min) 始终返回 min")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(42);
        for (int i = 0; i < 100; ++i)
            CHECK(RandX::RandInt(7, 7) == 7);
        for (int i = 0; i < 100; ++i)
            CHECK(RandX::RandInt(-3, -3) == -3);

    }
    TEST_CASE("RandLogNormal 正数")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandLogNormal(0.0, 1.0) > 0.0);

    }
    TEST_CASE("RandNormal 均值接近 0")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        double sum = 0;
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials10000; ++i)
            sum += RandX::RandNormal(0.0, 1.0);
        double mean = sum / RandXTest::TestConstants::kMonteCarloTrials10000;
        CHECK(mean > -RandXTest::TestConstants::kRandNormalMeanAbsoluteTolerance);
        CHECK(mean < RandXTest::TestConstants::kRandNormalMeanAbsoluteTolerance);

    }
    TEST_CASE("RandPoisson 期望为 0 时合法返回 0")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        CHECK(RandX::RandPoisson(0.0) == 0);
        CHECK_THROWS_AS((void)RandX::RandPoisson<int>(1e15), std::invalid_argument);

    }
    TEST_CASE("RandReal 半开区间上界约束 [min, max)")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 42 };
        for (int i = 0; i < 100000; ++i)
        {
            double r = RandX::RandReal(rng, 0.0, 1.0);
            CHECK(r >= 0.0);
            CHECK(r < 1.0);
        }

    }
    TEST_CASE("RandReal(min, min) 始终返回 min")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(42);
        for (int i = 0; i < 100; ++i)
            CHECK(RandX::RandReal(2.5, 2.5) == 2.5);

    }
    TEST_CASE("RandStudentT 有限值占绝大多数")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 自由度 4：有限方差但重尾
        int finiteCount = 0;
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            const double v = RandX::RandStudentT(4.0);
            if (std::isfinite(v)) ++finiteCount;
        }
        CHECK(finiteCount >= RandXTest::TestConstants::kConvenienceTrials100 - 5);

    }
    TEST_CASE("RandWeibull 非负")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandWeibull(2.0, 1.0) >= 0.0);

    }
    TEST_CASE("RandWeighted 单元素权重始终返回 0")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(42);
        std::vector<double> w = {1.0};
        for (int i = 0; i < 50; ++i)
            CHECK(RandX::RandWeighted(w) == 0);

    }
    TEST_CASE("RandWeighted 引擎与离散分布重载")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 42 };
        std::vector<double> weights = { 10.0, 0.0, 0.0 };
        CHECK(RandX::RandWeighted(rng, weights) == 0);

        std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
        CHECK(RandX::RandWeighted(dist) == 0);
        CHECK(RandX::RandWeighted(rng, dist) == 0);

    }
    TEST_CASE("RandWeighted 正权重采样保证")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<double> weights = { 0.0, 0.0, 1.0, 0.0 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandWeighted(weights) == 2);

    }
}

#endif
