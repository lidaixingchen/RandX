#ifndef RANDX_TESTS_SPECIFIC_CPP23_CONTRACTS_HPP
#define RANDX_TESTS_SPECIFIC_CPP23_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <concepts>
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

namespace RandXTest
{
namespace Cpp23Fixtures
{
template <class Container>
concept CanRandShuffle = requires(Container&& container) {
    RandX::RandShuffle(std::forward<Container>(container));
};

template <class Container>
concept CanRangesRandShuffle = requires(Container&& container) {
    RandX::ranges::RandShuffle(std::forward<Container>(container));
};

template <int N, class T, class Engine>
concept CanRandBits = requires(Engine& engine) {
    RandX::RandBits<N, T>(engine);
};

template <class Iterator>
concept CanIteratorRandElement = requires(Iterator first, Iterator last) {
    RandX::RandElement(first, last);
};

template <class Iterator>
concept CanIteratorRandElementWithEngine = requires(
    RandX::Xoshiro256StarStar& engine, Iterator first, Iterator last) {
    RandX::RandElement(engine, first, last);
};

struct NonSizedSentinel
{
    const int* end_ptr{ nullptr };
    std::size_t* comparisons{ nullptr };
    bool operator==(const int* value) const noexcept
    {
        if (comparisons) ++*comparisons;
        return value == end_ptr;
    }
};

struct NonCopyableType
{
    int val;
    explicit NonCopyableType(int value) : val(value) {}
    NonCopyableType(const NonCopyableType&) = delete;
    NonCopyableType& operator=(const NonCopyableType&) = delete;
    NonCopyableType(NonCopyableType&&) = default;
    NonCopyableType& operator=(NonCopyableType&&) = default;
};

struct CopyOnlyElement
{
    int value{};
    CopyOnlyElement() = default;
    explicit CopyOnlyElement(int item) : value(item) {}
    CopyOnlyElement(const CopyOnlyElement&) = default;
    CopyOnlyElement& operator=(const CopyOnlyElement&) = delete;
};

struct ModernRandomAccessIterator
{
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::random_access_iterator_tag;

    int* current{nullptr};

    int& operator*() const { return *current; }
    int& operator[](difference_type offset) const { return current[offset]; }
    ModernRandomAccessIterator& operator++() { ++current; return *this; }
    ModernRandomAccessIterator operator++(int)
    {
        ModernRandomAccessIterator previous = *this;
        ++*this;
        return previous;
    }
    ModernRandomAccessIterator& operator--() { --current; return *this; }
    ModernRandomAccessIterator operator--(int)
    {
        ModernRandomAccessIterator previous = *this;
        --*this;
        return previous;
    }
    ModernRandomAccessIterator& operator+=(difference_type offset) { current += offset; return *this; }
    ModernRandomAccessIterator& operator-=(difference_type offset) { current -= offset; return *this; }

    friend ModernRandomAccessIterator operator+(ModernRandomAccessIterator iterator, difference_type offset)
    {
        iterator += offset;
        return iterator;
    }
    friend ModernRandomAccessIterator operator+(difference_type offset, ModernRandomAccessIterator iterator)
    {
        iterator += offset;
        return iterator;
    }
    friend ModernRandomAccessIterator operator-(ModernRandomAccessIterator iterator, difference_type offset)
    {
        iterator -= offset;
        return iterator;
    }
    friend difference_type operator-(ModernRandomAccessIterator first, ModernRandomAccessIterator last)
    {
        return first.current - last.current;
    }
    friend bool operator==(ModernRandomAccessIterator first, ModernRandomAccessIterator last)
    {
        return first.current == last.current;
    }
    friend bool operator!=(ModernRandomAccessIterator first, ModernRandomAccessIterator last)
    {
        return !(first == last);
    }
    friend bool operator<(ModernRandomAccessIterator first, ModernRandomAccessIterator last)
    {
        return first.current < last.current;
    }
    friend bool operator>(ModernRandomAccessIterator first, ModernRandomAccessIterator last)
    {
        return last < first;
    }
    friend bool operator<=(ModernRandomAccessIterator first, ModernRandomAccessIterator last)
    {
        return !(last < first);
    }
    friend bool operator>=(ModernRandomAccessIterator first, ModernRandomAccessIterator last)
    {
        return !(first < last);
    }
};

template <class Range>
concept CanRandElement = requires(Range&& range) {
    RandX::ranges::RandElement(std::forward<Range>(range));
};

template <class Range>
concept CanRangesRandElementWithEngine = requires(
    RandX::Xoshiro256StarStar& engine, Range&& range) {
    RandX::ranges::RandElement(engine, std::forward<Range>(range));
};
}
}

