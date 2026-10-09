#ifndef RANDX_TESTS_COMMON_SAMPLING_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_SAMPLING_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <numeric>
#include <list>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
#include "doctest.h"
#include "fixtures.hpp"
#include "../fixtures/sampling_type_contracts.hpp"

namespace RandXTest
{
namespace SamplingCompileChecks
{
template <class Container, class = void>
struct HasDefaultContainerRandSample : std::false_type {};

template <class Container>
struct HasDefaultContainerRandSample<Container, std::void_t<decltype(
	RandX::RandSample(std::declval<Container&>(), std::declval<std::size_t>()))>> : std::true_type {};

template <class Container, class = void>
struct HasExplicitContainerRandSample : std::false_type {};

template <class Container>
struct HasExplicitContainerRandSample<Container, std::void_t<decltype(
	RandX::RandSample(std::declval<RandX::Xoshiro256StarStar&>(),
		std::declval<Container&>(), std::declval<std::size_t>()))>> : std::true_type {};

template <class Iterator, class = void>
struct HasDefaultIteratorRandSample : std::false_type {};

template <class Iterator>
struct HasDefaultIteratorRandSample<Iterator, std::void_t<decltype(
	RandX::RandSample(std::declval<Iterator>(), std::declval<Iterator>(),
		std::declval<typename std::iterator_traits<Iterator>::difference_type>()))>> : std::true_type {};

template <class Iterator, class = void>
struct HasExplicitIteratorRandSample : std::false_type {};

template <class Iterator>
struct HasExplicitIteratorRandSample<Iterator, std::void_t<decltype(
	RandX::RandSample(std::declval<RandX::Xoshiro256StarStar&>(),
		std::declval<Iterator>(), std::declval<Iterator>(),
		std::declval<typename std::iterator_traits<Iterator>::difference_type>()))>> : std::true_type {};

template <class Container, class = void>
struct HasEmptyMember : std::false_type {};

template <class Container>
struct HasEmptyMember<Container, std::void_t<decltype(std::declval<const Container&>().empty())>>
	: std::true_type {};

#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 201911L
template <class Range, class = void>
struct HasRangesRandSample : std::false_type {};

template <class Range>
struct HasRangesRandSample<Range, std::void_t<decltype(RandX::ranges::RandSample(
	std::declval<Range&>(), std::declval<std::ranges::range_difference_t<Range>>()))>>
	: std::true_type {};
#endif

using CopyOnlySampleItem = SamplingContractFixtures::NonDefaultReadOnlyCopyItem;
using CopyOnlyRandomAccessIterator = std::vector<CopyOnlySampleItem>::const_iterator;
using CopyOnlyInputIterator = std::list<CopyOnlySampleItem>::const_iterator;

static_assert(HasDefaultContainerRandSample<std::vector<int>>::value,
	"RandSample must accept a vector through its const container parameter");
static_assert(HasExplicitContainerRandSample<std::vector<int>>::value,
	"the engine overload must accept a vector through its const container parameter");
static_assert(HasDefaultContainerRandSample<const std::vector<int>>::value,
	"RandSample must accept an explicitly const range");
static_assert(HasExplicitContainerRandSample<const std::vector<int>>::value,
	"the engine overload must accept an explicitly const range");
static_assert(!HasDefaultContainerRandSample<SamplingTypeFixtures::MutableOnlyRandomAccessRange>::value,
	"mutable-only ranges must be rejected by the container overload constraints");
static_assert(!HasExplicitContainerRandSample<SamplingTypeFixtures::MutableOnlyRandomAccessRange>::value,
	"mutable-only ranges must be rejected by the engine container overload constraints");
static_assert(std::is_copy_constructible<CopyOnlySampleItem>::value,
	"the copy-only sample fixture must be copy constructible");
static_assert(!std::is_copy_assignable<CopyOnlySampleItem>::value,
	"the copy-only sample fixture must not be copy assignable");
static_assert(HasDefaultIteratorRandSample<CopyOnlyRandomAccessIterator>::value,
	"random-access sampling must accept copy-only elements");
static_assert(HasExplicitIteratorRandSample<CopyOnlyRandomAccessIterator>::value,
	"engine random-access sampling must accept copy-only elements");
static_assert(!HasDefaultIteratorRandSample<CopyOnlyInputIterator>::value,
	"reservoir sampling must require copy-assignable elements");
static_assert(!HasExplicitIteratorRandSample<CopyOnlyInputIterator>::value,
	"engine reservoir sampling must require copy-assignable elements");
static_assert(!HasEmptyMember<SamplingTypeFixtures::SizedIndexedContainer>::value,
	"the indexed container fixture must not expose empty()");

#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 201911L
static_assert(HasRangesRandSample<std::vector<CopyOnlySampleItem>>::value,
	"the C++23 ranges adapter must retain the random-access copy-only path");
static_assert(!HasRangesRandSample<std::list<CopyOnlySampleItem>>::value,
	"the C++23 ranges adapter must constrain the reservoir path");
#endif
}
}

