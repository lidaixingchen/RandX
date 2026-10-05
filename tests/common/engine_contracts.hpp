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

namespace RandXTest::StreamContracts
{
inline constexpr std::uint64_t kReferenceSeed = 0x4D595DF4D0F33173ULL;
inline constexpr std::uint64_t kZeroStreamId = 0;
inline constexpr std::uint64_t kSingleJumpStreamId = 1;
inline constexpr std::uint64_t kStreamHalfBits = std::numeric_limits<std::uint32_t>::digits;
inline constexpr std::uint64_t kSmallStreamIndex = 3;
inline constexpr std::uint64_t kSmallHighStreamIndex = 2;
inline constexpr std::uint64_t kHalfBoundaryStreamId = std::uint64_t{1} << kStreamHalfBits;
inline constexpr std::uint64_t kHighestStreamIdBit =
    std::uint64_t{1} << (std::numeric_limits<std::uint64_t>::digits - 1);
inline constexpr std::uint64_t kMaximumStreamId = (std::numeric_limits<std::uint64_t>::max)();
inline constexpr std::uint64_t kLowStreamMask = (std::numeric_limits<std::uint32_t>::max)();

template <class Word, std::size_t Count>
constexpr bool StatesEqual(
    const std::array<Word, Count>& lhs,
    const std::array<Word, Count>& rhs) noexcept
{
    for (std::size_t index = 0; index < Count; ++index)
    {
        if (lhs[index] != rhs[index])
            return false;
    }
    return true;
}

struct JumpOnlyEngine
{
    using result_type = std::uint64_t;

    explicit constexpr JumpOnlyEngine(std::uint64_t seed) noexcept : trace(seed) {}

    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }
    constexpr result_type operator()() noexcept { return trace; }
    constexpr void jump() noexcept { trace = trace * 10 + 1; }

    std::uint64_t trace;
};

struct JumpAndLongJumpEngine
{
    using result_type = std::uint64_t;

    explicit constexpr JumpAndLongJumpEngine(std::uint64_t seed) noexcept : trace(seed) {}

    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }
    constexpr result_type operator()() noexcept { return trace; }
    constexpr void jump() noexcept { trace = trace * 10 + 1; }
    constexpr void longJump() noexcept { trace = trace * 10 + 2; }

    std::uint64_t trace;
};

template <class Engine>
bool MatchesLegacyMapping()
{
    const std::array<std::uint64_t, 2> seeds{ 12345, 987654321 };
    const std::array<std::uint64_t, 6> streamIds{
        kZeroStreamId,
        kSingleJumpStreamId,
        kSmallStreamIndex,
        kHalfBoundaryStreamId,
        (std::uint64_t{kSmallStreamIndex} << kStreamHalfBits) | 5,
        (std::uint64_t{kSmallHighStreamIndex} << kStreamHalfBits) | 2,
    };

    for (const std::uint64_t seed : seeds)
    {
        Engine initial{ seed };
        for (std::size_t stateIndex = 0; stateIndex < 3; ++stateIndex)
        {
            const Engine startingState = initial;
            for (const std::uint64_t streamId : streamIds)
            {
                Engine actual = startingState;
                Engine expected = startingState;
                const std::uint64_t longJumps = streamId >> kStreamHalfBits;
                const std::uint64_t shortJumps = streamId & kLowStreamMask;
                for (std::uint64_t index = 0; index < longJumps; ++index)
                    expected.longJump();
                for (std::uint64_t index = 0; index < shortJumps; ++index)
                    expected.jump();

                RandX::detail::ApplyStreamIdJumps(actual, streamId);
                if (actual.serialize() != expected.serialize())
                    return false;
                for (std::size_t outputIndex = 0; outputIndex < 4; ++outputIndex)
                {
                    if (actual() != expected())
                        return false;
                }
            }
            (void)initial();
        }
    }
    return true;
}