TEST_SUITE("专属/C++23/概念约束")
{
    using namespace RandXTest::Cpp23Fixtures;
    TEST_CASE("RandBits 编译期类型约束断言")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        static_assert(!CanRandBits<32, std::int32_t, RandX::Xoshiro256StarStar>);
        static_assert(!CanRandBits<64, std::int64_t, RandX::Xoshiro256StarStar>);
        static_assert(!CanRandBits<1, bool, RandX::Xoshiro256StarStar>);
        static_assert(!CanRandBits<0, std::uint32_t, RandX::Xoshiro256StarStar>);
        static_assert(!CanRandBits<65, std::uint64_t, RandX::Xoshiro256StarStar>);

    }
}

TEST_SUITE("专属/C++23/编译期")
{
    TEST_CASE("RandIntCE 64位无符号整数全范围有效生成")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr std::uint64_t v1 = RandX::RandIntCE<std::uint64_t, 12345ULL>(0ULL, std::numeric_limits<std::uint64_t>::max());
        constexpr std::uint64_t v2 = RandX::RandIntCE<std::uint64_t, 67890ULL>(0ULL, std::numeric_limits<std::uint64_t>::max());
        CHECK(v1 != 0ULL);
        CHECK(v2 != 0ULL);
        CHECK(v1 != v2);

    }
#if !defined(_MSC_VER) || defined(__SIZEOF_INT128__)
    TEST_CASE("RandIntCE constexpr")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr int v = RandX::RandIntCE(0, 100);
        static_assert(v == 61);
        static_assert(RandX::RandIntCE<int, 42>(1, 6) >= 1);
        static_assert(RandX::RandIntCE<int, 42>(1, 6) <= 6);
        CHECK(v == 61);

    }
#endif
    TEST_CASE("ShuffleCE constexpr 洗牌")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr auto shuffled = [] {
            std::array<int, 10> a = { 0,1,2,3,4,5,6,7,8,9 };
            RandX::ShuffleCE(a.begin(), a.end());
            return a;
        }();
        // 验证是排列：排序后等于原序列
        static_assert([](std::array<int, 10> s) {
            for (int i = 0; i < 10; ++i)
                for (int j = i + 1; j < 10; ++j)
                    if (s[j] < s[i]) { auto t = s[i]; s[i] = s[j]; s[j] = t; }
            return s == std::array{ 0,1,2,3,4,5,6,7,8,9 };
        }(shuffled));
        // 验证确实被打乱了（极大概率不等）
        static_assert(shuffled != std::array{ 0,1,2,3,4,5,6,7,8,9 });

    }
    TEST_CASE("ShuffledArray 自定义 Seed 生成不同编译期结果")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr std::array<int, 5> arr = { 1, 2, 3, 4, 5 };
        constexpr auto res1 = RandX::ShuffledArray<int, 5, 11111ULL>(arr);
        constexpr auto res2 = RandX::ShuffledArray<int, 5, 99999ULL>(arr);
        CHECK(res1 != res2);

    }
}

