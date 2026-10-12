#ifndef RANDX_TESTS_COMMON_SCALED_DISTRIBUTION_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_SCALED_DISTRIBUTION_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>
#include <stdexcept>
#include "doctest.h"
#include "fixtures.hpp"

namespace RandXTest
{
    namespace ScaledDistributionFixtures
    {
        class ScriptedThenXoshiroEngine
        {
        public:
            using result_type = std::uint64_t;

            explicit ScriptedThenXoshiroEngine(std::initializer_list<result_type> values)
                : size_(values.size()), fallback_(FallbackSeed)
            {
                std::copy(values.begin(), values.end(), values_.begin());
            }

            static constexpr result_type min() noexcept { return 0; }
            static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }

            result_type operator()()
            {
                const result_type value = index_ < size_ ? values_[index_] : fallback_();
                ++index_;
                return value;
            }

            std::size_t calls() const noexcept { return index_; }

        private:
            static constexpr std::size_t Capacity = 8;
            static constexpr std::uint64_t FallbackSeed = 0x5c3e7149ULL;
            std::array<result_type, Capacity> values_{};
            std::size_t size_{0};
            std::size_t index_{0};
            RandX::Xoshiro256StarStar fallback_;
        };

        class RepeatedEngine
        {
        public:
            using result_type = std::uint64_t;
            explicit RepeatedEngine(result_type value) : value_(value) {}
            static constexpr result_type min() noexcept { return 0; }
            static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }
            result_type operator()() noexcept { ++calls_; return value_; }
            std::size_t calls() const noexcept { return calls_; }
        private:
            result_type value_;
            std::size_t calls_{0};
        };

        class CountingEngine
        {
        public:
            using result_type = std::uint64_t;

            static constexpr result_type min() noexcept { return 0; }
            static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }

            result_type operator()() noexcept
            {
                ++call_count;
                return 0;
            }

            std::size_t call_count{0};
        };
    }
}

