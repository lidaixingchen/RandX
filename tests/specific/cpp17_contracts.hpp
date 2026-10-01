#ifndef RANDX_TESTS_SPECIFIC_CPP17_CONTRACTS_HPP
#define RANDX_TESTS_SPECIFIC_CPP17_CONTRACTS_HPP

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

namespace RandXTest
{
namespace Cpp17Fixtures
{
template <class Container, class = void>
struct can_shuffle : std::false_type {};

template <class Container>
struct can_shuffle<Container, std::void_t<decltype(RandX::RandShuffle(std::declval<Container&>()))>> : std::true_type {};

template <int N, class T, class = void>
struct can_rand_bits : std::false_type {};

template <int N, class T>
struct can_rand_bits<N, T, std::void_t<decltype(RandX::RandBits<N, T>())>> : std::true_type {};

template <class Engine, class Iterator, class = void>
struct can_sample_with_engine : std::false_type {};

template <class Engine, class Iterator>
struct can_sample_with_engine<Engine, Iterator, std::void_t<decltype(RandX::RandSample(
    std::declval<Engine&>(),
    std::declval<Iterator>(),
    std::declval<Iterator>(),
    std::declval<typename std::iterator_traits<Iterator>::difference_type>()))>> : std::true_type {};
}
}

TEST_SUITE("专属/C++17/SFINAE约束")
{
    using namespace RandXTest::Cpp17Fixtures;
    TEST_CASE("SFINAE 契约约束诊断验证")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        static_assert(!can_shuffle<const std::vector<int>>::value, "RandShuffle must reject const containers");
        static_assert(!can_rand_bits<32, std::int32_t>::value, "RandBits must reject signed int32_t for 32 bits");
        static_assert(!can_rand_bits<64, std::int64_t>::value, "RandBits must reject signed int64_t for 64 bits");
        static_assert(!can_rand_bits<1, bool>::value, "RandBits must reject bool");

    }
    TEST_CASE("RandSample 类型签名支持只读复制元素与不可复制引擎")
    {
        using Item = RandXTest::SamplingContractFixtures::NonDefaultReadOnlyCopyItem;
        using Container = std::vector<Item>;
        using Engine = RandXTest::OverloadFixtures::MoveOnlyEngine;
        using Iterator = std::vector<int>::iterator;
        static_assert(!std::is_default_constructible<Item>::value, "sample item must not need a default constructor");
        static_assert(std::is_copy_constructible<Item>::value, "sample item must be copy constructible");
        static_assert(!std::is_copy_assignable<Item>::value, "sample item must not need copy assignment");
        static_assert(std::is_same<
            decltype(RandX::RandSample(std::declval<const Container&>(), std::size_t{})),
            std::vector<Item>>::value,
            "container overload must return the sampled value type");
        static_assert(!std::is_copy_constructible<Engine>::value, "sample engine must remain non-copyable");
        static_assert(can_sample_with_engine<Engine, Iterator>::value,
            "explicit iterator overload must accept an engine by lvalue reference");

    }
}

#endif
