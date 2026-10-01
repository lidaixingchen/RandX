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