TEST_SUITE("公共/基础/缩放分布")
{
    TEST_CASE("缩放分布保留单位样本下溢后的有限尾部")
    {
        constexpr std::uint64_t Half = 0x8000000000000000ULL;
        constexpr std::uint64_t ThreeQuarters = 0xc000000000000000ULL;
        constexpr long double GammaUnderflowPower = 1.5L;
        constexpr long double WeibullRescuePower = 0.5L;
        const auto verifyType = [=](auto type_tag)
        {
            using T = decltype(type_tag);
            using WorkT = std::conditional_t<std::is_same_v<T, float>, double, T>;
            const T maximum = (std::numeric_limits<T>::max)();
            const T minimum = std::numeric_limits<T>::denorm_min() > T{0}
                ? std::numeric_limits<T>::denorm_min() : std::numeric_limits<T>::min();
            const WorkT log_maximum = std::log(static_cast<WorkT>(maximum));
            const WorkT log_minimum = std::log(static_cast<WorkT>(minimum));
            RandXTest::ScaledDistributionFixtures::ScriptedThenXoshiroEngine reference{ Half };
            std::exponential_distribution<WorkT> unit_exponential(WorkT{1});
            const WorkT exponential_sample = unit_exponential(reference);
            const T gamma_shape = static_cast<T>(exponential_sample /
                (static_cast<WorkT>(GammaUnderflowPower) * log_maximum));
            RandXTest::ScaledDistributionFixtures::ScriptedThenXoshiroEngine gamma{
                Half, ThreeQuarters, Half, Half
            };
            const T gamma_value = RandX::RandGamma(gamma, gamma_shape, maximum);
            CHECK(std::isfinite(gamma_value));
            CHECK(gamma_value > T{0});
            CHECK(gamma_value < T{1});

            const WorkT log_unit = std::log(exponential_sample);
            const T weibull_shape = static_cast<T>(log_unit /
                (log_minimum - static_cast<WorkT>(WeibullRescuePower) * log_maximum));
            RandXTest::ScaledDistributionFixtures::ScriptedThenXoshiroEngine weibull{ Half };
            const T weibull_value = RandX::RandWeibull(weibull, weibull_shape, maximum);
            const T expected_weibull = static_cast<T>(std::exp(
                log_unit / static_cast<WorkT>(weibull_shape) + log_maximum));
            CHECK(std::isfinite(weibull_value));
            CHECK(weibull_value > T{0});
            CHECK(weibull_value == expected_weibull);
        };
        verifyType(float{});
        verifyType(double{});
        verifyType(static_cast<long double>(0));
    }

    TEST_CASE("相同小自由度 Gamma 尾部的 Fisher 比值为一")
    {
        constexpr std::uint64_t Quarter = 0x4000000000000000ULL;
        const auto verifyType = [=](auto type_tag)
        {
            using T = decltype(type_tag);
            constexpr T Degrees = static_cast<T>(0.003L);
            RandXTest::ScaledDistributionFixtures::RepeatedEngine engine{ Quarter };
            const T value = RandX::RandFisherF(engine, Degrees, Degrees);
            CHECK(value == T{1});
            CHECK(engine.calls() > 0);
            CHECK(engine.calls() % 2 == 0);
        };
        verifyType(float{});
        verifyType(double{});
        verifyType(static_cast<long double>(0));
    }

    TEST_CASE("小形状缩放入口与显式引擎保持一致且安全参数保留标准序列")
    {
        constexpr std::uint64_t ScaledSampleSeed = 0x7391a5c2ULL;
        constexpr std::uint64_t StandardSampleSeed = 0x184b2d67ULL;
        const auto saved_default_state = RandX::DefaultEngine().serialize();

        const auto verifyType = [&](auto type_tag)
        {
            using T = decltype(type_tag);
            const T small_shape = static_cast<T>(1.0e-6);
            const T maximum = (std::numeric_limits<T>::max)();

            RandX::Xoshiro256StarStar gamma_actual{ ScaledSampleSeed };
            const auto gamma_initial_state = gamma_actual.serialize();
            const T gamma_value = RandX::RandGamma(gamma_actual, small_shape, maximum);
            CHECK(!std::isnan(gamma_value));
            CHECK(gamma_actual.serialize() != gamma_initial_state);

            RandX::DefaultEngine() = RandX::Xoshiro256StarStar{ ScaledSampleSeed };
            const T gamma_default_value = RandX::RandGamma(small_shape, maximum);
            CHECK(gamma_default_value == gamma_value);
            CHECK(RandX::DefaultEngine().serialize() == gamma_actual.serialize());

            RandX::Xoshiro256StarStar weibull_actual{ ScaledSampleSeed };
            const auto weibull_initial_state = weibull_actual.serialize();
            const T weibull_value = RandX::RandWeibull(weibull_actual, small_shape, maximum);
            CHECK(!std::isnan(weibull_value));
            CHECK(weibull_actual.serialize() != weibull_initial_state);

            RandX::DefaultEngine() = RandX::Xoshiro256StarStar{ ScaledSampleSeed };
            const T weibull_default_value = RandX::RandWeibull(small_shape, maximum);
            CHECK(weibull_default_value == weibull_value);
            CHECK(RandX::DefaultEngine().serialize() == weibull_actual.serialize());

            const T safe_shape = T{0.5};
            const T safe_scale = T{1};
            RandX::Xoshiro256StarStar standard_gamma_actual{ StandardSampleSeed };
            RandX::Xoshiro256StarStar standard_gamma_expected{ StandardSampleSeed };
            std::gamma_distribution<T> standard_gamma(safe_shape, safe_scale);
            CHECK(RandX::RandGamma(standard_gamma_actual, safe_shape, safe_scale) ==
                standard_gamma(standard_gamma_expected));
            CHECK(standard_gamma_actual.serialize() == standard_gamma_expected.serialize());

            RandX::Xoshiro256StarStar standard_weibull_actual{ StandardSampleSeed };
            RandX::Xoshiro256StarStar standard_weibull_expected{ StandardSampleSeed };
            std::weibull_distribution<T> standard_weibull(safe_shape, safe_scale);
            CHECK(RandX::RandWeibull(standard_weibull_actual, safe_shape, safe_scale) ==
                standard_weibull(standard_weibull_expected));
            CHECK(standard_weibull_actual.serialize() == standard_weibull_expected.serialize());
        };

        verifyType(float{});
        verifyType(double{});
        verifyType(static_cast<long double>(0));

        RandX::DefaultEngine().deserialize(saved_default_state);
    }

    TEST_CASE("小自由度稳定路径覆盖 Gamma 小形状并保持有效引擎状态")
    {
        constexpr std::uint64_t StudentSeed = 0x2957d31bULL;
        constexpr std::uint64_t FisherSeed = 0x648f0ca5ULL;
        const auto saved_default_state = RandX::DefaultEngine().serialize();

        const auto verifyType = [&](auto type_tag)
        {
            using T = decltype(type_tag);
            const T small_degrees = static_cast<T>(0.01);

            RandX::Xoshiro256StarStar student_actual{ StudentSeed };
            const auto student_initial_state = student_actual.serialize();
            const T student_value = RandX::RandStudentT(student_actual, small_degrees);
            CHECK(!std::isnan(student_value));
            CHECK(student_actual.serialize() != student_initial_state);

            RandX::DefaultEngine() = RandX::Xoshiro256StarStar{ StudentSeed };
            const T student_default_value = RandX::RandStudentT(small_degrees);
            CHECK(student_default_value == student_value);
            CHECK(RandX::DefaultEngine().serialize() == student_actual.serialize());

            RandX::Xoshiro256StarStar fisher_actual{ FisherSeed };
            const auto fisher_initial_state = fisher_actual.serialize();
            const T fisher_value = RandX::RandFisherF(fisher_actual, small_degrees, small_degrees);
            CHECK(!std::isnan(fisher_value));
            CHECK(fisher_value >= T{0});
            CHECK(fisher_actual.serialize() != fisher_initial_state);

            RandX::DefaultEngine() = RandX::Xoshiro256StarStar{ FisherSeed };
            const T fisher_default_value = RandX::RandFisherF(small_degrees, small_degrees);
            CHECK(fisher_default_value == fisher_value);
            CHECK(RandX::DefaultEngine().serialize() == fisher_actual.serialize());

            RandX::Xoshiro256StarStar safe_student_actual{ StudentSeed };
            RandX::Xoshiro256StarStar safe_student_expected{ StudentSeed };
            const T safe_degrees = T{2};
            std::student_t_distribution<T> standard_student(safe_degrees);
            CHECK(RandX::RandStudentT(safe_student_actual, safe_degrees) ==
                standard_student(safe_student_expected));
            CHECK(safe_student_actual.serialize() == safe_student_expected.serialize());

            RandX::Xoshiro256StarStar safe_fisher_actual{ FisherSeed };
            RandX::Xoshiro256StarStar safe_fisher_expected{ FisherSeed };
            std::fisher_f_distribution<T> standard_fisher(safe_degrees, safe_degrees);
            CHECK(RandX::RandFisherF(safe_fisher_actual, safe_degrees, safe_degrees) ==
                standard_fisher(safe_fisher_expected));
            CHECK(safe_fisher_actual.serialize() == safe_fisher_expected.serialize());
        };

        verifyType(float{});
        verifyType(double{});
        verifyType(static_cast<long double>(0));

#if defined(__GNUC__) && !defined(__clang__)
        constexpr std::uint64_t Half = 0x8000000000000000ULL;
        constexpr std::uint64_t ThreeQuarters = 0xc000000000000000ULL;
        constexpr std::uint64_t PowerUniform = 0x0200000000000000ULL;
        constexpr std::uint64_t FourFifths = 0xcccccccccccccccdULL;
        constexpr double SmallDegrees = 0.01;

        RandXTest::ScaledDistributionFixtures::ScriptedThenXoshiroEngine fisher{
            ThreeQuarters, Half, Half, PowerUniform,
            ThreeQuarters, Half, Half, PowerUniform
        };
        const double fisher_value = RandX::RandFisherF(fisher, SmallDegrees, SmallDegrees);
        CHECK(!std::isnan(fisher_value));
        CHECK(fisher_value >= 0.0);
        CHECK(fisher.calls() >= 8);

        RandXTest::ScaledDistributionFixtures::ScriptedThenXoshiroEngine student{
            Half, FourFifths, ThreeQuarters, Half, Half, PowerUniform
        };
        const double student_value = RandX::RandStudentT(student, SmallDegrees);
        CHECK(std::isfinite(student_value));
        CHECK(student.calls() >= 6);
#endif

        RandX::DefaultEngine().deserialize(saved_default_state);
    }

    TEST_CASE("分布非法零参数在读取引擎前抛出")
    {
        RandXTest::ScaledDistributionFixtures::CountingEngine engine;

        CHECK_THROWS_AS((void)RandX::RandGamma(engine, 0.0, 1.0), std::invalid_argument);
        CHECK(engine.call_count == 0);
        CHECK_THROWS_AS((void)RandX::RandWeibull(engine, 0.0, 1.0), std::invalid_argument);
        CHECK(engine.call_count == 0);
        CHECK_THROWS_AS((void)RandX::RandStudentT(engine, 0.0), std::invalid_argument);
        CHECK(engine.call_count == 0);
        CHECK_THROWS_AS((void)RandX::RandFisherF(engine, 0.0, 1.0), std::invalid_argument);
        CHECK(engine.call_count == 0);
    }
}

#endif