TEST_SUITE("公共/基础/抽样")
{
    TEST_CASE("Input Iterator RandElement 按值返回")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::istringstream iss("10 20 30 40 50");
        std::istream_iterator<int> beg(iss), end;
        int val = RandX::RandElement(beg, end);
        CHECK((val == 10 || val == 20 || val == 30 || val == 40 || val == 50));

    }
    TEST_CASE("RandElement 与 RandSample 支持原生 C 数组")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        int arr[5] = { 10, 20, 30, 40, 50 };
        int val = RandX::RandElement(arr);
        CHECK((val >= 10 && val <= 50));

        const auto sample = RandX::RandSample(arr, 3);
        CHECK(sample.size() == 3);

    }
    TEST_CASE("RandElement 单元素容器始终返回该元素")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(42);
        std::vector<int> single = { 99 };
        for (int i = 0; i < 50; ++i)
            CHECK(RandX::RandElement(single) == 99);

    }
    TEST_CASE("RandElement 右值容器按值返回维持生命周期安全")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(42);
        int val = RandX::RandElement(std::vector<int>{10, 20, 30});
        CHECK((val == 10 || val == 20 || val == 30));

    }
    TEST_CASE("RandElement 容器版返回元素引用")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::array<int, 3> arr = { 10, 20, 30 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            int v = RandX::RandElement(arr);
            CHECK((v == 10 || v == 20 || v == 30));
        }

    }
    TEST_CASE("RandElement 空范围抛异常")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> empty;
        CHECK_THROWS_AS((void)RandX::RandElement(empty.begin(), empty.end()), std::invalid_argument);

    }
    TEST_CASE("RandElement 支持仅有 size 和索引的容器")
    {
        using Container = RandXTest::SamplingTypeFixtures::SizedIndexedContainer;
        Container population;
        const int selected = RandX::RandElement(population);
        CHECK(std::find(population.begin(), population.end(), selected) != population.end());

        const Container empty(RandXTest::SamplingTypeFixtures::kEmptySizedIndexedValueCount);
        CHECK_THROWS_AS((void)RandX::RandElement(empty), std::invalid_argument);

    }
    TEST_CASE("RandElement 迭代器版（输入迭代器 reservoir sampling）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::list<int> l = { 100, 200, 300, 400, 500 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            auto val = RandX::RandElement(l.begin(), l.end());
            CHECK(val >= 100);
            CHECK(val <= 500);
        }
        // 引擎重载
        RandX::Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            auto val = RandX::RandElement(rng, l.begin(), l.end());
            CHECK(val >= 100);
            CHECK(val <= 500);
        }

    }
    TEST_CASE("RandElement 迭代器版（随机访问）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 10, 20, 30, 40, 50 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            auto it = RandX::RandElement(v.begin(), v.end());
            CHECK(it >= v.begin());
            CHECK(it < v.end());
            CHECK(*it >= 10);
            CHECK(*it <= 50);
        }
        // 引擎重载
        RandX::Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            auto it = RandX::RandElement(rng, v.begin(), v.end());
            CHECK(it >= v.begin());
            CHECK(it < v.end());
        }

    }
    TEST_CASE("RandSample 容器版支持不可默认构造类型")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        struct NonDefaultConstructibleItem
        {
            int id;
            NonDefaultConstructibleItem() = delete;
            explicit NonDefaultConstructibleItem(int v) : id(v) {}
            NonDefaultConstructibleItem(const NonDefaultConstructibleItem&) = default;
            NonDefaultConstructibleItem(NonDefaultConstructibleItem&&) = default;
            NonDefaultConstructibleItem& operator=(const NonDefaultConstructibleItem&) = default;
            NonDefaultConstructibleItem& operator=(NonDefaultConstructibleItem&&) = default;
        };

        std::vector<NonDefaultConstructibleItem> items;
        items.emplace_back(10);
        items.emplace_back(20);
        items.emplace_back(30);
        items.emplace_back(40);

        auto sample = RandX::RandSample(items, std::size_t{2});
        CHECK(sample.size() == 2);
        CHECK(sample[0].id != sample[1].id);
        CHECK((sample[0].id == 10 || sample[0].id == 20 || sample[0].id == 30 || sample[0].id == 40));
        CHECK((sample[1].id == 10 || sample[1].id == 20 || sample[1].id == 30 || sample[1].id == 40));

    }
    TEST_CASE("RandSample 容器版支持原生数组")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        int arr[5] = { 10, 20, 30, 40, 50 };
        auto sample = RandX::RandSample(arr, std::size_t{3});
        CHECK(sample.size() == 3);
        for (int x : sample)
        {
            CHECK((x == 10 || x == 20 || x == 30 || x == 40 || x == 50));
        }

    }
    TEST_CASE("RandSample 容器版支持带引擎重载")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> pool = { 1, 2, 3, 4, 5, 6, 7, 8 };
        RandX::Xoshiro256StarStar rng(12345);
        auto sample = RandX::RandSample(rng, pool, 3);
        CHECK(sample.size() == 3);
        for (int v : sample)
        {
            CHECK((v >= 1 && v <= 8));
        }

    }
    TEST_CASE("RandSample 容器重载支持 const vector")
    {
        constexpr std::size_t kRequestCount{2};
        const std::vector<int> population{
            RandXTest::SamplingTypeFixtures::kFirstSizedIndexedValue,
            RandXTest::SamplingTypeFixtures::kSecondSizedIndexedValue,
            RandXTest::SamplingTypeFixtures::kThirdSizedIndexedValue};

        const auto defaultSample = RandX::RandSample(population, kRequestCount);
        CHECK(defaultSample.size() == kRequestCount);

        RandX::Xoshiro256StarStar engine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto explicitSample = RandX::RandSample(engine, population, kRequestCount);
        CHECK(explicitSample.size() == kRequestCount);

    }
    TEST_CASE("RandSample 按索引抽取可复制构造元素")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        struct CopyConstructibleItem
        {
            const std::size_t id;
            std::size_t* copyCount;
            CopyConstructibleItem(std::size_t value, std::size_t& count)
                : id(value), copyCount(&count) {}
            CopyConstructibleItem(const CopyConstructibleItem& other)
                : id(other.id), copyCount(other.copyCount) { ++*copyCount; }
            CopyConstructibleItem(CopyConstructibleItem&&) = default;
            CopyConstructibleItem& operator=(const CopyConstructibleItem&) = delete;
            CopyConstructibleItem& operator=(CopyConstructibleItem&&) = delete;
        };
        constexpr std::size_t PopulationSize = 100;
        constexpr std::size_t SampleSize = 3;
        std::size_t copyCount = 0;
        std::vector<CopyConstructibleItem> items;
        items.reserve(PopulationSize);
        for (std::size_t i = 0; i < PopulationSize; ++i)
            items.emplace_back(i, copyCount);

        const auto sample = RandX::RandSample(items, SampleSize);
        REQUIRE(sample.size() == SampleSize);
        CHECK(copyCount == SampleSize);
        std::set<std::size_t> ids;
        for (const auto& item : sample)
        {
            CHECK(item.id < PopulationSize);
            ids.insert(item.id);
        }
        CHECK(ids.size() == SampleSize);

        RandX::Xoshiro256StarStar engine(RandX::DefaultSeed);
        copyCount = 0;
        const auto engineSample = RandX::RandSample(engine, items, SampleSize);
        REQUIRE(engineSample.size() == SampleSize);
        CHECK(copyCount == SampleSize);
        ids.clear();
        for (const auto& item : engineSample)
        {
            CHECK(item.id < PopulationSize);
            ids.insert(item.id);
        }
        CHECK(ids.size() == SampleSize);

        copyCount = 0;
        const auto empty = RandX::RandSample(engine, items, std::size_t{0});
        CHECK(empty.empty());
        CHECK(copyCount == 0);
        const auto all = RandX::RandSample(engine, items, (std::numeric_limits<std::size_t>::max)());
        REQUIRE(all.size() == items.size());
        CHECK(copyCount == PopulationSize);
        for (std::size_t i = 0; i < PopulationSize; ++i)
            CHECK(all[i].id == items[i].id);

        for (std::size_t i = 0; i < PopulationSize; ++i)
            CHECK(items[i].id == i);

    }
    TEST_CASE("RandSample 数量、成员、唯一性与引擎状态")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr std::size_t populationSize = 128;
        const std::size_t sampleSizes[] = {0, 1, populationSize / 2, populationSize};
        std::vector<std::size_t> population(populationSize);
        for (std::size_t index = 0; index < populationSize; ++index)
            population[index] = index;
        for (const std::size_t sampleSize : sampleSizes)
        {
            RandX::Xoshiro256StarStar engine(RandXTest::TestConstants::kDefaultEngineTestSeed);
            const auto initialState = engine;
            const auto sample = RandX::RandSample(engine, population, sampleSize);
            const std::size_t expectedSize = (std::min)(sampleSize, populationSize);
            REQUIRE(sample.size() == expectedSize);
            const std::set<std::size_t> selected(sample.begin(), sample.end());
            CHECK(selected.size() == expectedSize);
            for (const std::size_t value : selected)
                CHECK(value < populationSize);
            if (sampleSize != 0 && sampleSize != populationSize)
                CHECK(engine != initialState);
            else
                CHECK(engine == initialState);
        }

    }
    TEST_CASE("RandSample 空范围边界")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = {1, 2, 3, 4, 5};
        auto res = RandX::RandSample(v.begin(), v.begin(), 3);
        CHECK(res.empty());

    }
    TEST_CASE("RandSample 逆序迭代器边界")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = {1, 2, 3, 4, 5};
        auto res = RandX::RandSample(v.end(), v.begin(), 3);
        CHECK(res.empty());

    }
    TEST_CASE("RandSample(n=0) 返回空容器")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> src = {1, 2, 3, 4, 5};
        auto s = RandX::RandSample(src, 0);
        CHECK(s.empty());

    }
    TEST_CASE("RandShuffle 保持元素集合")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        std::vector<int> orig = v;
        RandX::RandShuffle(v);
        std::sort(v.begin(), v.end());
        CHECK(v == orig);

    }
    TEST_CASE("vector<bool> 代理引用抽样与洗牌多重集合不变量")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<bool> vb{ false, false, true };
        for (int i = 0; i < 50; ++i)
        {
            auto sample = RandX::RandSample(vb, 2);
            CHECK(sample.size() == 2);
            int true_cnt = (sample[0] ? 1 : 0) + (sample[1] ? 1 : 0);
            CHECK(true_cnt <= 1);
        }

        std::vector<bool> vb_shuffle{ false, false, true, true, false, true };
        const auto orig_true = std::count(vb_shuffle.begin(), vb_shuffle.end(), true);
        const auto orig_false = std::count(vb_shuffle.begin(), vb_shuffle.end(), false);
        RandX::RandShuffle(vb_shuffle);
        CHECK(std::count(vb_shuffle.begin(), vb_shuffle.end(), true) == orig_true);
        CHECK(std::count(vb_shuffle.begin(), vb_shuffle.end(), false) == orig_false);

    }
    TEST_CASE("vector<bool> 抽样与洗牌代理引用支持")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 42 };
        std::vector<bool> vb = { false, false, true };
        for (int i = 0; i < 50; ++i)
        {
            auto sampled1 = RandX::RandSample(vb, 2);
            int trueCount1 = 0;
            for (bool b : sampled1)
            {
                if (b) ++trueCount1;
            }
            CHECK(trueCount1 <= 1);

            auto sampled2 = RandX::RandSample(rng, vb.begin(), vb.end(), 2);
            int trueCount2 = 0;
            for (bool b : sampled2)
            {
                if (b) ++trueCount2;
            }
            CHECK(trueCount2 <= 1);
        }

        std::vector<bool> vb_shuffle = { false, false, true, true, false };
        for (int i = 0; i < 20; ++i)
        {
            RandX::RandShuffle(vb_shuffle);
            int trueCount = 0;
            for (bool b : vb_shuffle)
            {
                if (b) ++trueCount;
            }
            CHECK(trueCount == 2);
        }

    }
    TEST_CASE("RandSample 非默认构造只读复制元素覆盖全部路径")
    {
        using Item = RandXTest::SamplingContractFixtures::NonDefaultReadOnlyCopyItem;
        static_assert(!std::is_default_constructible<Item>::value, "sample item must not need a default constructor");
        static_assert(std::is_copy_constructible<Item>::value, "sample item must be copy constructible");
        static_assert(!std::is_copy_assignable<Item>::value, "sample item must not need copy assignment");

        const auto makePopulation = [](std::size_t size, std::size_t& copyCount) {
            std::vector<Item> population;
            population.reserve(size);
            for (std::size_t position = 0; position < size; ++position)
                population.emplace_back(position, static_cast<int>(position % 2), copyCount);
            return population;
        };
        const auto verifyPositions = [](const std::vector<Item>& sample, std::size_t expectedSize, std::size_t populationSize) {
            REQUIRE(sample.size() == expectedSize);
            std::set<std::size_t> positions;
            for (const Item& item : sample)
            {
                CHECK(item.position < populationSize);
                positions.insert(item.position);
            }
            CHECK(positions.size() == expectedSize);
        };

        const std::size_t hashThreshold = RandX::detail::HashSetThresholdK;
        std::size_t copyCount = 0;
        auto hashPopulation = makePopulation(hashThreshold + 1, copyCount);
        const std::ptrdiff_t hashSampleCount = 1;
        CHECK(static_cast<std::uint64_t>(hashSampleCount)
            <= (static_cast<std::uint64_t>(hashPopulation.size()) - 1) / RandX::detail::HashSetThresholdK);
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto hashSample = RandX::RandSample(hashPopulation.begin(), hashPopulation.end(), hashSampleCount);
        verifyPositions(hashSample, static_cast<std::size_t>(hashSampleCount), hashPopulation.size());
        CHECK(copyCount == static_cast<std::size_t>(hashSampleCount));

        const std::size_t indexPopulationSize = hashThreshold * 2;
        auto indexPopulation = makePopulation(indexPopulationSize, copyCount);
        const std::ptrdiff_t indexSampleCount = 2;
        CHECK(static_cast<std::uint64_t>(indexSampleCount)
            > (static_cast<std::uint64_t>(indexPopulation.size()) - 1) / RandX::detail::HashSetThresholdK);
        RandX::Xoshiro256StarStar indexEngine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        copyCount = 0;
        const auto indexSample = RandX::RandSample(
            indexEngine, indexPopulation.begin(), indexPopulation.end(), indexSampleCount);
        verifyPositions(indexSample, static_cast<std::size_t>(indexSampleCount), indexPopulation.size());
        CHECK(copyCount == static_cast<std::size_t>(indexSampleCount));

        const std::size_t bitmapThreshold = static_cast<std::size_t>(RandX::detail::SampleBitmapThresholdK);
        const std::size_t bitmapDensityDivisor = static_cast<std::size_t>(RandX::detail::SampleBitmapDensityDivisor);
        auto bitmapPopulation = makePopulation(bitmapThreshold, copyCount);
        const std::size_t bitmapSampleCount = 1;
        CHECK(bitmapSampleCount > (bitmapPopulation.size() - 1) / bitmapThreshold);
        CHECK(bitmapSampleCount <= bitmapPopulation.size() / bitmapDensityDivisor);
        RandX::Xoshiro256StarStar bitmapEngine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        copyCount = 0;
        const auto bitmapSample = RandX::RandSample(bitmapEngine, bitmapPopulation, bitmapSampleCount);
        verifyPositions(bitmapSample, bitmapSampleCount, bitmapPopulation.size());
        CHECK(copyCount == bitmapSampleCount);

        auto fullPopulation = makePopulation(4, copyCount);
        RandX::Xoshiro256StarStar fullEngine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto fullEngineState = fullEngine;
        copyCount = 0;
        const auto fullSample = RandX::RandSample(
            fullEngine, fullPopulation, (std::numeric_limits<std::size_t>::max)());
        verifyPositions(fullSample, fullPopulation.size(), fullPopulation.size());
        CHECK(copyCount == fullPopulation.size());
        CHECK(fullEngine == fullEngineState);
        for (std::size_t position = 0; position < fullSample.size(); ++position)
        {
            CHECK(fullSample[position].position == position);
            CHECK(fullSample[position].value == static_cast<int>(position % 2));
        }

    }
    TEST_CASE("RandSample 位图跨字边界保留成员范围")
    {
        const std::size_t wordBits = static_cast<std::size_t>(RandX::detail::SampleBitmapWordBits);
        const std::size_t bitmapThreshold = static_cast<std::size_t>(RandX::detail::SampleBitmapThresholdK);
        const std::size_t densityDivisor = static_cast<std::size_t>(RandX::detail::SampleBitmapDensityDivisor);
        const std::size_t populationSizes[] = {wordBits - 1, wordBits, wordBits + 1};
        const std::size_t sampleCount = 1;
        std::size_t bitmapCases = 0;

        for (const std::size_t populationSize : populationSizes)
        {
            if (populationSize == 0 || populationSize > bitmapThreshold)
                continue;
            if (sampleCount <= (populationSize - 1) / bitmapThreshold
                || sampleCount > populationSize / densityDivisor)
                continue;

            std::vector<int> population(populationSize);
            for (std::size_t position = 0; position < population.size(); ++position)
                population[position] = static_cast<int>(position);
            RandX::Xoshiro256StarStar engine(RandXTest::TestConstants::kDefaultEngineTestSeed);
            const auto sample = RandX::RandSample(engine, population, sampleCount);
            REQUIRE(sample.size() == sampleCount);
            CHECK(std::find(population.begin(), population.end(), sample.front()) != population.end());
            ++bitmapCases;
        }
        CHECK(bitmapCases > 0);

    }
    TEST_CASE("RandSample 重复值按不同源位置独立入选")
    {
        using Item = RandXTest::SamplingContractFixtures::NonDefaultReadOnlyCopyItem;
        const std::size_t hashThreshold = RandX::detail::HashSetThresholdK;
        const std::size_t populationSize = hashThreshold * 2 + 1;
        const std::ptrdiff_t sampleCount = 2;
        constexpr int repeatedValue = 73;
        std::size_t copyCount = 0;
        std::vector<Item> population;
        population.reserve(populationSize);
        for (std::size_t position = 0; position < populationSize; ++position)
            population.emplace_back(position, repeatedValue, copyCount);

        RandX::Xoshiro256StarStar engine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto sample = RandX::RandSample(engine, population.begin(), population.end(), sampleCount);
        REQUIRE(sample.size() == static_cast<std::size_t>(sampleCount));
        CHECK(sample[0].value == repeatedValue);
        CHECK(sample[1].value == repeatedValue);
        CHECK(sample[0].position != sample[1].position);
        CHECK(copyCount == static_cast<std::size_t>(sampleCount));

    }
    TEST_CASE("RandSample 数量边界保留早返回状态")
    {
        const std::vector<int> population{11, 17, 23, 29};
        const std::ptrdiff_t populationSize = static_cast<std::ptrdiff_t>(population.size());

        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto defaultInitialState = RandX::DefaultEngine();
        const auto negativeDefault = RandX::RandSample(population.begin(), population.end(), std::ptrdiff_t{-1});
        CHECK(negativeDefault.empty());
        CHECK(RandX::DefaultEngine() == defaultInitialState);
        const auto zeroDefault = RandX::RandSample(population.begin(), population.end(), std::ptrdiff_t{0});
        CHECK(zeroDefault.empty());
        CHECK(RandX::DefaultEngine() == defaultInitialState);

        RandXTest::SamplingContractFixtures::CountingEngine explicitEngine;
        const auto negativeExplicit = RandX::RandSample(
            explicitEngine, population.begin(), population.end(), std::ptrdiff_t{-1});
        CHECK(negativeExplicit.empty());
        const auto zeroExplicit = RandX::RandSample(
            explicitEngine, population.begin(), population.end(), std::ptrdiff_t{0});
        CHECK(zeroExplicit.empty());
        CHECK(explicitEngine.callCount == 0);

        const std::vector<int> emptyPopulation;
        const auto emptyExplicit = RandX::RandSample(
            explicitEngine, emptyPopulation.begin(), emptyPopulation.end(), std::ptrdiff_t{1});
        CHECK(emptyExplicit.empty());
        CHECK(explicitEngine.callCount == 0);

        const auto fullExplicit = RandX::RandSample(
            explicitEngine, population.begin(), population.end(), populationSize);
        REQUIRE(fullExplicit == population);
        CHECK(explicitEngine.callCount == 0);
        const auto oversizedExplicit = RandX::RandSample(
            explicitEngine, population.begin(), population.end(), populationSize + 1);
        CHECK(oversizedExplicit == population);
        CHECK(explicitEngine.callCount == 0);
        const auto maximumContainerRequest = RandX::RandSample(
            explicitEngine, population, (std::numeric_limits<std::size_t>::max)());
        CHECK(maximumContainerRequest == population);
        CHECK(explicitEngine.callCount == 0);

        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto containerInitialState = RandX::DefaultEngine();
        CHECK(RandX::RandSample(population, std::size_t{0}).empty());
        CHECK(RandX::DefaultEngine() == containerInitialState);

        const std::vector<int> emptyContainer;
        CHECK(RandX::RandSample(emptyContainer, std::size_t{1}).empty());
        CHECK(RandX::DefaultEngine() == containerInitialState);
        CHECK(RandX::RandSample(
            emptyContainer, (std::numeric_limits<std::size_t>::max)()).empty());
        CHECK(RandX::DefaultEngine() == containerInitialState);

        const std::vector<int> singlePopulation{43};
        RandXTest::SamplingContractFixtures::CountingEngine singleEngine;
        const auto singleElement = RandX::RandSample(
            singleEngine, singlePopulation.begin(), singlePopulation.end(), std::ptrdiff_t{1});
        CHECK((singleElement == std::vector<int>{43}));
        CHECK(singleEngine.callCount == 0);

        const std::vector<int> inputPopulation{47, 53, 59};
        using InputIterator = RandXTest::SamplingContractFixtures::InputIterator<int>;
        RandXTest::SamplingContractFixtures::OperationTrace inputTrace;
        const InputIterator inputFirst{inputPopulation.data(), &inputTrace};
        const InputIterator inputLast{inputPopulation.data() + inputPopulation.size(), &inputTrace};
        RandXTest::SamplingContractFixtures::CountingEngine inputEngine;
        CHECK(RandX::RandSample(inputEngine, inputFirst, inputLast, std::ptrdiff_t{-1}).empty());
        CHECK(RandX::RandSample(inputEngine, inputFirst, inputLast, std::ptrdiff_t{0}).empty());
        CHECK(inputTrace.dereferences == 0);
        CHECK(inputTrace.increments == 0);
        CHECK(inputEngine.callCount == 0);

        const std::vector<int> emptyInputPopulation;
        const InputIterator emptyInputFirst{emptyInputPopulation.data(), &inputTrace};
        const InputIterator emptyInputLast{emptyInputPopulation.data(), &inputTrace};
        CHECK(RandX::RandSample(inputEngine, emptyInputFirst, emptyInputLast, std::ptrdiff_t{3}).empty());
        CHECK(inputTrace.dereferences == 0);
        CHECK(inputTrace.increments == 0);
        CHECK(inputEngine.callCount == 0);

        std::list<int> shortInput{31, 37};
        RandXTest::SamplingContractFixtures::CountingEngine reservoirEngine;
        const auto exhausted = RandX::RandSample(
            reservoirEngine, shortInput.begin(), shortInput.end(), std::ptrdiff_t{5});
        CHECK((exhausted == std::vector<int>{31, 37}));
        CHECK(reservoirEngine.callCount == 0);

    }
    TEST_CASE("RandSample istream_iterator 初填耗尽不消耗随机数")
    {
        constexpr int firstValue = 59;
        constexpr int secondValue = 61;
        std::istringstream input("59 61");
        std::istream_iterator<int> first(input);
        const std::istream_iterator<int> last;
        RandXTest::SamplingContractFixtures::CountingEngine engine;
        const auto sample = RandX::RandSample(engine, first, last, std::ptrdiff_t{5});
        CHECK((sample == std::vector<int>{firstValue, secondValue}));
        CHECK(engine.callCount == 0);

    }
    TEST_CASE("RandSample 蓄水池超大请求返回短输入且不取引擎")
    {
        constexpr std::ptrdiff_t kMaximumRequest =
            (std::numeric_limits<std::ptrdiff_t>::max)();
        const std::list<int> emptyInput;
        const std::list<int> shortInput{
            RandXTest::SamplingTypeFixtures::kFirstReservoirValue,
            RandXTest::SamplingTypeFixtures::kSecondReservoirValue};
        const std::vector<int> expected{
            RandXTest::SamplingTypeFixtures::kFirstReservoirValue,
            RandXTest::SamplingTypeFixtures::kSecondReservoirValue};

        REQUIRE(shortInput.size() == RandXTest::SamplingTypeFixtures::kShortReservoirValueCount);

        RandXTest::SamplingContractFixtures::CountingEngine emptyEngine;
        CHECK(RandX::RandSample(emptyEngine, emptyInput.cbegin(), emptyInput.cend(), kMaximumRequest).empty());
        CHECK(emptyEngine.callCount == 0);

        RandXTest::SamplingContractFixtures::CountingEngine shortEngine;
        const auto explicitSample = RandX::RandSample(
            shortEngine, shortInput.cbegin(), shortInput.cend(), kMaximumRequest);
        CHECK(explicitSample == expected);
        CHECK(shortEngine.callCount == 0);

        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto initialDefaultEngine = RandX::DefaultEngine();
        CHECK(RandX::RandSample(emptyInput.cbegin(), emptyInput.cend(), kMaximumRequest).empty());
        CHECK(RandX::DefaultEngine() == initialDefaultEngine);
        const auto defaultSample = RandX::RandSample(
            shortInput.cbegin(), shortInput.cend(), kMaximumRequest);
        CHECK(defaultSample == expected);
        CHECK(RandX::DefaultEngine() == initialDefaultEngine);

    }
    TEST_CASE("RandSample 默认与显式引擎连续状态独立")
    {
        const std::size_t populationSize = RandX::detail::HashSetThresholdK * 2;
        const std::ptrdiff_t sampleCount = 2;
        std::vector<std::size_t> population(populationSize);
        for (std::size_t position = 0; position < population.size(); ++position)
            population[position] = position;

        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar explicitEngine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (std::size_t callIndex = 0; callIndex < 2; ++callIndex)
        {
            const auto defaultSample = RandX::RandSample(population.begin(), population.end(), sampleCount);
            const auto explicitSample = RandX::RandSample(
                explicitEngine, population.begin(), population.end(), sampleCount);
            CHECK(defaultSample == explicitSample);
            CHECK(RandX::DefaultEngine() == explicitEngine);
        }

        RandX::Xoshiro256StarStar firstEngine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar independentEngine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto independentInitialState = independentEngine;
        const auto firstSample = RandX::RandSample(
            firstEngine, population.begin(), population.end(), sampleCount);
        CHECK(firstEngine != independentInitialState);
        CHECK(independentEngine == independentInitialState);
        const auto independentSample = RandX::RandSample(
            independentEngine, population.begin(), population.end(), sampleCount);
        CHECK(firstSample == independentSample);
        CHECK(firstEngine == independentEngine);
        const auto firstStateAfterOneCall = firstEngine;
        (void)RandX::RandSample(firstEngine, population.begin(), population.end(), sampleCount);
        CHECK(firstEngine != firstStateAfterOneCall);
        (void)RandX::RandSample(independentEngine, population.begin(), population.end(), sampleCount);
        CHECK(firstEngine == independentEngine);

    }
    TEST_CASE("RandSample 显式引擎支持位宽、非零最小值与不可复制引擎")
    {
        const std::size_t hashPopulationSize = RandX::detail::HashSetThresholdK + 1;
        const std::ptrdiff_t hashSampleCount = 1;
        std::vector<int> hashPopulation(hashPopulationSize);
        for (std::size_t position = 0; position < hashPopulation.size(); ++position)
            hashPopulation[position] = static_cast<int>(position);

        RandXTest::ExtendedFixtures::Synthetic32BitWideType engine32;
        const auto sample32 = RandX::RandSample(
            engine32, hashPopulation.begin(), hashPopulation.end(), hashSampleCount);
        REQUIRE(sample32.size() == static_cast<std::size_t>(hashSampleCount));
        CHECK(sample32.front() >= 0);
        CHECK(static_cast<std::size_t>(sample32.front()) < hashPopulation.size());

        RandX::Xoshiro256StarStar engine64(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto sample64 = RandX::RandSample(
            engine64, hashPopulation.begin(), hashPopulation.end(), hashSampleCount);
        REQUIRE(sample64.size() == static_cast<std::size_t>(hashSampleCount));

        const std::size_t indexPopulationSize = RandX::detail::HashSetThresholdK * 2;
        const std::ptrdiff_t indexSampleCount = 2;
        std::vector<int> indexPopulation(indexPopulationSize);
        for (std::size_t position = 0; position < indexPopulation.size(); ++position)
            indexPopulation[position] = static_cast<int>(position);
        RandXTest::OverloadFixtures::MoveOnlyEngine moveOnlyEngine;
        static_assert(!std::is_copy_constructible<RandXTest::OverloadFixtures::MoveOnlyEngine>::value,
            "the sample engine fixture must remain non-copyable");
        const auto moveOnlySample = RandX::RandSample(
            moveOnlyEngine, indexPopulation.begin(), indexPopulation.end(), indexSampleCount);
        REQUIRE(moveOnlySample.size() == static_cast<std::size_t>(indexSampleCount));

        const std::vector<int> inputValues{41, 43, 47, 53};
        RandXTest::ExtendedFixtures::SyntheticNonZeroMinEngine nonZeroMinEngine;
        static_assert(RandXTest::ExtendedFixtures::SyntheticNonZeroMinEngine::min() != 0,
            "fixture must exercise a non-zero engine minimum");
        const auto nonZeroMinSample = RandX::RandSample(
            nonZeroMinEngine, inputValues.begin(), inputValues.end(), std::ptrdiff_t{2});
        REQUIRE(nonZeroMinSample.size() == 2);
        for (const int value : nonZeroMinSample)
            CHECK(std::find(inputValues.begin(), inputValues.end(), value) != inputValues.end());

    }
    TEST_CASE("RandSample 稀疏小样本保持重抽次数接受顺序与默认引擎状态")
    {
        constexpr std::size_t kRequestCounts[] = {1, 2, 3};
        constexpr std::size_t kPopulationMultiplier = 4;
        constexpr std::size_t kPopulationSize = RandX::detail::HashSetThresholdK * kPopulationMultiplier;
        constexpr std::size_t kRepeatedIndex = RandX::detail::HashSetThresholdK / 2;
        constexpr std::size_t kDistinctIndex = kRepeatedIndex + 1;
        constexpr std::size_t kThirdIndex = kDistinctIndex + 1;
        using Engine = RandXTest::SamplingContractFixtures::ScriptedSampleIndexEngine<kPopulationSize>;
        const std::vector<std::uint64_t> draws{
            kRepeatedIndex, kRepeatedIndex, kRepeatedIndex, kDistinctIndex, kThirdIndex};
        std::vector<std::size_t> population(kPopulationSize);
        std::iota(population.begin(), population.end(), std::size_t{0});

        for (const std::size_t count : kRequestCounts)
        {
            Engine expectedEngine{draws};
            std::unordered_set<std::uint64_t> expectedIndices;
            std::vector<std::size_t> expected;
            while (expected.size() < count)
            {
                std::uniform_int_distribution<std::uint64_t> distribution(0, kPopulationSize - 1);
                const auto index = distribution(expectedEngine);
                if (expectedIndices.insert(index).second) expected.push_back(population[index]);
            }
            Engine iteratorEngine{draws};
            CHECK(RandX::RandSample(iteratorEngine, population.begin(), population.end(),
                static_cast<std::ptrdiff_t>(count)) == expected);
            CHECK(iteratorEngine.callCount == expectedEngine.callCount);
            Engine containerEngine{draws};
            CHECK(RandX::RandSample(containerEngine, population, count) == expected);
            CHECK(containerEngine.callCount == expectedEngine.callCount);

            RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
            auto referenceEngine = RandX::DefaultEngine();
            std::uniform_int_distribution<std::uint64_t> distribution(0, kPopulationSize - 1);
            std::unordered_set<std::uint64_t> selected;
            std::vector<std::size_t> reference;
            while (reference.size() < count)
            {
                const auto index = distribution(referenceEngine);
                if (selected.insert(index).second) reference.push_back(population[index]);
            }
            CHECK(RandX::RandSample(population.begin(), population.end(),
                static_cast<std::ptrdiff_t>(count)) == reference);
            CHECK(RandX::DefaultEngine() == referenceEngine);
        }
    }
    TEST_CASE("RandSample 稀疏索引对复制异常保留重复重抽后的引擎与源进度")
    {
        constexpr std::size_t kRequestCount = 2;
        constexpr std::size_t kPopulationMultiplier = 4;
        constexpr std::size_t kPopulationSize = RandX::detail::HashSetThresholdK * kPopulationMultiplier;
        constexpr std::size_t kRepeatedIndex = RandX::detail::HashSetThresholdK / 2;
        constexpr std::size_t kDistinctIndex = kRepeatedIndex + 1;
        using Engine = RandXTest::SamplingContractFixtures::ScriptedSampleIndexEngine<kPopulationSize>;
        using Item = RandXTest::SamplingContractFixtures::ThrowingCopyItem;
        using Trace = RandXTest::SamplingContractFixtures::OperationTrace;
        using Iterator = RandXTest::SamplingContractFixtures::RandomAccessIterator<Item>;
        Engine engine{{kRepeatedIndex, kRepeatedIndex, kRepeatedIndex, kDistinctIndex}};
        auto expectedEngine = engine;
        std::unordered_set<std::uint64_t> expectedIndices;
        while (expectedIndices.size() < kRequestCount)
        {
            std::uniform_int_distribution<std::uint64_t> distribution(0, kPopulationSize - 1);
            expectedIndices.insert(distribution(expectedEngine));
        }
        Trace itemTrace;
        itemTrace.throwOnCopyAttempt = kRequestCount;
        itemTrace.engineCallCount = &engine.callCount;
        Trace sourceTrace;
        std::vector<Item> population;
        population.reserve(kPopulationSize);
        for (std::size_t position = 0; position < kPopulationSize; ++position)
            population.emplace_back(static_cast<int>(position), itemTrace);
        const Iterator first{population.data(), &sourceTrace};
        const Iterator last{population.data() + population.size(), &sourceTrace};

        CHECK_THROWS_AS((void)RandX::RandSample(engine, first, last,
            static_cast<std::ptrdiff_t>(kRequestCount)), std::runtime_error);
        CHECK(engine.callCount == expectedEngine.callCount);
        CHECK(itemTrace.engineCallsAtThrow == engine.callCount);
        CHECK(itemTrace.copyAttempts == kRequestCount);
        CHECK(sourceTrace.dereferences == kRequestCount);
        CHECK(sourceTrace.increments == 0);
    }
    TEST_CASE("RandSample 随机访问复制异常保留引擎与源进度")
    {
        using Item = RandXTest::SamplingContractFixtures::ThrowingCopyItem;
        using Trace = RandXTest::SamplingContractFixtures::OperationTrace;
        using Iterator = RandXTest::SamplingContractFixtures::RandomAccessIterator<Item>;
        const auto makePopulation = [](std::size_t size, Trace& trace) {
            std::vector<Item> population;
            population.reserve(size);
            for (std::size_t position = 0; position < size; ++position)
                population.emplace_back(static_cast<int>(position), trace);
            return population;
        };

        const std::size_t hashThreshold = RandX::detail::HashSetThresholdK;
        Trace hashCopyTrace;
        hashCopyTrace.throwOnCopyAttempt = 1;
        Trace hashSourceTrace;
        auto hashPopulation = makePopulation(hashThreshold + 1, hashCopyTrace);
        const Iterator hashFirst{hashPopulation.data(), &hashSourceTrace};
        const Iterator hashLast{hashPopulation.data() + hashPopulation.size(), &hashSourceTrace};
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto hashInitialState = RandX::DefaultEngine();
        CHECK_THROWS_AS((void)RandX::RandSample(hashFirst, hashLast, std::ptrdiff_t{1}), std::runtime_error);
        CHECK(RandX::DefaultEngine() != hashInitialState);
        CHECK(hashCopyTrace.copyAttempts == 1);
        CHECK(hashSourceTrace.dereferences == 1);
        CHECK(hashSourceTrace.increments == 0);

        const std::size_t indexPopulationSize = hashThreshold * 2;
        Trace indexCopyTrace;
        indexCopyTrace.throwOnCopyAttempt = 1;
        Trace indexSourceTrace;
        auto indexPopulation = makePopulation(indexPopulationSize, indexCopyTrace);
        const Iterator indexFirst{indexPopulation.data(), &indexSourceTrace};
        const Iterator indexLast{indexPopulation.data() + indexPopulation.size(), &indexSourceTrace};
        RandXTest::SamplingContractFixtures::CountingEngine indexEngine;
        indexCopyTrace.engineCallCount = &indexEngine.callCount;
        CHECK_THROWS_AS((void)RandX::RandSample(indexEngine, indexFirst, indexLast, std::ptrdiff_t{2}),
            std::runtime_error);
        CHECK(indexEngine.callCount >= 2);
        CHECK(indexCopyTrace.engineCallsAtThrow == indexEngine.callCount);
        CHECK(indexCopyTrace.copyAttempts == 1);
        CHECK(indexSourceTrace.dereferences == 1);
        CHECK(indexSourceTrace.increments == 0);

        const std::size_t bitmapThreshold = static_cast<std::size_t>(RandX::detail::SampleBitmapThresholdK);
        Trace bitmapCopyTrace;
        bitmapCopyTrace.throwOnCopyAttempt = 1;
        Trace bitmapSourceTrace;
        auto bitmapPopulation = makePopulation(bitmapThreshold, bitmapCopyTrace);
        const RandXTest::SamplingContractFixtures::RandomAccessContainer<Item> bitmapRange{
            bitmapPopulation.data(), bitmapPopulation.size(), &bitmapSourceTrace};
        RandXTest::SamplingContractFixtures::CountingEngine bitmapEngine;
        bitmapCopyTrace.engineCallCount = &bitmapEngine.callCount;
        CHECK_THROWS_AS((void)RandX::RandSample(bitmapEngine, bitmapRange, std::size_t{1}),
            std::runtime_error);
        CHECK(bitmapEngine.callCount > 0);
        CHECK(bitmapCopyTrace.engineCallsAtThrow == bitmapEngine.callCount);
        CHECK(bitmapCopyTrace.copyAttempts == 1);
        CHECK(bitmapSourceTrace.dereferences == 1);
        CHECK(bitmapSourceTrace.increments == 0);

        Trace fullCopyTrace;
        fullCopyTrace.throwOnCopyAttempt = 1;
        Trace fullSourceTrace;
        auto fullPopulation = makePopulation(4, fullCopyTrace);
        const RandXTest::SamplingContractFixtures::RandomAccessContainer<Item> fullRange{
            fullPopulation.data(), fullPopulation.size(), &fullSourceTrace};
        RandXTest::SamplingContractFixtures::CountingEngine fullEngine;
        fullCopyTrace.engineCallCount = &fullEngine.callCount;
        CHECK_THROWS_AS((void)RandX::RandSample(
            fullEngine, fullRange, (std::numeric_limits<std::size_t>::max)()), std::runtime_error);
        CHECK(fullEngine.callCount == 0);
        CHECK(fullCopyTrace.engineCallsAtThrow == 0);
        CHECK(fullCopyTrace.copyAttempts == 1);
        CHECK(fullSourceTrace.dereferences == 1);
        CHECK(fullSourceTrace.increments == 0);

    }
    TEST_CASE("RandSample 随机访问引擎异常不提前访问源元素")
    {
        using Item = RandXTest::SamplingContractFixtures::ThrowingCopyItem;
        using Trace = RandXTest::SamplingContractFixtures::OperationTrace;
        using Iterator = RandXTest::SamplingContractFixtures::RandomAccessIterator<Item>;
        const std::size_t populationSize = RandX::detail::HashSetThresholdK + 1;
        Trace itemTrace;
        Trace sourceTrace;
        std::vector<Item> population;
        population.reserve(populationSize);
        for (std::size_t position = 0; position < populationSize; ++position)
            population.emplace_back(static_cast<int>(position), itemTrace);
        const Iterator first{population.data(), &sourceTrace};
        const Iterator last{population.data() + population.size(), &sourceTrace};
        RandXTest::SamplingContractFixtures::ThrowingEngine engine;

        CHECK_THROWS_AS((void)RandX::RandSample(engine, first, last, std::ptrdiff_t{1}), std::runtime_error);
        CHECK(engine.callCount == 1);
        CHECK(itemTrace.copyAttempts == 0);
        CHECK(sourceTrace.dereferences == 0);
        CHECK(sourceTrace.increments == 0);

    }
    TEST_CASE("RandSample 蓄水池复制赋值与引擎异常保留单遍进度")
    {
        using Item = RandXTest::SamplingContractFixtures::ThrowingCopyItem;
        using Trace = RandXTest::SamplingContractFixtures::OperationTrace;
        using Iterator = RandXTest::SamplingContractFixtures::InputIterator<Item>;
        const auto makePopulation = [](std::size_t size, Trace& trace) {
            std::vector<Item> population;
            population.reserve(size);
            for (std::size_t position = 0; position < size; ++position)
                population.emplace_back(static_cast<int>(position), trace);
            return population;
        };

        Trace fillCopyTrace;
        fillCopyTrace.throwOnCopyAttempt = 2;
        Trace fillSourceTrace;
        auto fillPopulation = makePopulation(4, fillCopyTrace);
        const Iterator fillFirst{fillPopulation.data(), &fillSourceTrace};
        const Iterator fillLast{fillPopulation.data() + fillPopulation.size(), &fillSourceTrace};
        RandXTest::SamplingContractFixtures::CountingEngine fillEngine;
        fillCopyTrace.engineCallCount = &fillEngine.callCount;
        CHECK_THROWS_AS((void)RandX::RandSample(fillEngine, fillFirst, fillLast, std::ptrdiff_t{3}),
            std::runtime_error);
        CHECK(fillCopyTrace.copyAttempts == 2);
        CHECK(fillEngine.callCount == 0);
        CHECK(fillSourceTrace.dereferences == 2);
        CHECK(fillSourceTrace.increments == 1);

        constexpr std::ptrdiff_t reservoirSize{2};
        Trace replacementTrace;
        replacementTrace.throwOnAssignmentAttempt = 1;
        Trace replacementSourceTrace;
        auto replacementPopulation = makePopulation(4, replacementTrace);
        const Iterator replacementFirst{replacementPopulation.data(), &replacementSourceTrace};
        const Iterator replacementLast{replacementPopulation.data() + replacementPopulation.size(),
            &replacementSourceTrace};
        RandXTest::SamplingContractFixtures::CountingEngine replacementEngine;
        replacementTrace.engineCallCount = &replacementEngine.callCount;
        CHECK_THROWS_AS((void)RandX::RandSample(
            replacementEngine, replacementFirst, replacementLast, reservoirSize), std::runtime_error);
        CHECK(replacementEngine.callCount > 0);
        CHECK(replacementTrace.engineCallsAtThrow == replacementEngine.callCount);
        CHECK(replacementTrace.copyAttempts >= static_cast<std::size_t>(reservoirSize));
        CHECK(replacementTrace.assignmentAttempts == 1);
        CHECK(replacementSourceTrace.dereferences == 3);
        CHECK(replacementSourceTrace.increments == 2);

        Trace engineFailureTrace;
        Trace engineFailureSourceTrace;
        auto engineFailurePopulation = makePopulation(4, engineFailureTrace);
        const Iterator engineFailureFirst{engineFailurePopulation.data(), &engineFailureSourceTrace};
        const Iterator engineFailureLast{engineFailurePopulation.data() + engineFailurePopulation.size(),
            &engineFailureSourceTrace};
        RandXTest::SamplingContractFixtures::ThrowingEngine throwingEngine;
        engineFailureTrace.engineCallCount = &throwingEngine.callCount;
        CHECK_THROWS_AS((void)RandX::RandSample(
            throwingEngine, engineFailureFirst, engineFailureLast, reservoirSize), std::runtime_error);
        CHECK(throwingEngine.callCount == 1);
        CHECK(engineFailureTrace.copyAttempts >= static_cast<std::size_t>(reservoirSize));
        CHECK(engineFailureSourceTrace.dereferences == 2);
        CHECK(engineFailureSourceTrace.increments == 2);

    }
}

#if defined(__SIZEOF_INT128__)
TEST_SUITE("公共/条件/128位差值/抽样")
{
    TEST_CASE("RandSample 超宽迭代器差值长度边界")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        using WideDiff = RandXTest::WideSampleFixtures::WideDiff;
        constexpr int IndexBits = std::numeric_limits<std::uint64_t>::digits;
        constexpr WideDiff ExtraLength = 5;
        constexpr WideDiff Length = (WideDiff{1} << IndexBits) + ExtraLength;
        const RandXTest::WideSampleFixtures::WideRange population{Length};
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
}
#endif

#endif
