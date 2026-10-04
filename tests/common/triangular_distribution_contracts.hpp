#ifndef TESTS_COMMON_TRIANGULAR_DISTRIBUTION_CONTRACTS_HPP
#define TESTS_COMMON_TRIANGULAR_DISTRIBUTION_CONTRACTS_HPP

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

namespace RandXTest::TriangularPublicFixtures
{
struct FixedCanonicalEngine64
{
    using result_type = std::uint64_t;

    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }

    explicit FixedCanonicalEngine64(const result_type value) noexcept : next_value(value) {}

    result_type operator()() noexcept
    {
        ++call_count;
        return next_value;
    }

    result_type next_value;
    std::size_t call_count{0};
};

inline std::uint64_t EncodeCanonicalUniform(const double uniform) noexcept
{
    constexpr int EngineBits = std::numeric_limits<std::uint64_t>::digits;
    constexpr int CanonicalBits = std::numeric_limits<double>::digits;
    const std::uint64_t canonicalValue = static_cast<std::uint64_t>(std::ldexp(uniform, CanonicalBits));
    return canonicalValue << (EngineBits - CanonicalBits);
}

template<class T>
void CheckInvalidAndDegenerateParameters()
{
    using Engine = RandXTest::ExtendedFixtures::CallCountingEngine<
        std::uint64_t, 0, (std::numeric_limits<std::uint64_t>::max)()>;
    Engine engine;
    const std::array<T, 3> invalidValues{
        std::numeric_limits<T>::quiet_NaN(), std::numeric_limits<T>::infinity(),
        -std::numeric_limits<T>::infinity()};
    for (const T invalid : invalidValues)
    {
        CHECK_THROWS_AS((void)RandX::RandTriangular(engine, invalid, T{0.5}, T{1}), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandTriangular(engine, T{0}, invalid, T{1}), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandTriangular(engine, T{0}, T{0.5}, invalid), std::invalid_argument);
    }
    CHECK_THROWS_AS((void)RandX::RandTriangular(engine, -(std::numeric_limits<T>::max)(), T{0},
        (std::numeric_limits<T>::max)()), std::invalid_argument);
    CHECK(RandX::RandTriangular(engine, T{2}, T{2}, T{2}) == T{2});
    CHECK(std::signbit(RandX::RandTriangular(engine, -T{0}, T{0}, -T{0})));
    CHECK(engine.call_count == 0);
}
}

