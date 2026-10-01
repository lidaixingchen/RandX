#ifndef RANDX_TESTS_IMPLEMENTATION_CPP17_CONTRACTS_HPP
#define RANDX_TESTS_IMPLEMENTATION_CPP17_CONTRACTS_HPP

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

TEST_SUITE("内部/C++17引擎检测")
{
    TEST_CASE("RandomEngine 概念与特征检测")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoshiro256StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoroshiro128StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoshiro128StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoroshiro64StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::SplitMix64>);
        static_assert(RandX::detail::is_random_engine_v<RandX::SFC64>);
        static_assert(RandX::detail::is_random_engine_v<RandX::RomuDuoJr>);
        static_assert(RandX::detail::is_random_engine_v<RandX::ChaCha20>);
        static_assert(RandX::detail::is_random_engine_v<RandXTest::OverloadFixtures::ThrowingEngine>);
        static_assert(RandX::detail::is_random_engine_v<RandXTest::OverloadFixtures::MoveOnlyEngine>);

        static_assert(!RandX::detail::is_random_engine_v<RandXTest::OverloadFixtures::NotAnEngine>);
        static_assert(!RandX::detail::is_random_engine_v<RandXTest::OverloadFixtures::IncompleteEngineNoMinMax>);
        static_assert(!RandX::detail::is_random_engine_v<RandXTest::OverloadFixtures::IncompleteEngineSignedResult>);
        static_assert(!RandX::detail::is_random_engine_v<RandXTest::OverloadFixtures::IncompleteEngineBoolResult>);
        static_assert(!RandX::detail::is_random_engine_v<int>);
        static_assert(!RandX::detail::is_random_engine_v<double>);
        static_assert(!RandX::detail::is_random_engine_v<bool>);
        static_assert(!RandX::detail::is_random_engine_v<std::string>);

    }
    TEST_CASE("is_random_engine_v 适配引用类型")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoshiro256StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoshiro256StarStar&>);
        static_assert(RandX::detail::is_random_engine_v<const RandX::Xoshiro256StarStar&>);
        static_assert(RandX::detail::is_random_engine_v<decltype(RandX::DefaultEngine())>);
        static_assert(RandX::detail::is_full_64bit_engine_v<RandX::Xoshiro256StarStar>);
        static_assert(RandX::detail::is_full_64bit_engine_v<RandX::Xoshiro256StarStar&>);
        static_assert(RandX::detail::is_full_32bit_engine_v<RandX::Xoshiro128StarStar>);
        static_assert(RandX::detail::is_full_32bit_engine_v<RandX::Xoshiro128StarStar&>);

    }
    TEST_CASE("operator<< 序列化能力特征")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 此测试主要保证编译期 SFINAE 排除标量引擎
        static_assert(!RandX::detail::is_serializable_engine_v<RandX::SplitMix64>,
            "SplitMix64 must not be serializable (scalar state_type)");
        static_assert(RandX::detail::is_serializable_engine_v<RandX::Xoshiro256StarStar>,
            "Xoshiro256StarStar must be serializable");

    }
}

#endif