TEST_SUITE("专属/C++23/范围")
{
    using namespace RandXTest::Cpp23Fixtures;
    TEST_CASE("RandElement 范围路径按实际访问分支约束复制能力")
    {
        using namespace RandXTest::Cpp23Fixtures;
        using InputRange = std::list<CopyOnlyElement>;
        using RandomAccessRange = std::vector<CopyOnlyElement>;
        using InputIterator = std::ranges::iterator_t<InputRange>;
        using RandomAccessIterator = std::ranges::iterator_t<RandomAccessRange>;
        static_assert(!CanIteratorRandElement<InputIterator>);
        static_assert(!CanIteratorRandElementWithEngine<InputIterator>);
        static_assert(CanIteratorRandElement<RandomAccessIterator>);
        static_assert(CanIteratorRandElementWithEngine<RandomAccessIterator>);
        static_assert(!CanRandElement<InputRange&>);
        static_assert(!CanRangesRandElementWithEngine<InputRange&>);
        static_assert(CanRandElement<RandomAccessRange&>);
        static_assert(CanRangesRandElementWithEngine<RandomAccessRange&>);

        constexpr int firstValue = 17;
        constexpr int secondValue = 23;
        constexpr int thirdValue = 31;
        RandomAccessRange population;
        population.emplace_back(firstValue);
        population.emplace_back(secondValue);
        population.emplace_back(thirdValue);

        const auto defaultIterator = RandX::RandElement(population.cbegin(), population.cend());
        REQUIRE(defaultIterator != population.cend());
        const auto defaultValue = RandX::ranges::RandElement(population);
        CHECK((defaultValue.value == firstValue
            || defaultValue.value == secondValue || defaultValue.value == thirdValue));

        RandX::Xoshiro256StarStar engine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto explicitIterator = RandX::RandElement(engine, population.cbegin(), population.cend());
        REQUIRE(explicitIterator != population.cend());
        const auto explicitValue = RandX::ranges::RandElement(engine, population);
        CHECK((explicitValue.value == firstValue
            || explicitValue.value == secondValue || explicitValue.value == thirdValue));

    }
    TEST_CASE("RandElement 与 views::filter 组合")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        auto evens = v | std::views::filter([](int x) { return x % 2 == 0; });
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            int x = RandX::ranges::RandElement(evens);
            CHECK(x % 2 == 0);
            CHECK(x >= 2);
            CHECK(x <= 10);
        }

    }
    TEST_CASE("RandElement 容器直传返回值拷贝")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 10, 20, 30, 40, 50 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            int x = RandX::ranges::RandElement(v);
            CHECK((x == 10 || x == 20 || x == 30 || x == 40 || x == 50));
        }

    }
    TEST_CASE("RandFill 模板推导 double")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<double> buf(100);
        RandX::ranges::RandFill(buf, 0.0, 1.0);
        for (double x : buf)
        {
            CHECK(x >= 0.0);
            CHECK(x <= 1.0);
        }

    }
    TEST_CASE("RandFill 模板推导 float")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<float> buf(100);
        RandX::ranges::RandFill(buf, 0.0f, 1.0f);
        for (float x : buf)
        {
            CHECK(x >= 0.0f);
            CHECK(x <= 1.0f);
        }

    }
    TEST_CASE("RandFill 模板推导 int")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> buf(100);
        RandX::ranges::RandFill(buf, 0, 99);
        for (int x : buf)
        {
            CHECK(x >= 0);
            CHECK(x <= 99);
        }

    }
#if defined(__SIZEOF_INT128__)
    TEST_CASE("RandSample iota_view 的 128 位差值长度边界")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        using WideDiff = __int128;
        constexpr int IndexBits = std::numeric_limits<std::uint64_t>::digits;
        constexpr WideDiff ExtraLength = 5;
        constexpr WideDiff Length = (WideDiff{1} << IndexBits) + ExtraLength;
        const auto population = std::views::iota(WideDiff{0}, Length);
        RandX::Xoshiro256StarStar engine(RandX::DefaultSeed);
        const auto original = engine;
        CHECK(RandX::RandSample(engine, population, std::size_t{0}).empty());
        REQUIRE_THROWS_AS((void)RandX::RandSample(engine, population, std::size_t{1}), std::length_error);
        CHECK_THROWS_AS((void)RandX::RandSample(engine, population.begin(), population.end(), Length), std::length_error);
        CHECK(engine == original);
        RandX::Reseed(RandX::DefaultSeed);
        CHECK_THROWS_AS((void)RandX::RandSample(population, std::size_t{1}), std::length_error);
        CHECK_THROWS_AS((void)RandX::RandSample(population.begin(), population.end(), WideDiff{1}), std::length_error);
        CHECK(RandX::DefaultEngine() == original);

    }
