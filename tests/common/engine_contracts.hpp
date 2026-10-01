#ifndef RANDX_TESTS_COMMON_ENGINE_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_ENGINE_CONTRACTS_HPP

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

TEST_SUITE("公共/基础/引擎")
{
    TEST_CASE("七种 PRNG 的种子与状态契约")
    {
        RandXTest::EngineFixtures::VerifyEngineSeedSeqContracts<RandX::SplitMix64>();
        RandXTest::EngineFixtures::VerifyEngineSeedSeqContracts<RandX::Xoshiro256StarStar>();
        RandXTest::EngineFixtures::VerifyEngineSeedSeqContracts<RandX::Xoroshiro128StarStar>();
        RandXTest::EngineFixtures::VerifyEngineSeedSeqContracts<RandX::Xoshiro128StarStar>();
        RandXTest::EngineFixtures::VerifyEngineSeedSeqContracts<RandX::Xoroshiro64StarStar>();
        RandXTest::EngineFixtures::VerifyEngineSeedSeqContracts<RandX::SFC64>();
        RandXTest::EngineFixtures::VerifyEngineSeedSeqContracts<RandX::RomuDuoJr>();
    }

    TEST_CASE("MakeStreamEngine 多流不重叠")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        auto s0 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(0, 12345);
        auto s1 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(1, 12345);
        bool allSame = true;
        for (int i = 0; i < 10; ++i)
        {
            if (s0() != s1()) allSame = false;
        }
        CHECK_FALSE(allSame);

    }
    TEST_CASE("MakeStreamEngine 流编号跳跃一致性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr std::uint64_t seed = 123456ULL;
        constexpr std::uint64_t shortStreamId = 3ULL;
        constexpr std::uint64_t longStreamId = std::uint64_t{1} << std::numeric_limits<std::uint32_t>::digits;

        auto shortStream = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(shortStreamId, seed);
        RandX::Xoshiro256StarStar expectedShort{ seed };
        for (std::uint64_t i = 0; i < shortStreamId; ++i)
            expectedShort.jump();
        CHECK(shortStream.serialize() == expectedShort.serialize());

        auto longStream = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(longStreamId, seed);
        RandX::Xoshiro256StarStar expectedLong{ seed };
        expectedLong.longJump();
        CHECK(longStream.serialize() == expectedLong.serialize());

    }
    TEST_CASE("RomuDuoJr")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::RomuDuoJr rng{ 12345 };
        const std::uint64_t expected[] = {
            2454886589211414944ULL, 12510505629750556783ULL,
            16962469053573158940ULL, 8350026492023846583ULL,
            15401281827437905834ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);

    }
    TEST_CASE("SFC64")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::SFC64 rng{ 12345 };
        const std::uint64_t expected[] = {
            13526236746588683560ULL, 8823148983839225293ULL,
            5240613241081073383ULL, 17030394482648619497ULL,
            7698197985592869707ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);

    }
    TEST_CASE("SplitMix64")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::SplitMix64 rng{ 12345 };
        const std::uint64_t expected[] = {
            2454886589211414944ULL, 3778200017661327597ULL,
            2205171434679333405ULL, 3248800117070709450ULL,
            9350289611492784363ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);

    }
    TEST_CASE("SplitMix64::discard 步进等价性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::uint64_t test_steps[] = { 0, 1, 5, 23, 100, 1000 };
        for (auto n : test_steps)
        {
            RandX::SplitMix64 sm1{ 42 };
            sm1.discard(n);
            RandX::SplitMix64 sm2{ 42 };
            for (std::uint64_t i = 0; i < n; ++i)
            {
                (void)sm2();
            }
            CHECK(sm1() == sm2());
        }

    }
    TEST_CASE("Xoroshiro128StarStar")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoroshiro128StarStar rng{ 12345 };
        const std::uint64_t expected[] = {
            9940793396233540349ULL, 8784320640503919345ULL,
            16208043774633962581ULL, 11032235639386297630ULL,
            4698907930579033109ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);

    }
    TEST_CASE("Xoroshiro64StarStar")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoroshiro64StarStar rng{ 12345 };
        const std::uint32_t expected[] = {
            63958076U, 2181105171U, 532052331U, 3458610118U, 2965685819U
        };
        for (auto e : expected)
            CHECK(rng() == e);

    }
    TEST_CASE("Xoshiro128StarStar")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro128StarStar rng{ 12345 };
        const std::uint32_t expected[] = {
            1096865841U, 933661059U, 3314798965U, 1305642763U, 1040785987U
        };
        for (auto e : expected)
            CHECK(rng() == e);

    }
    TEST_CASE("Xoshiro256StarStar")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 12345 };
        const std::uint64_t expected[] = {
            13720838825685603483ULL, 2398916695208396998ULL,
            17770384849984869256ULL, 891717726879801395ULL,
            10241316046318454344ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);

    }
    TEST_CASE("discard 与连续调用等价")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar a{ 42 };
        RandX::Xoshiro256StarStar b{ 42 };
        a.discard(100);
        for (int i = 0; i < 100; ++i) b();
        CHECK(a() == b());

    }
    TEST_CASE("jump / longJump 平稳步进")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 777 };
        rng.jump();
        rng.longJump();
        (void)rng();

    }
    TEST_CASE("operator== / != 相等性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar a{ 1 }, b{ 1 }, c{ 2 };
        CHECK(a == b);
        CHECK(a != c);

    }
    TEST_CASE("std::seed_seq 播种确定性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::seed_seq seq{ 1, 2, 3, 4, 5, 6, 7, 8 };
        RandX::Xoshiro256StarStar rng1{ seq };
        std::seed_seq seq2{ 1, 2, 3, 4, 5, 6, 7, 8 };
        RandX::Xoshiro256StarStar rng2{ seq2 };
        CHECK(rng1() == rng2());
        CHECK(rng1() == rng2());

    }
    TEST_CASE("全零吸收态逃逸保证")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::array<std::uint64_t, 4> zeroState{};
        RandX::Xoshiro256StarStar rng{ zeroState };
        CHECK_FALSE((rng() == 0 && rng() == 0 && rng() == 0));

        RandX::Xoshiro256StarStar rng2{ 12345 };
        rng2.deserialize(zeroState);
        CHECK_FALSE((rng2() == 0 && rng2() == 0 && rng2() == 0));

    }
    TEST_CASE("全零输入自动重置确定性序列")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        {
            RandX::Xoshiro256StarStar rng{ std::array<std::uint64_t, 4>{} };
            CHECK(rng() == 0ULL);
            CHECK(rng() == 5760ULL);
            CHECK(rng() == 5760ULL);
        }
        {
            RandX::Xoroshiro128StarStar rng{ std::array<std::uint64_t, 2>{} };
            CHECK(rng() == 5760ULL);
            CHECK(rng() == 97014257280ULL);
            CHECK(rng() == 16610091813126018688ULL);
        }
        {
            RandX::Xoshiro128StarStar rng{ std::array<std::uint32_t, 4>{} };
            CHECK(rng() == 0U);
            CHECK(rng() == 5760U);
            CHECK(rng() == 5760U);
        }
        {
            RandX::Xoroshiro64StarStar rng{ std::array<std::uint32_t, 2>{} };
            CHECK(rng() == 3802928447U);
            CHECK(rng() == 3134575995U);
            CHECK(rng() == 1411955750U);
        }
        {
            RandX::SFC64 rng{ std::array<std::uint64_t, 4>{} };
            CHECK(rng() == 0ULL);
            CHECK(rng() == 1ULL);
            CHECK(rng() == 2ULL);
        }
        {
            RandX::RomuDuoJr rng{ std::array<std::uint64_t, 2>{} };
            CHECK(rng() == 1ULL);
            CHECK(rng() == 0ULL);
            CHECK(rng() == 3205649788950522037ULL);
        }

    }
    TEST_CASE("引擎 Trivially Destructible 静态断言")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        static_assert(std::is_trivially_destructible_v<RandX::Xoshiro256StarStar>);
        static_assert(std::is_trivially_destructible_v<RandX::Xoroshiro128StarStar>);
        static_assert(std::is_trivially_destructible_v<RandX::Xoshiro128StarStar>);
        static_assert(std::is_trivially_destructible_v<RandX::Xoroshiro64StarStar>);
        static_assert(std::is_trivially_destructible_v<RandX::SplitMix64>);
        static_assert(std::is_trivially_destructible_v<RandX::SFC64>);
        static_assert(std::is_trivially_destructible_v<RandX::RomuDuoJr>);
        CHECK(true);

    }
}

#endif
