#ifndef RANDX_TESTS_IMPLEMENTATION_CPP23_CONTRACTS_HPP
#define RANDX_TESTS_IMPLEMENTATION_CPP23_CONTRACTS_HPP

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
#include <ranges>
#include "../common/fixtures.hpp"

TEST_SUITE("内部/C++23引擎概念")
{
    TEST_CASE("JumpableEngine 概念约束")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 满足 JumpableEngine：仅 3 个 xoshiro 系列有 jump()
        static_assert(RandX::detail::JumpableEngine<RandX::Xoshiro256StarStar>);
        static_assert(RandX::detail::JumpableEngine<RandX::Xoroshiro128StarStar>);
        static_assert(RandX::detail::JumpableEngine<RandX::Xoshiro128StarStar>);

        // 不满足 JumpableEngine：无 jump() 方法
        static_assert(!RandX::detail::JumpableEngine<RandX::SplitMix64>);
        static_assert(!RandX::detail::JumpableEngine<RandX::SFC64>);
        static_assert(!RandX::detail::JumpableEngine<RandX::RomuDuoJr>);
        static_assert(!RandX::detail::JumpableEngine<RandX::Xoroshiro64StarStar>);
        static_assert(!RandX::detail::JumpableEngine<RandX::ChaCha20>);

    }
    TEST_CASE("RandomEngine 概念与 is_random_engine_v 适配引用类型")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        static_assert(RandX::detail::RandomEngine<RandX::Xoshiro256StarStar>);
        static_assert(RandX::detail::RandomEngine<RandX::Xoshiro256StarStar&>);
        static_assert(RandX::detail::RandomEngine<const RandX::Xoshiro256StarStar&>);
        static_assert(RandX::detail::RandomEngine<decltype(RandX::DefaultEngine())>);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoshiro256StarStar&>);
        static_assert(RandX::detail::IsFull64BitEngine<RandX::Xoshiro256StarStar>);
        static_assert(RandX::detail::IsFull64BitEngine<RandX::Xoshiro256StarStar&>);
        static_assert(RandX::detail::IsFull32BitEngine<RandX::Xoshiro128StarStar>);
        static_assert(RandX::detail::IsFull32BitEngine<RandX::Xoshiro128StarStar&>);

    }
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
    TEST_CASE("SerializableEngine 概念约束")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 验证序列化概念约束：数组状态引擎满足，标量与密码学引擎排除
        static_assert(RandX::detail::SerializableEngine<RandX::Xoshiro256StarStar>);
        static_assert(!RandX::detail::SerializableEngine<RandX::SplitMix64>);  // state_type = uint64_t 标量
        static_assert(!RandX::detail::SerializableEngine<RandX::ChaCha20>);    // CSPRNG 不导出状态

    }
    TEST_CASE("StreamEngine 概念约束")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 满足 StreamEngine：当前等价于 JumpableEngine
        static_assert(RandX::detail::StreamEngine<RandX::Xoshiro256StarStar>);
        static_assert(RandX::detail::StreamEngine<RandX::Xoroshiro128StarStar>);
        static_assert(RandX::detail::StreamEngine<RandX::Xoshiro128StarStar>);

        // 不满足 StreamEngine
        static_assert(!RandX::detail::StreamEngine<RandX::SplitMix64>);
        static_assert(!RandX::detail::StreamEngine<RandX::SFC64>);
        static_assert(!RandX::detail::StreamEngine<RandX::RomuDuoJr>);
        static_assert(!RandX::detail::StreamEngine<RandX::Xoroshiro64StarStar>);
        static_assert(!RandX::detail::StreamEngine<RandX::ChaCha20>);

    }
    TEST_CASE("operator<< 序列化能力特征")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 此测试主要保证编译期 SFINAE 排除标量引擎
        // 如果 SFINAE 失败，下行将触发 static_assert
        static_assert(!RandX::detail::SerializableEngine<RandX::SplitMix64>);
        static_assert(RandX::detail::SerializableEngine<RandX::Xoshiro256StarStar>);

    }
}

#endif