#endif
    TEST_CASE("RandSample 容器版支持非 common random_access_range 与引擎重载")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        auto population = std::views::iota(0, 10L);
        static_assert(std::ranges::random_access_range<decltype(population)>);
        static_assert(std::ranges::sized_range<decltype(population)>);
        static_assert(!std::ranges::common_range<decltype(population)>);

        auto sample = RandX::RandSample(population, std::size_t{3});
        CHECK(sample.size() == 3);
        for (int val : sample)
        {
            CHECK(val >= 0);
            CHECK(val < 10);
        }
        std::set<int> unique_vals(sample.begin(), sample.end());
        CHECK(unique_vals.size() == 3);

        RandX::Xoshiro256StarStar rng(12345);
        auto sample_engine = RandX::RandSample(rng, population, std::size_t{4});
        CHECK(sample_engine.size() == 4);
        for (int val : sample_engine)
        {
            CHECK(val >= 0);
            CHECK(val < 10);
        }

    }
    TEST_CASE("RandSample 容器直传")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 0,1,2,3,4,5,6,7,8,9 };
        auto s = RandX::ranges::RandSample(v, 3);
        CHECK(s.size() == 3);
        for (int x : s)
        {
            CHECK(x >= 0);
            CHECK(x <= 9);
            CHECK(std::find(v.begin(), v.end(), x) != v.end());
        }
        // 无放回：3 个元素互不相同
        CHECK(s[0] != s[1]);
        CHECK(s[0] != s[2]);
        CHECK(s[1] != s[2]);

    }
    TEST_CASE("RandSample 迭代器版支持 128 位 difference_type (iota_view)")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        auto pop = std::views::iota(0LL, 100LL);
        auto sample = RandX::RandSample(pop.begin(), pop.end(), 5);
        CHECK(sample.size() == 5);
        for (long long val : sample)
        {
            CHECK(val >= 0LL);
            CHECK(val < 100LL);
        }

        RandX::Xoshiro256StarStar rng(12345);
        auto sample_engine = RandX::RandSample(rng, pop.begin(), pop.end(), 4);
        CHECK(sample_engine.size() == 4);
        for (long long val : sample_engine)
        {
            CHECK(val >= 0LL);
            CHECK(val < 100LL);
        }

    }
    TEST_CASE("RandShuffle 容器直传保持元素集合")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        std::vector<int> orig = v;
        RandX::ranges::RandShuffle(v);
        // 排序后应与原集合一致
        std::sort(v.begin(), v.end());
        CHECK(v == orig);

    }
    TEST_CASE("Sentinels 与 Ranges 概念约束诊断")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> src = { 10, 20, 30, 40, 50, 60 };
        auto counted_it = std::counted_iterator(src.begin(), 6);
        auto sub_r = std::ranges::subrange(counted_it, std::default_sentinel);

        auto sample = RandX::ranges::RandSample(sub_r, 3);
        CHECK(sample.size() == 3);
        for (int val : sample)
        {
            CHECK(std::find(src.begin(), src.end(), val) != src.end());
        }

        auto elem = RandX::ranges::RandElement(sub_r);
        CHECK(std::find(src.begin(), src.end(), elem) != src.end());

        // 静态概念诊断验证：只读容器无法匹配 RandShuffle
        static_assert(!CanRandShuffle<const std::vector<int>&>);
        static_assert(!CanRangesRandShuffle<const std::vector<int>&>);

    }
    TEST_CASE("counted_iterator 与 default_sentinel 正常分派")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 1, 2, 3, 4, 5 };
        auto r = std::ranges::subrange(
            std::counted_iterator(v.begin(), 3),
            std::default_sentinel
        );
        int elem = RandX::ranges::RandElement(r);
        CHECK((elem >= 1 && elem <= 3));

    }
    TEST_CASE("vector 与 list 基本 range 选取")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 10, 20, 30, 40, 50 };
        int val_v = RandX::ranges::RandElement(v);
        CHECK((val_v >= 10 && val_v <= 50));

        std::list<int> l = { 100, 200, 300 };
        int val_l = RandX::ranges::RandElement(l);
        CHECK((val_l >= 100 && val_l <= 300));

        RandX::Xoshiro256StarStar rng{ 42 };
        int val_eng = RandX::ranges::RandElement(rng, v);
        CHECK((val_eng >= 10 && val_eng <= 50));

    }
    TEST_CASE("views::take_while 非 sized 哨兵正常分派")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 1, 2, 3, 4, 10, 5 };
        auto tw = v | std::views::take_while([](int x) { return x < 5; });
        int elem = RandX::ranges::RandElement(tw);
        CHECK((elem >= 1 && elem <= 4));

        RandX::Xoshiro256StarStar rng{ 777 };
        int elem_eng = RandX::ranges::RandElement(rng, tw);
        CHECK((elem_eng >= 1 && elem_eng <= 4));

    }
    TEST_CASE("空范围抛出 invalid_argument")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> empty_vec;
        CHECK_THROWS_AS((void)RandX::ranges::RandElement(empty_vec), std::invalid_argument);

        RandX::Xoshiro256StarStar rng{ 42 };
        CHECK_THROWS_AS((void)RandX::ranges::RandElement(rng, empty_vec), std::invalid_argument);

    }
    TEST_CASE("类型约束排斥不可拷贝与纯单遍输入范围")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 不可拷贝类型被约束排除
        static_assert(!RandXTest::Cpp23Fixtures::CanRandElement<std::vector<RandXTest::Cpp23Fixtures::NonCopyableType>&>);
        CHECK(!RandXTest::Cpp23Fixtures::CanRandElement<std::vector<RandXTest::Cpp23Fixtures::NonCopyableType>&>);

        // 纯单遍 input_range（istream_view）被约束排除
        static_assert(!RandXTest::Cpp23Fixtures::CanRandElement<std::ranges::istream_view<int>&>);
        CHECK(!RandXTest::Cpp23Fixtures::CanRandElement<std::ranges::istream_view<int>&>);

    }
    TEST_CASE("自定义随机访问迭代器搭配非 sized 哨兵")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        int arr[5] = { 11, 22, 33, 44, 55 };
        auto r = std::ranges::subrange(static_cast<const int*>(arr), RandXTest::Cpp23Fixtures::NonSizedSentinel{ arr + 5 });
        static_assert(std::random_access_iterator<const int*>);
        static_assert(!std::sized_sentinel_for<RandXTest::Cpp23Fixtures::NonSizedSentinel, const int*>);

        int elem = RandX::ranges::RandElement(r);
        CHECK((elem == 11 || elem == 22 || elem == 33 || elem == 44 || elem == 55));

        const std::size_t populationSize = std::size(arr);
        std::size_t comparisons = 0;
        auto counted = std::ranges::subrange(static_cast<const int*>(arr),
            RandXTest::Cpp23Fixtures::NonSizedSentinel{ arr + populationSize, &comparisons });
        RandX::Xoshiro256StarStar engine{ 42 };
        const auto sample = RandX::RandSample(engine, counted, populationSize);
        CHECK(sample == std::vector<int>(std::begin(arr), std::end(arr)));
        CHECK(comparisons == populationSize + 1);

    }
}