inline constexpr std::array<std::uint64_t, 4> kXoshiro256SingleJump{
    0x1D5B7AB18ADDED4BULL, 0x859FFE33ABD7A632ULL,
    0xA14F1A71B90DE0BFULL, 0x2966B6EB556B9243ULL,
};
inline constexpr std::array<std::uint64_t, 4> kXoshiro256HalfBoundary{
    0xFFDAF6E8FD1D263BULL, 0x320AFC80CF8CB82AULL,
    0x1B75513F38F71BE1ULL, 0x8944BCC64F80DD90ULL,
};
inline constexpr std::array<std::uint64_t, 4> kXoshiro256HighestBit{
    0xB9CB2D7AD42F3BA6ULL, 0x577E94473EB63036ULL,
    0xD39715A4BFD1D7E2ULL, 0x78ECF475CE2086AFULL,
};
inline constexpr std::array<std::uint64_t, 4> kXoshiro256MaximumId{
    0xEC26602E9CC1155DULL, 0x8CB1C7287BC050F2ULL,
    0x0AC82894CEA2BA01ULL, 0xFDCC65CF1206E342ULL,
};

inline constexpr std::array<std::uint64_t, 2> kXoroshiro128SingleJump{
    0x1B25F3747B7FA164ULL, 0x1E3F251CEA969FEFULL,
};
inline constexpr std::array<std::uint64_t, 2> kXoroshiro128HalfBoundary{
    0x0259854B1B0B04C6ULL, 0xC4F40049804DC566ULL,
};
inline constexpr std::array<std::uint64_t, 2> kXoroshiro128HighestBit{
    0x95C6FA0ECF347D87ULL, 0xA61D3D1CF0ECB64AULL,
};
inline constexpr std::array<std::uint64_t, 2> kXoroshiro128MaximumId{
    0x622294898F7DFAEBULL, 0x8AD1EDD99D83E1F1ULL,
};

inline constexpr std::array<std::uint32_t, 4> kXoshiro128SingleJump{
    0xE6A8CB49U, 0x5FCC45E5U, 0x31CEE1A1U, 0x24F14C38U,
};
inline constexpr std::array<std::uint32_t, 4> kXoshiro128HalfBoundary{
    0x3F8A239FU, 0x67DABC83U, 0xA96A2C41U, 0x3EBA7FBEU,
};
inline constexpr std::array<std::uint32_t, 4> kXoshiro128HighestBit{
    0x7BDBA5D5U, 0x7EB8D11EU, 0x3CA41D6FU, 0xBF18A02AU,
};
inline constexpr std::array<std::uint32_t, 4> kXoshiro128MaximumId{
    0xC252CB2DU, 0xDB1F4164U, 0xD1276DD2U, 0x78093C64U,
};

static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(kZeroStreamId, kReferenceSeed).serialize(),
    RandX::Xoshiro256StarStar{kReferenceSeed}.serialize()));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(kSingleJumpStreamId, kReferenceSeed).serialize(),
    kXoshiro256SingleJump));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(kHalfBoundaryStreamId, kReferenceSeed).serialize(),
    kXoshiro256HalfBoundary));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(kHighestStreamIdBit, kReferenceSeed).serialize(),
    kXoshiro256HighestBit));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(kMaximumStreamId, kReferenceSeed).serialize(),
    kXoshiro256MaximumId));

static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoroshiro128StarStar>(kZeroStreamId, kReferenceSeed).serialize(),
    RandX::Xoroshiro128StarStar{kReferenceSeed}.serialize()));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoroshiro128StarStar>(kSingleJumpStreamId, kReferenceSeed).serialize(),
    kXoroshiro128SingleJump));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoroshiro128StarStar>(kHalfBoundaryStreamId, kReferenceSeed).serialize(),
    kXoroshiro128HalfBoundary));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoroshiro128StarStar>(kHighestStreamIdBit, kReferenceSeed).serialize(),
    kXoroshiro128HighestBit));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoroshiro128StarStar>(kMaximumStreamId, kReferenceSeed).serialize(),
    kXoroshiro128MaximumId));