TEST_SUITE("公共/基础/分布")
{
    TEST_CASE("RandTriangular 参数、退化和引擎消耗契约")
    {
        RandXTest::TriangularPublicFixtures::CheckInvalidAndDegenerateParameters<float>();
        RandXTest::TriangularPublicFixtures::CheckInvalidAndDegenerateParameters<double>();
        RandXTest::TriangularPublicFixtures::CheckInvalidAndDegenerateParameters<long double>();
        using Engine64 = RandXTest::ExtendedFixtures::CallCountingEngine<
            std::uint64_t, 0, (std::numeric_limits<std::uint64_t>::max)()>;
        using Engine32 = RandXTest::ExtendedFixtures::CallCountingEngine<
            std::uint32_t, 0, (std::numeric_limits<std::uint32_t>::max)()>;
        static_assert(std::is_same_v<decltype(RandX::RandTriangular(0.0F, 0.5F, 1.0F)), float>);
        static_assert(std::is_same_v<decltype(RandX::RandTriangular(0.0, 0.5, 1.0)), double>);
        static_assert(std::is_same_v<decltype(RandX::RandTriangular(0.0L, 0.5L, 1.0L)), long double>);
        static_assert(std::is_same_v<
            decltype(RandX::RandTriangular(std::declval<Engine64&>(), 0.0F, 0.5F, 1.0F)), float>);

        Engine64 engine64;
        const float floatSample = RandX::RandTriangular(engine64, 0.0F, 0.25F, 1.0F);
        CHECK(std::isfinite(floatSample));
        CHECK(floatSample >= 0.0F);
        CHECK(floatSample < 1.0F);
        CHECK(engine64.call_count == 1);

        Engine64 doubleEngine64;
        const double doubleSample = RandX::RandTriangular(doubleEngine64, 0.0, 0.25, 1.0);
        CHECK(std::isfinite(doubleSample));
        CHECK(doubleSample >= 0.0);
        CHECK(doubleSample < 1.0);
        CHECK(doubleEngine64.call_count == 1);

        Engine32 floatEngine32;
        (void)RandX::RandTriangular(floatEngine32, 0.0F, 0.25F, 1.0F);
        CHECK(floatEngine32.call_count == 1);

        Engine32 doubleEngine32;
        (void)RandX::RandTriangular(doubleEngine32, 0.0, 0.25, 1.0);
        CHECK(doubleEngine32.call_count == 2);

        Engine64 longDoubleEngine64;
        Engine32 longDoubleEngine32;
        CHECK(std::isfinite(RandX::RandTriangular(longDoubleEngine64, 0.0L, 0.5L, 1.0L)));
        CHECK(std::isfinite(RandX::RandTriangular(longDoubleEngine32, 0.0L, 0.5L, 1.0L)));
        constexpr int LongDoubleBits = std::numeric_limits<long double>::digits;
        constexpr int Engine64Bits = std::numeric_limits<typename Engine64::result_type>::digits;
        constexpr int Engine32Bits = std::numeric_limits<typename Engine32::result_type>::digits;
        CHECK(longDoubleEngine64.call_count == (LongDoubleBits + Engine64Bits - 1) / Engine64Bits);
        CHECK(longDoubleEngine32.call_count == (LongDoubleBits + Engine32Bits - 1) / Engine32Bits);

        Engine64 noDrawEngine;
        CHECK(RandX::RandTriangular(noDrawEngine, 3.0, 3.0, 3.0) == 3.0);
        CHECK(noDrawEngine.call_count == 0);
        CHECK_THROWS_AS((void)RandX::RandTriangular(noDrawEngine, 1.0, 0.0, 2.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandTriangular(noDrawEngine, 0.0, 2.0, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandTriangular(
            noDrawEngine, std::numeric_limits<double>::quiet_NaN(), 0.5, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandTriangular(
            noDrawEngine, 0.0, std::numeric_limits<double>::infinity(), 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandTriangular(
            noDrawEngine, -(std::numeric_limits<double>::max)(), 0.0,
            (std::numeric_limits<double>::max)()), std::invalid_argument);
        CHECK(noDrawEngine.call_count == 0);

        RandXTest::ExtendedFixtures::ThrowingEngine throwingEngine;
        CHECK_THROWS_AS((void)RandX::RandTriangular(throwingEngine, 0.0, 0.5, 1.0), std::runtime_error);

        const double adjacent = std::nextafter(1.0, 2.0);
        Engine64 adjacentEngine;
        CHECK(RandX::RandTriangular(adjacentEngine, 1.0, 1.0, adjacent) == 1.0);
        CHECK(adjacentEngine.call_count == 1);
    }

    TEST_CASE("RandTriangular 高动态范围众数邻域保持单调")
    {
        constexpr double Minimum = -0x1.d20cf377b24f0p+697;
        constexpr double Peak = -0x1.365bdb929e7aap+696;
        constexpr double Maximum = 0x1.600d4fe621a90p+696;
        constexpr double BelowUniform = 0x1.efc904b61f014p-2;
        constexpr double AboveUniform = 0x1.efc904b61f016p-2;
        RandXTest::TriangularPublicFixtures::FixedCanonicalEngine64 belowEngine{
            RandXTest::TriangularPublicFixtures::EncodeCanonicalUniform(BelowUniform)};
        RandXTest::TriangularPublicFixtures::FixedCanonicalEngine64 aboveEngine{
            RandXTest::TriangularPublicFixtures::EncodeCanonicalUniform(AboveUniform)};

        const double below = RandX::RandTriangular(belowEngine, Minimum, Peak, Maximum);
        const double above = RandX::RandTriangular(aboveEngine, Minimum, Peak, Maximum);
        CHECK(below <= above);
        CHECK(below >= Minimum);
        CHECK(above < Maximum);
        CHECK(belowEngine.call_count == 1);
        CHECK(aboveEngine.call_count == 1);
    }

    TEST_CASE("RandTriangular 默认和显式引擎保持固定种子状态等价")
    {
        constexpr std::uint64_t Seed = 0xA13F27C5ULL;
        RandX::Xoshiro256StarStar explicitEngine{Seed};
        RandX::Reseed(Seed);
        const double defaultSample = RandX::RandTriangular(-2.0, 0.75, 4.0);
        const double explicitSample = RandX::RandTriangular(explicitEngine, -2.0, 0.75, 4.0);
        CHECK(defaultSample == explicitSample);
        CHECK(RandX::DefaultEngine().serialize() == explicitEngine.serialize());

        const auto stateBeforeNoDraw = RandX::DefaultEngine().serialize();
        CHECK(RandX::RandTriangular(7.0, 7.0, 7.0) == 7.0);
        CHECK(RandX::DefaultEngine().serialize() == stateBeforeNoDraw);
        CHECK_THROWS_AS((void)RandX::RandTriangular(2.0, 1.0, 3.0), std::invalid_argument);
        CHECK(RandX::DefaultEngine().serialize() == stateBeforeNoDraw);
    }

}

#endif
