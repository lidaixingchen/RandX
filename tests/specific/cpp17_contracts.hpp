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
}

#endif