static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro128StarStar>(kZeroStreamId, kReferenceSeed).serialize(),
    RandX::Xoshiro128StarStar{kReferenceSeed}.serialize()));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro128StarStar>(kSingleJumpStreamId, kReferenceSeed).serialize(),
    kXoshiro128SingleJump));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro128StarStar>(kHalfBoundaryStreamId, kReferenceSeed).serialize(),
    kXoshiro128HalfBoundary));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro128StarStar>(kHighestStreamIdBit, kReferenceSeed).serialize(),
    kXoshiro128HighestBit));
static_assert(StatesEqual(
    RandX::MakeStreamEngine<RandX::Xoshiro128StarStar>(kMaximumStreamId, kReferenceSeed).serialize(),
    kXoshiro128MaximumId));
}

namespace RandXTest::StreamContracts
{
struct ThrowingJumpEngine : JumpOnlyEngine
{
    using JumpOnlyEngine::JumpOnlyEngine;
    void jump() { throw std::runtime_error("jump failure"); }
};

struct ThrowingLongJumpEngine : JumpAndLongJumpEngine
{
    using JumpAndLongJumpEngine::JumpAndLongJumpEngine;
    void longJump() { throw std::runtime_error("longJump failure"); }
};
}

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

    TEST_CASE("MakeStreamEngine 不同流编号的输出观测")
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
    TEST_CASE("MakeStreamEngine 流映射与旧循环完整状态等价")
    {
        CHECK(RandXTest::StreamContracts::MatchesLegacyMapping<RandX::Xoshiro256StarStar>());
        CHECK(RandXTest::StreamContracts::MatchesLegacyMapping<RandX::Xoroshiro128StarStar>());
        CHECK(RandXTest::StreamContracts::MatchesLegacyMapping<RandX::Xoshiro128StarStar>());
    }
    TEST_CASE("MakeStreamEngine 保留自定义跳跃适配顺序")
    {
        constexpr std::uint64_t seed = 3;
        constexpr std::uint64_t jumpOnlyId = 4;
        constexpr std::uint64_t longJumpId =
            (std::uint64_t{2} << RandXTest::StreamContracts::kStreamHalfBits) | 3;

        auto jumpOnly = RandX::MakeStreamEngine<RandXTest::StreamContracts::JumpOnlyEngine>(jumpOnlyId, seed);
        auto expectedJumpOnly = RandXTest::StreamContracts::JumpOnlyEngine{seed};
        for (std::uint64_t index = 0; index < jumpOnlyId; ++index)
            expectedJumpOnly.jump();
        CHECK(jumpOnly.trace == expectedJumpOnly.trace);

        auto bothJumps = RandX::MakeStreamEngine<RandXTest::StreamContracts::JumpAndLongJumpEngine>(longJumpId, seed);
        auto expectedBothJumps = RandXTest::StreamContracts::JumpAndLongJumpEngine{seed};
        for (std::uint64_t index = 0; index < (longJumpId >> RandXTest::StreamContracts::kStreamHalfBits); ++index)
            expectedBothJumps.longJump();
        for (std::uint64_t index = 0; index < (longJumpId & RandXTest::StreamContracts::kLowStreamMask); ++index)
            expectedBothJumps.jump();
        CHECK(bothJumps.trace == expectedBothJumps.trace);
    }
    TEST_CASE("MakeStreamEngine 传播自定义跳跃异常")
    {
        using namespace RandXTest::StreamContracts;
        CHECK_NOTHROW((void)RandX::MakeStreamEngine<ThrowingJumpEngine>(kZeroStreamId, kReferenceSeed));
        CHECK_THROWS_AS((void)RandX::MakeStreamEngine<ThrowingJumpEngine>(kSingleJumpStreamId, kReferenceSeed), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::MakeStreamEngine<ThrowingLongJumpEngine>(kHalfBoundaryStreamId, kReferenceSeed), std::runtime_error);
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