TEST_SUITE("专属/C++23/抽样")
{
    TEST_CASE("RandSample 实际实例化现代随机访问迭代器")
    {
        using Iterator = RandXTest::Cpp23Fixtures::ModernRandomAccessIterator;
        static_assert(std::random_access_iterator<Iterator>);

        constexpr std::size_t populationSize = 8;
        constexpr std::ptrdiff_t sampleCount = 4;
        std::array<int, populationSize> population{};
        for (std::size_t position = 0; position < population.size(); ++position)
            population[position] = static_cast<int>(position + 100);
        const Iterator first{population.data()};
        const Iterator last{population.data() + population.size()};

        const auto defaultSample = RandX::RandSample(first, last, sampleCount);
        REQUIRE(defaultSample.size() == static_cast<std::size_t>(sampleCount));
        std::set<int> selected(defaultSample.begin(), defaultSample.end());
        CHECK(selected.size() == defaultSample.size());
        for (const int value : defaultSample)
            CHECK(std::find(population.begin(), population.end(), value) != population.end());

        RandX::Xoshiro256StarStar engine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto explicitSample = RandX::RandSample(engine, first, last, sampleCount);
        REQUIRE(explicitSample.size() == static_cast<std::size_t>(sampleCount));
        for (const int value : explicitSample)
            CHECK(std::find(population.begin(), population.end(), value) != population.end());

        auto range = std::ranges::subrange(first, last);
        const auto rangeSample = RandX::ranges::RandSample(range, sampleCount);
        REQUIRE(rangeSample.size() == static_cast<std::size_t>(sampleCount));
        std::set<int> rangeSelected(rangeSample.begin(), rangeSample.end());
        CHECK(rangeSelected.size() == rangeSample.size());
        for (const int value : rangeSample)
            CHECK(std::find(population.begin(), population.end(), value) != population.end());

    }
    TEST_CASE("RandSample 实际实例化独立哨兵默认与显式入口")
    {
        constexpr std::size_t populationSize = RandX::detail::HashSetThresholdK * 2;
        constexpr std::ptrdiff_t sampleCount = 2;
        std::array<int, populationSize> population{};
        for (std::size_t position = 0; position < population.size(); ++position)
            population[position] = static_cast<int>(position);
        const int* first = population.data();
        std::size_t defaultComparisons = 0;
        const RandXTest::Cpp23Fixtures::NonSizedSentinel defaultLast{
            population.data() + population.size(), &defaultComparisons};
        static_assert(std::sentinel_for<RandXTest::Cpp23Fixtures::NonSizedSentinel, const int*>);
        static_assert(!std::sized_sentinel_for<RandXTest::Cpp23Fixtures::NonSizedSentinel, const int*>);

        const auto defaultSample = RandX::RandSample(first, defaultLast, sampleCount);
        REQUIRE(defaultSample.size() == static_cast<std::size_t>(sampleCount));
        CHECK(defaultComparisons == population.size() + 1);
        for (const int value : defaultSample)
            CHECK(std::find(population.begin(), population.end(), value) != population.end());

        std::size_t explicitComparisons = 0;
        const RandXTest::Cpp23Fixtures::NonSizedSentinel explicitLast{
            population.data() + population.size(), &explicitComparisons};
        RandX::Xoshiro256StarStar engine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto explicitSample = RandX::RandSample(engine, first, explicitLast, sampleCount);
        REQUIRE(explicitSample.size() == static_cast<std::size_t>(sampleCount));
        CHECK(explicitComparisons == population.size() + 1);
        for (const int value : explicitSample)
            CHECK(std::find(population.begin(), population.end(), value) != population.end());

    }
    TEST_CASE("RandSample 实际实例化只移动 istream_view 三种入口")
    {
        using View = std::ranges::istream_view<int>;
        using Iterator = std::ranges::iterator_t<View>;
        static_assert(std::input_iterator<Iterator>);
        static_assert(!std::copy_constructible<Iterator>);
        static_assert(std::move_constructible<Iterator>);

        constexpr std::ptrdiff_t sampleCount = 2;
        const auto checkMembership = [sampleCount](const std::vector<int>& sample, const std::array<int, 4>& source) {
            REQUIRE(sample.size() == static_cast<std::size_t>(sampleCount));
            for (const int value : sample)
                CHECK(std::find(source.begin(), source.end(), value) != source.end());
        };
        const std::array<int, 4> source{13, 19, 29, 31};

        std::istringstream defaultInput("13 19 29 31");
        auto defaultView = std::views::istream<int>(defaultInput);
        auto defaultFirst = defaultView.begin();
        const auto defaultSample = RandX::RandSample(
            std::move(defaultFirst), std::default_sentinel, sampleCount);
        checkMembership(defaultSample, source);
        CHECK(defaultInput.eof());

        std::istringstream explicitInput("13 19 29 31");
        auto explicitView = std::views::istream<int>(explicitInput);
        auto explicitFirst = explicitView.begin();
        RandX::Xoshiro256StarStar engine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto explicitSample = RandX::RandSample(
            engine, std::move(explicitFirst), std::default_sentinel, sampleCount);
        checkMembership(explicitSample, source);
        CHECK(explicitInput.eof());

        std::istringstream rangesInput("13 19 29 31");
        auto rangesView = std::views::istream<int>(rangesInput);
        const auto rangesSample = RandX::ranges::RandSample(rangesView, sampleCount);
        checkMembership(rangesSample, source);
        CHECK(rangesInput.eof());

    }
    TEST_CASE("RandSample istream_view 零量与输入耗尽保持进度契约")
    {
        std::istringstream zeroInput("41 43");
        auto zeroView = std::views::istream<int>(zeroInput);
        const auto zeroSample = RandX::ranges::RandSample(zeroView, std::ptrdiff_t{0});
        CHECK(zeroSample.empty());
        int nextValue = 0;
        zeroInput >> nextValue;
        CHECK(nextValue == 43);

        std::istringstream shortInput("47 53");
        auto shortView = std::views::istream<int>(shortInput);
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto initialDefaultState = RandX::DefaultEngine();
        const auto exhaustedSample = RandX::ranges::RandSample(shortView, std::ptrdiff_t{5});
        CHECK((exhaustedSample == std::vector<int>{47, 53}));
        CHECK(RandX::DefaultEngine() == initialDefaultState);

    }
}

#endif
