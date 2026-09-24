// test_randx_cpp17.cpp — RandX_Cpp17.hpp (C++17) 确定性单元测试（doctest 版）
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "RandX_Cpp17.hpp"

#include <array>
#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <list>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// 统计检验临界值（非魔法数字：自由度 99/127，alpha=0.001 的卡方临界值）
namespace
{
    // 自由度 99, alpha=0.001 临界值 ≈ 148.2
    constexpr double CHI2_CRITICAL_DF99 = 148.2;
    // 自由度 127, alpha=0.001 临界值 ≈ 173.6
    constexpr double CHI2_CRITICAL_DF127 = 173.6;
    // 统计检验样本数与分箱数
    constexpr int STAT_N = 1'000'000;
    constexpr int STAT_BINS_100 = 100;
    constexpr int STAT_BINS_128 = 128;
    // 蒙特卡洛试验次数
    constexpr int MC_TRIALS_1K = 1000;
    constexpr int MC_TRIALS_10K = 10000;
    constexpr int MC_TRIALS_100 = 100;
    // ChaCha20 卡方检验样本数：须 < 2^20/8 = 131072 以避免触发自动 reseed（保持确定性）
    constexpr int CHACHA20_CHI2_N = 100'000;
}

using namespace RandX;

// ============================================================================
// 已知序列断言（seed=12345）— 7 引擎 KAT
// 与 test_randx.cpp 期望值完全一致（见附录 C 双标准同步策略）
// ============================================================================
TEST_SUITE("已知序列 seed=12345")
{
    TEST_CASE("Xoshiro256StarStar")
    {
        Xoshiro256StarStar rng{ 12345 };
        const std::uint64_t expected[] = {
            13720838825685603483ULL, 2398916695208396998ULL,
            17770384849984869256ULL, 891717726879801395ULL,
            10241316046318454344ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("Xoroshiro128StarStar")
    {
        Xoroshiro128StarStar rng{ 12345 };
        const std::uint64_t expected[] = {
            9940793396233540349ULL, 8784320640503919345ULL,
            16208043774633962581ULL, 11032235639386297630ULL,
            4698907930579033109ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("Xoshiro128StarStar")
    {
        Xoshiro128StarStar rng{ 12345 };
        const std::uint32_t expected[] = {
            1096865841U, 933661059U, 3314798965U, 1305642763U, 1040785987U
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("Xoroshiro64StarStar")
    {
        Xoroshiro64StarStar rng{ 12345 };
        const std::uint32_t expected[] = {
            63958076U, 2181105171U, 532052331U, 3458610118U, 2965685819U
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("SplitMix64")
    {
        SplitMix64 rng{ 12345 };
        const std::uint64_t expected[] = {
            2454886589211414944ULL, 3778200017661327597ULL,
            2205171434679333405ULL, 3248800117070709450ULL,
            9350289611492784363ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("SFC64")
    {
        SFC64 rng{ 12345 };
        const std::uint64_t expected[] = {
            13526236746588683560ULL, 8823148983839225293ULL,
            5240613241081073383ULL, 17030394482648619497ULL,
            7698197985592869707ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("RomuDuoJr")
    {
        RomuDuoJr rng{ 12345 };
        const std::uint64_t expected[] = {
            2454886589211414944ULL, 12510505629750556783ULL,
            16962469053573158940ULL, 8350026492023846583ULL,
            15401281827437905834ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }
}

// ============================================================================
// 引擎基础设施
// ============================================================================
TEST_SUITE("引擎基础设施")
{
    TEST_CASE("MakeStreamEngine 多流不重叠")
    {
        auto s0 = MakeStreamEngine<Xoshiro256StarStar>(0, 12345);
        auto s1 = MakeStreamEngine<Xoshiro256StarStar>(1, 12345);
        bool allSame = true;
        for (int i = 0; i < 10; ++i)
        {
            if (s0() != s1()) allSame = false;
        }
        CHECK_FALSE(allSame);
    }

    TEST_CASE("serialize / deserialize 状态恢复")
    {
        RandX::Xoshiro256StarStar rng{ 99999 };
        rng.discard(10);
        auto state = rng.serialize();
        RandX::Xoshiro256StarStar rng2{ state };
        for (int i = 0; i < 10; ++i)
            CHECK(rng() == rng2());
    }

    TEST_CASE("全零吸收态逃逸保证")
    {
        std::array<std::uint64_t, 4> zeroState{};
        RandX::Xoshiro256StarStar rng{ zeroState };
        CHECK_FALSE((rng() == 0 && rng() == 0 && rng() == 0));

        RandX::Xoshiro256StarStar rng2{ 12345 };
        rng2.deserialize(zeroState);
        CHECK_FALSE((rng2() == 0 && rng2() == 0 && rng2() == 0));
    }

    // 全零状态自动重置为有效初值 s_[0]=1（SFC64 为 0x9E37...），重置后序列完全确定，
    // 且与 test_randx.cpp 的同名断言保持一致（见 docs/API.md「全零状态静默修正」）
    TEST_CASE("全零输入自动重置确定性序列")
    {
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
        static_assert(std::is_trivially_destructible_v<RandX::Xoshiro256StarStar>);
        static_assert(std::is_trivially_destructible_v<RandX::Xoroshiro128StarStar>);
        static_assert(std::is_trivially_destructible_v<RandX::Xoshiro128StarStar>);
        static_assert(std::is_trivially_destructible_v<RandX::Xoroshiro64StarStar>);
        static_assert(std::is_trivially_destructible_v<RandX::SplitMix64>);
        static_assert(std::is_trivially_destructible_v<RandX::SFC64>);
        static_assert(std::is_trivially_destructible_v<RandX::RomuDuoJr>);
        CHECK(true);
    }

    TEST_CASE("discard 与连续调用等价")
    {
        Xoshiro256StarStar a{ 42 };
        Xoshiro256StarStar b{ 42 };
        a.discard(100);
        for (int i = 0; i < 100; ++i) b();
        CHECK(a() == b());
    }

    TEST_CASE("operator== / != 相等性")
    {
        Xoshiro256StarStar a{ 1 }, b{ 1 }, c{ 2 };
        CHECK(a == b);
        CHECK(a != c);
    }

    TEST_CASE("jump / longJump 平稳步进")
    {
        Xoshiro256StarStar rng{ 777 };
        rng.jump();
        rng.longJump();
        (void)rng();
    }

    TEST_CASE("std::seed_seq 播种确定性")
    {
        std::seed_seq seq{ 1, 2, 3, 4, 5, 6, 7, 8 };
        Xoshiro256StarStar rng1{ seq };
        std::seed_seq seq2{ 1, 2, 3, 4, 5, 6, 7, 8 };
        Xoshiro256StarStar rng2{ seq2 };
        CHECK(rng1() == rng2());
        CHECK(rng1() == rng2());
    }
}

// ============================================================================
// 便捷 API
// ============================================================================
TEST_SUITE("便捷 API")
{
    TEST_CASE("RandInt / RandReal 范围闭/半开区间")
    {
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            int v = RandInt(1, 6);
            CHECK(v >= 1);
            CHECK(v <= 6);
        }
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            double v = RandReal();
            CHECK(v >= 0.0);
            CHECK(v < 1.0);
        }
    }

    TEST_CASE("RandNormal 均值接近 0")
    {
        double sum = 0;
        for (int i = 0; i < MC_TRIALS_10K; ++i)
            sum += RandNormal(0.0, 1.0);
        double mean = sum / MC_TRIALS_10K;
        CHECK(mean > -0.1);
        CHECK(mean < 0.1);
    }

    TEST_CASE("RandShuffle 保持元素集合")
    {
        std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        std::vector<int> orig = v;
        RandShuffle(v);
        CHECK(v != orig);
        std::sort(v.begin(), v.end());
        CHECK(v == orig);
    }

    TEST_CASE("RandWeighted 正权重采样保证")
    {
        std::vector<double> weights = { 0.0, 0.0, 1.0, 0.0 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandWeighted(weights) == 2);
    }

    TEST_CASE("RandElement 容器版返回元素引用")
    {
        std::array<int, 3> arr = { {10, 20, 30} };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            int v = RandElement(arr);
            CHECK((v == 10 || v == 20 || v == 30));
        }
    }

    TEST_CASE("Reseed 可重播种确定性")
    {
        Reseed(42);
        const auto a = RandInt(0, 1000000);
        Reseed(42);
        const auto b = RandInt(0, 1000000);
        CHECK(a == b);
    }

    TEST_CASE("扩展便捷 API 集成")
    {
        // RandSample：无放回抽样
        std::vector<int> pool = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        auto sample = RandSample(pool, 3);
        CHECK(sample.size() == 3);
        for (const auto& x : sample)
            CHECK(std::find(pool.begin(), pool.end(), x) != pool.end());
        CHECK((sample[0] != sample[1] || sample[1] != sample[2] || sample[0] != sample[2]));

        // RandPermutation：随机排列
        auto perm = RandPermutation(10);
        CHECK(perm.size() == 10);
        auto sorted_perm = perm;
        std::sort(sorted_perm.begin(), sorted_perm.end());
        for (std::size_t i = 0; i < 10; ++i)
            CHECK(sorted_perm[i] == static_cast<int>(i));

        // RandString：随机字符串
        auto str = RandString(16);
        CHECK(str.size() == 16);
        auto str2 = RandString(8, "01");
        CHECK(str2.size() == 8);
        for (const auto& c : str2) CHECK((c == '0' || c == '1'));

        // RandExp：指数分布（验证非负）
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandExp() >= 0.0);

        // RandPoisson：泊松分布（验证非负）
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandPoisson() >= 0);

        // RandGamma：伽马分布（验证正数）
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandGamma() > 0.0);

        // RandBits：N 位随机数
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            CHECK(RandBits<8>() < 256);
            CHECK(RandBits<1>() < 2);
        }

        // RandUUID：格式验证
        auto uuid = RandUUID();
        CHECK(uuid.size() == 36);
        CHECK(uuid[8] == '-');
        CHECK(uuid[13] == '-');
        CHECK(uuid[18] == '-');
        CHECK(uuid[23] == '-');
        CHECK(uuid[14] == '4');  // 版本号
        CHECK((uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b'));
    }

    TEST_CASE("RandSample 容器版支持不可默认构造类型")
    {
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
        int arr[5] = { 10, 20, 30, 40, 50 };
        auto sample = RandX::RandSample(arr, std::size_t{3});
        CHECK(sample.size() == 3);
        for (int x : sample)
        {
            CHECK((x == 10 || x == 20 || x == 30 || x == 40 || x == 50));
        }
    }

    // ============================================================
    // 扩展分布：Bernoulli/Binomial/LogNormal/Geometric/
    // Cauchy/Weibull/ExtremeValue/ChiSquared/StudentT/FisherF/Beta
    // ============================================================
    TEST_CASE("RandBernoulli 与 RandBool 引擎重载等价")
    {
        Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandBernoulli(rng1, 0.3) == RandBool(rng2, 0.3));
    }

    TEST_CASE("RandBinomial 范围与均值")
    {
        // t=60, p=0.5：理论均值 30，方差 15
        double sum = 0;
        for (int i = 0; i < MC_TRIALS_10K; ++i)
        {
            int v = RandBinomial(60, 0.5);
            CHECK(v >= 0);
            CHECK(v <= 60);
            sum += v;
        }
        const double mean = sum / MC_TRIALS_10K;
        CHECK(mean > 25.0);
        CHECK(mean < 35.0);
    }

    TEST_CASE("RandLogNormal 正数")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandLogNormal(0.0, 1.0) > 0.0);
    }

    TEST_CASE("RandGeometric 非负")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandGeometric(0.3) >= 0);
    }

    TEST_CASE("RandGeometric 边界参数 p=1 稳定返回零且不触发断言")
    {
        RandX::Xoshiro256StarStar rng{ 42 };
        CHECK(RandX::RandGeometric(1.0) == 0);
        CHECK(RandX::RandGeometric(rng, 1.0) == 0);
        CHECK(RandX::RandGeometric<std::uint32_t>(1.0) == 0U);
        CHECK_THROWS_AS((void)RandX::RandGeometric(0.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandGeometric(1.5), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandGeometric<int>(1e-15), std::invalid_argument);
    }

    TEST_CASE("RandCauchy 有限值占绝大多数")
    {
        // Cauchy 重尾，理论上 P(|x|<1e6)≈0.99968，inf 极罕见
        int finiteCount = 0;
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            const double v = RandCauchy(0.0, 1.0);
            if (std::isfinite(v)) ++finiteCount;
        }
        CHECK(finiteCount >= MC_TRIALS_100 - 5);
    }

    TEST_CASE("RandWeibull 非负")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandWeibull(2.0, 1.0) >= 0.0);
    }

    TEST_CASE("RandExtremeValue 有限值")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            const double v = RandExtremeValue(0.0, 1.0);
            CHECK(std::isfinite(v));
        }
    }

    TEST_CASE("RandChiSquared 非负")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandChiSquared(4.0) >= 0.0);
    }

    TEST_CASE("RandStudentT 有限值占绝大多数")
    {
        // 自由度 4：有限方差但重尾
        int finiteCount = 0;
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            const double v = RandStudentT(4.0);
            if (std::isfinite(v)) ++finiteCount;
        }
        CHECK(finiteCount >= MC_TRIALS_100 - 5);
    }

    TEST_CASE("RandFisherF 非负")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandFisherF(5.0, 5.0) >= 0.0);
    }

    TEST_CASE("RandBeta 范围 [0,1] 与均值")
    {
        // a=2, b=2：理论均值 0.5，方差 0.05
        double sum = 0;
        for (int i = 0; i < MC_TRIALS_10K; ++i)
        {
            const double v = RandBeta(2.0, 2.0);
            CHECK(v >= 0.0);
            CHECK(v <= 1.0);
            sum += v;
        }
        const double mean = sum / MC_TRIALS_10K;
        CHECK(mean > 0.4);
        CHECK(mean < 0.6);
    }

    TEST_CASE("新分布引擎重载确定性")
    {
        Xoshiro256StarStar rng1{ 12345 }, rng2{ 12345 };
        CHECK(RandBinomial(rng1, 10, 0.5) == RandBinomial(rng2, 10, 0.5));
        CHECK(RandLogNormal(rng1, 0.0, 1.0) == RandLogNormal(rng2, 0.0, 1.0));
        CHECK(RandGeometric(rng1, 0.3) == RandGeometric(rng2, 0.3));
        CHECK(RandCauchy(rng1, 0.0, 1.0) == RandCauchy(rng2, 0.0, 1.0));
        CHECK(RandWeibull(rng1, 2.0, 1.0) == RandWeibull(rng2, 2.0, 1.0));
        CHECK(RandExtremeValue(rng1, 0.0, 1.0) == RandExtremeValue(rng2, 0.0, 1.0));
        CHECK(RandChiSquared(rng1, 4.0) == RandChiSquared(rng2, 4.0));
        CHECK(RandStudentT(rng1, 4.0) == RandStudentT(rng2, 4.0));
        CHECK(RandFisherF(rng1, 5.0, 5.0) == RandFisherF(rng2, 5.0, 5.0));
        CHECK(RandBeta(rng1, 2.0, 2.0) == RandBeta(rng2, 2.0, 2.0));
    }
}

// ============================================================================
// 新增 API（C++17 SFINAE）：RandChar / RandElement 迭代器版 / RandFill / RandVector / operator<<>> 
// ============================================================================
TEST_SUITE("新增 API")
{
    TEST_CASE("RandChar 范围与类型")
    {
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char c = RandChar('a', 'z');
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            wchar_t wc = RandChar<wchar_t>(L'a', L'z');
            CHECK(wc >= L'a');
            CHECK(wc <= L'z');
        }
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char16_t c16 = RandChar<char16_t>(u'a', u'z');
            CHECK(c16 >= u'a');
            CHECK(c16 <= u'z');
        }
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char32_t c32 = RandChar<char32_t>(U'a', U'z');
            CHECK(c32 >= U'a');
            CHECK(c32 <= U'z');
        }
        // RandChar(max) 单参版
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char c = RandChar<char>('z');
            CHECK(c >= char{});
            CHECK(c <= 'z');
        }
    }

#if defined(__cpp_char8_t) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    TEST_CASE("RandChar char8_t（C++20+ 条件启用）")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char8_t c = RandChar<char8_t>(u8'a', u8'z');
            CHECK(c >= u8'a');
            CHECK(c <= u8'z');
        }
    }
#endif

    TEST_CASE("RandChar 引擎重载")
    {
        Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char c = RandChar<char>(rng, 'a', 'z');
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char c = RandChar<char>(rng, 'z');
            CHECK(c >= char{});
            CHECK(c <= 'z');
        }
    }

    TEST_CASE("RandElement 迭代器版（随机访问）")
    {
        std::vector<int> v = { 10, 20, 30, 40, 50 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            auto it = RandElement(v.begin(), v.end());
            CHECK(it >= v.begin());
            CHECK(it < v.end());
            CHECK(*it >= 10);
            CHECK(*it <= 50);
        }
        // 引擎重载
        Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            auto it = RandElement(rng, v.begin(), v.end());
            CHECK(it >= v.begin());
            CHECK(it < v.end());
        }
    }

    TEST_CASE("RandElement 迭代器版（输入迭代器 reservoir sampling）")
    {
        std::list<int> l = { 100, 200, 300, 400, 500 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            auto val = RandElement(l.begin(), l.end());
            CHECK(val >= 100);
            CHECK(val <= 500);
        }
        // 引擎重载
        Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            auto val = RandElement(rng, l.begin(), l.end());
            CHECK(val >= 100);
            CHECK(val <= 500);
        }
    }

    TEST_CASE("RandElement 空范围抛异常")
    {
        std::vector<int> empty;
        CHECK_THROWS_AS((void)RandElement(empty.begin(), empty.end()), std::invalid_argument);
    }

    TEST_CASE("RandFill 填充范围")
    {
        std::array<int, 100> arr{};
        RandFill(arr.begin(), arr.end(), 0, 99);
        for (const auto& v : arr)
        {
            CHECK(v >= 0);
            CHECK(v <= 99);
        }
        // 浮点版
        std::vector<double> v(100);
        RandFill(v.begin(), v.end(), 0.0, 1.0);
        for (const auto& d : v)
        {
            CHECK(d >= 0.0);
            CHECK(d < 1.0);
        }
        // 引擎重载
        Xoshiro256StarStar rng{ 12345 };
        std::vector<int> v2(100);
        RandFill(rng, v2.begin(), v2.end(), 1, 10);
        for (const auto& i : v2)
        {
            CHECK(i >= 1);
            CHECK(i <= 10);
        }
    }

    TEST_CASE("RandVector 生成 vector")
    {
        auto vi = RandVector(0, 99, 100);
        CHECK(vi.size() == 100);
        for (const auto& v : vi)
        {
            CHECK(v >= 0);
            CHECK(v <= 99);
        }
        auto vd = RandVector(0.0, 1.0, 100);
        CHECK(vd.size() == 100);
        for (const auto& v : vd)
        {
            CHECK(v >= 0.0);
            CHECK(v < 1.0);
        }
        // 引擎重载
        Xoshiro256StarStar rng{ 12345 };
        auto vi2 = RandVector(rng, 1, 6, 50);
        CHECK(vi2.size() == 50);
        for (const auto& v : vi2)
        {
            CHECK(v >= 1);
            CHECK(v <= 6);
        }
        auto vd2 = RandVector(rng, 0.0, 10.0, 50);
        CHECK(vd2.size() == 50);
        for (const auto& v : vd2)
        {
            CHECK(v >= 0.0);
            CHECK(v < 10.0);
        }
    }

    TEST_CASE("operator<< / operator>> 流式序列化")
    {
        Xoshiro256StarStar rng1{ 12345 };
        // 推进若干步
        for (int i = 0; i < 10; ++i) (void)rng1();

        std::stringstream ss;
        ss << rng1;
        CHECK((ss.good() || ss.eof()));

        Xoshiro256StarStar rng2{ 99999 };
        ss >> rng2;
        CHECK((ss.good() || ss.eof()));

        // 恢复后两个引擎状态一致，后续输出相同
        CHECK(rng1 == rng2);
        CHECK(rng1() == rng2());
    }

    TEST_CASE("operator>> 失败时设置 failbit 且状态不变")
    {
        Xoshiro256StarStar rng1{ 12345 };
        Xoshiro256StarStar rng2{ 12345 };
        // 推进若干步，使状态非默认
        for (int i = 0; i < 5; ++i) (void)rng1(), (void)rng2();

        std::stringstream ss("not a number");  // 故意错误输入
        ss >> rng1;
        CHECK((ss.failbit & ss.rdstate()) != 0);
        // 状态未改变
        CHECK(rng1 == rng2);
    }

    TEST_CASE("operator<< 约束标量状态类型 SplitMix64")
    {
        // 此测试主要保证编译期 SFINAE 排除标量引擎
        static_assert(!detail::is_serializable_engine_v<SplitMix64>,
            "SplitMix64 must not be serializable (scalar state_type)");
        static_assert(detail::is_serializable_engine_v<Xoshiro256StarStar>,
            "Xoshiro256StarStar must be serializable");
    }
}

// ============================================================================
// RandSample 迭代器版
// ============================================================================
TEST_SUITE("RandSample 迭代器版")
{
    TEST_CASE("随机访问路径（索引分支）：n·64>=size 走索引数组")
    {
        std::vector<int> v = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        // n=5, size=10, n·64=320 >= 10 → 索引数组分支
        auto sample = RandSample(v.begin(), v.end(), 5);
        CHECK(sample.size() == 5);
        for (int x : sample)
        {
            CHECK(x >= 0);
            CHECK(x <= 9);
            CHECK(std::count(sample.begin(), sample.end(), x) == 1);
        }
    }

    TEST_CASE("随机访问路径（hash-set 分支）：n·64<size 走 hash-set")
    {
        std::vector<int> v(10000);
        for (int i = 0; i < 10000; ++i) v[static_cast<std::size_t>(i)] = i;
        // n=5, size=10000, n·64=320 < 10000 → hash-set 分支
        auto sample = RandSample(v.begin(), v.end(), 5);
        CHECK(sample.size() == 5);
        for (int x : sample)
        {
            CHECK(x >= 0);
            CHECK(x <= 9999);
            CHECK(std::count(sample.begin(), sample.end(), x) == 1);
        }
    }

    TEST_CASE("输入路径：std::list 走 reservoir sampling")
    {
        std::list<int> lst = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
        auto sample = RandSample(lst.begin(), lst.end(), 3);
        CHECK(sample.size() == 3);
        for (int x : sample)
        {
            bool found = false;
            for (int e : lst) if (e == x) { found = true; break; }
            CHECK(found);
        }
        std::sort(sample.begin(), sample.end());
        for (std::size_t i = 1; i < sample.size(); ++i)
            CHECK(sample[i] != sample[i - 1]);
    }

    TEST_CASE("流式输入：std::istream_iterator 走 reservoir")
    {
        std::istringstream iss("1 2 3 4 5 6 7 8 9 10");
        std::istream_iterator<int> first(iss);
        std::istream_iterator<int> last;
        auto sample = RandSample(first, last, 3);
        CHECK(sample.size() == 3);
        for (int x : sample)
        {
            CHECK(x >= 1);
            CHECK(x <= 10);
        }
        std::sort(sample.begin(), sample.end());
        for (std::size_t i = 1; i < sample.size(); ++i)
            CHECK(sample[i] != sample[i - 1]);
    }

    TEST_CASE("边界：n=0 返回空")
    {
        std::vector<int> v = { 1, 2, 3, 4, 5 };
        auto s1 = RandSample(v.begin(), v.end(), 0);
        CHECK(s1.empty());
        std::list<int> lst = { 1, 2, 3 };
        auto s2 = RandSample(lst.begin(), lst.end(), 0);
        CHECK(s2.empty());
    }

    TEST_CASE("边界：n>=size 返回全部")
    {
        std::vector<int> v = { 1, 2, 3, 4, 5 };
        auto s1 = RandSample(v.begin(), v.end(), 5);
        CHECK(s1.size() == 5);
        auto s2 = RandSample(v.begin(), v.end(), 10);
        CHECK(s2.size() == 5);
        std::list<int> lst = { 10, 20, 30 };
        auto s3 = RandSample(lst.begin(), lst.end(), 3);
        CHECK(s3.size() == 3);
        auto s4 = RandSample(lst.begin(), lst.end(), 10);
        CHECK(s4.size() == 3);
    }

    TEST_CASE("边界：空范围返回空")
    {
        std::vector<int> empty;
        auto s1 = RandSample(empty.begin(), empty.end(), 3);
        CHECK(s1.empty());
        std::list<int> emptyLst;
        auto s2 = RandSample(emptyLst.begin(), emptyLst.end(), 3);
        CHECK(s2.empty());
    }

    TEST_CASE("引擎重载确定性（随机访问）")
    {
        std::vector<int> v(1000);
        for (int i = 0; i < 1000; ++i) v[static_cast<std::size_t>(i)] = i;
        Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        auto s1 = RandSample(rng1, v.begin(), v.end(), 10);
        auto s2 = RandSample(rng2, v.begin(), v.end(), 10);
        CHECK(s1 == s2);
    }

    TEST_CASE("引擎重载确定性（输入迭代器 reservoir）")
    {
        std::list<int> lst = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19 };
        Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        auto s1 = RandSample(rng1, lst.begin(), lst.end(), 5);
        auto s2 = RandSample(rng2, lst.begin(), lst.end(), 5);
        CHECK(s1 == s2);
    }

    TEST_CASE("均匀性（reservoir）：前后半段均衡分布")
    {
        // N=200, n=10, TRIALS=10000；期望各半段被选 50000 次
        // 水塘抽样算法在完整遍历时各段均匀分布
        constexpr int N = 200;
        constexpr int n = 10;
        constexpr int TRIALS = 10000;
        std::list<int> v;
        for (int i = 0; i < N; ++i) v.push_back(i);
        int firstHalf = 0, secondHalf = 0;
        for (int t = 0; t < TRIALS; ++t)
        {
            auto s = RandSample(v.begin(), v.end(), n);
            for (int x : s)
            {
                if (x < N / 2) ++firstHalf;
                else ++secondHalf;
            }
        }
        // ±10% 容差，统计波动范围检查
        CHECK(firstHalf > 45000);
        CHECK(firstHalf < 55000);
        CHECK(secondHalf > 45000);
        CHECK(secondHalf < 55000);
    }

    TEST_CASE("均匀性（hash-set）：前后半段均衡")
    {
        // n·64 < size 强制走 hash-set 分支：N=10000, n=5, n·64=320<10000
        constexpr int N = 10000;
        constexpr int n = 5;
        constexpr int TRIALS = 10000;
        std::vector<int> v(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i) v[static_cast<std::size_t>(i)] = i;
        int firstHalf = 0, secondHalf = 0;
        for (int t = 0; t < TRIALS; ++t)
        {
            auto s = RandSample(v.begin(), v.end(), n);
            for (int x : s)
            {
                if (x < N / 2) ++firstHalf;
                else ++secondHalf;
            }
        }
        // 期望各 25000，±15% 容差
        CHECK(firstHalf > 21250);
        CHECK(firstHalf < 28750);
        CHECK(secondHalf > 21250);
        CHECK(secondHalf < 28750);
    }
}

// ============================================================================
// 统计自检（Chi-Square 频率检验）
// ============================================================================
TEST_SUITE("Chi-Square 频率检验")
{
    TEST_CASE("Xoshiro256StarStar 均匀性 df=99")
    {
        constexpr double EXPECTED = static_cast<double>(STAT_N) / STAT_BINS_100;

        Xoshiro256StarStar rng{ 98765 };
        std::array<int, STAT_BINS_100> counts{};
        for (int i = 0; i < STAT_N; ++i)
            ++counts[RandInt(rng, 0, STAT_BINS_100 - 1)];

        double chi2 = 0.0;
        for (int i = 0; i < STAT_BINS_100; ++i)
        {
            const double diff = counts[i] - EXPECTED;
            chi2 += diff * diff / EXPECTED;
        }
        CHECK(chi2 < CHI2_CRITICAL_DF99);
    }

    TEST_CASE("SFC64 原始输出均匀性 df=127")
    {
        constexpr double EXPECTED = static_cast<double>(STAT_N) / STAT_BINS_128;

        SFC64 rng{ 54321 };
        std::array<int, STAT_BINS_128> counts{};
        for (int i = 0; i < STAT_N; ++i)
            ++counts[rng() & 127];

        double chi2 = 0.0;
        for (int i = 0; i < STAT_BINS_128; ++i)
        {
            const double diff = counts[i] - EXPECTED;
            chi2 += diff * diff / EXPECTED;
        }
        CHECK(chi2 < CHI2_CRITICAL_DF127);
    }

    TEST_CASE("RomuDuoJr 原始输出均匀性 df=127")
    {
        constexpr double EXPECTED = static_cast<double>(STAT_N) / STAT_BINS_128;

        RomuDuoJr rng{ 54321 };
        std::array<int, STAT_BINS_128> counts{};
        for (int i = 0; i < STAT_N; ++i)
            ++counts[rng() & 127];

        double chi2 = 0.0;
        for (int i = 0; i < STAT_BINS_128; ++i)
        {
            const double diff = counts[i] - EXPECTED;
            chi2 += diff * diff / EXPECTED;
        }
        CHECK(chi2 < CHI2_CRITICAL_DF127);
    }
}

// ============================================================================
// RandChar / RandString 预设字符集
// ============================================================================
TEST_SUITE("RandChar 预设字符集")
{
    TEST_CASE("RandChar(CharSet::Lower) 全部属于 [a-z]")
    {
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char c = RandChar(CharSet::Lower);
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }
    }

    TEST_CASE("RandChar(CharSet::Hex) 全部属于 [0-9a-f]")
    {
        const std::string hexSet = "0123456789abcdef";
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char c = RandChar(CharSet::Hex);
            CHECK(hexSet.find(c) != std::string::npos);
        }
    }

    TEST_CASE("RandChar 9 种字符集全覆盖")
    {
        using CS = CharSet;
        const std::string alnum = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        const std::string alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        const std::string lower = "abcdefghijklmnopqrstuvwxyz";
        const std::string upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const std::string digit = "0123456789";
        const std::string hexS = "0123456789abcdef";
        const std::string printable = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const std::string base64UrlSafe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            CHECK(alnum.find(RandChar(CS::Alphanumeric)) != std::string::npos);
            CHECK(alpha.find(RandChar(CS::Alpha)) != std::string::npos);
            CHECK(lower.find(RandChar(CS::Lower)) != std::string::npos);
            CHECK(upper.find(RandChar(CS::Upper)) != std::string::npos);
            CHECK(digit.find(RandChar(CS::Digit)) != std::string::npos);
            CHECK(hexS.find(RandChar(CS::Hex)) != std::string::npos);
            CHECK(printable.find(RandChar(CS::Printable)) != std::string::npos);
            CHECK(base64.find(RandChar(CS::Base64)) != std::string::npos);
            CHECK(base64UrlSafe.find(RandChar(CS::Base64UrlSafe)) != std::string::npos);
        }
    }

    TEST_CASE("RandChar 引擎重载确定性")
    {
        Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            CHECK(RandChar(rng1, CharSet::Hex) == RandChar(rng2, CharSet::Hex));
        }
    }

    TEST_CASE("RandChar 支持异构自定义引擎重载")
    {
        // 验证 RandChar(Engine&, CharSet) 对 SFC64 / RomuDuoJr 等任意引擎均可编译且确定性成立
        SFC64 sfc1{ 42 }, sfc2{ 42 };
        RomuDuoJr romu1{ 42 }, romu2{ 42 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            CHECK(RandChar(sfc1, CharSet::Hex) == RandChar(sfc2, CharSet::Hex));
            CHECK(RandChar(romu1, CharSet::Lower) == RandChar(romu2, CharSet::Lower));
        }
    }

    TEST_CASE("RandString 引擎重载确定性与字符集")
    {
        // 验证 RandString(Engine&, n, CharSet) 引擎重载
        Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        const std::string hexSet = "0123456789abcdef";
        auto s1 = RandString(rng1, 32, CharSet::Hex);
        auto s2 = RandString(rng2, 32, CharSet::Hex);
        CHECK(s1.size() == 32);
        CHECK(s1 == s2);
        for (char c : s1)
            CHECK(hexSet.find(c) != std::string::npos);

        // 非默认引擎也应可工作
        SFC64 sfc{ 7 };
        auto s3 = RandString(sfc, 16, CharSet::Base64);
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        CHECK(s3.size() == 16);
        for (char c : s3)
            CHECK(base64.find(c) != std::string::npos);
    }

    TEST_CASE("RandString(n, CharSet::Digit) 全部属于 [0-9]")
    {
        const std::string digit = "0123456789";
        auto s = RandString(100, CharSet::Digit);
        CHECK(s.size() == 100);
        for (char c : s)
            CHECK(digit.find(c) != std::string::npos);
    }

    TEST_CASE("RandString(16, CharSet::Hex) 长度与字符集")
    {
        const std::string hexSet = "0123456789abcdef";
        auto s = RandString(16, CharSet::Hex);
        CHECK(s.size() == 16);
        for (char c : s)
            CHECK(hexSet.find(c) != std::string::npos);
    }

    TEST_CASE("RandString(12, CharSet::Base64) RFC 4648 合规")
    {
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        auto s = RandString(12, CharSet::Base64);
        CHECK(s.size() == 12);
        for (char c : s)
            CHECK(base64.find(c) != std::string::npos);
    }

    TEST_CASE("RandChar(CharSet::Base64UrlSafe) 全部属于 URL-safe 字母表")
    {
        const std::string urlSafeSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char c = RandChar(CharSet::Base64UrlSafe);
            CHECK(urlSafeSet.find(c) != std::string::npos);
        }
    }

    TEST_CASE("RandString(12, CharSet::Base64UrlSafe) RFC 4648 §5 合规")
    {
        const std::string urlSafeSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        auto s = RandString(12, CharSet::Base64UrlSafe);
        CHECK(s.size() == 12);
        for (char c : s)
            CHECK(urlSafeSet.find(c) != std::string::npos);
    }

    TEST_CASE("Base64 vs Base64UrlSafe 仅末两字符不同（引擎重载同种子）")
    {
        // 引擎重载下 uniform_int_distribution(0,63) 产生相同索引序列，
        // 仅 index=62（Base64:'+' / UrlSafe:'-'）与 index=63（Base64:'/' / UrlSafe:'_'）处字符不同
        Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const std::string urlSafe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char c1 = RandChar(rng1, CharSet::Base64);
            char c2 = RandChar(rng2, CharSet::Base64UrlSafe);
            // 同索引：若 index<62 则字符相同，index=62/63 则不同
            if (c1 == c2)
            {
                CHECK(base64.find(c1) < 62);
            }
            else
            {
                // 差异仅允许在 +→- 与 /→_（doctest 禁止 || 于 CHECK 宏内，先求值 bool）
                const bool validDiff = (c1 == '+' && c2 == '-') || (c1 == '/' && c2 == '_');
                CHECK(validDiff);
            }
        }
    }

    TEST_CASE("RandChar(CharSet::Hex) 均匀性 ±3σ")
    {
        // Hex 字符集 = "0123456789abcdef" 共 16 个字符（标准小写 hex）
        // 各字符期望频率 1/16，N=22000 采样，每字符期望 1375 次
        // σ = sqrt(N * p * (1-p)) = sqrt(22000 * 1/16 * 15/16) ≈ 35.90
        const std::string hexSet = "0123456789abcdef";
        const std::size_t cat = hexSet.size();
        constexpr int N = 22000;
        const int expected = N / static_cast<int>(cat);
        const double p = 1.0 / static_cast<double>(cat);
        const double sigma = std::sqrt(static_cast<double>(N) * p * (1.0 - p));
        std::vector<int> counts(cat, 0);
        RandX::Xoshiro256StarStar rng{ 0xC0FFEE };
        for (int i = 0; i < N; ++i)
        {
            char c = RandChar(rng, CharSet::Hex);
            auto idx = hexSet.find(c);
            CHECK(idx != std::string::npos);
            ++counts[idx];
        }
        for (std::size_t i = 0; i < cat; ++i)
        {
            const double dev = std::abs(counts[i] - expected);
            CHECK(dev < 3.0 * sigma);
        }
    }
}

// ============================================================================
// ChaCha20 CSPRNG（RFC 8439 §2.3.2 KAT + URBG 基础 + 构造异常 + 统计检验）
// ============================================================================
TEST_SUITE("ChaCha20 CSPRNG")
{
    TEST_CASE("RFC 8439 §2.3.2 KAT（counter=1，前 8 个 uint64 输出）")
    {
        // RFC 8439 §2.3.2 官方测试向量
        // key   = 00:01:02:...:1f（32 字节）
        // nonce = 00:00:00:09:00:00:00:4a:00:00:00:00（12 字节）
        // counter = 1
        // 一个 block = 64 字节 = 8 个 uint64（小端序组装）
        const std::uint8_t key[32] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
        };
        const std::uint8_t nonce[12] = {
            0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00
        };
        ChaCha20 rng(key, 32, nonce, 12, 1);

        // RFC 8439 §2.3.2 输出字节流（64 字节，按小端序组装为 uint64）
        // 经 pycryptodome 交叉验证，与 RFC 8439 官方测试向量一致
        const std::uint64_t expected[8] = {
            0x15593bd1e4e7f110ULL,  // 10 f1 e7 e4 d1 3b 59 15
            0xc47120a31fdd0f50ULL,  // 50 0f dd 1f a3 20 71 c4
            0x0368c033c7f4d1c7ULL,  // c7 d1 f4 c7 33 c0 68 03
            0x4e6cd4c39aaa2204ULL,  // 04 22 aa 9a c3 d4 6c 4e
            0x09aa9f07466482d2ULL,  // d2 82 64 46 07 9f aa 09
            0xa2028bd905d7c214ULL,  // 14 c2 d7 05 d9 8b 02 a2
            0xb94e16ded19c12b5ULL,  // b5 12 9c d1 de 16 4e b9
            0x4e3c50a2e883d0cbULL   // cb d0 83 e8 a2 50 3c 4e
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("min/max 与 result_type")
    {
        static_assert(std::is_same_v<ChaCha20::result_type, std::uint64_t>);
        CHECK(ChaCha20::min() == 0);
        CHECK(ChaCha20::max() == UINT64_MAX);
    }

    TEST_CASE("构造方式 2 确定性复现")
    {
        ChaCha20 a{ 42 };
        ChaCha20 b{ 42 };
        for (int i = 0; i < 20; ++i)
            CHECK(a() == b());
    }

    TEST_CASE("构造方式 3 异常：key 长度非法")
    {
        const std::uint8_t badKey[16] = { 0 };
        const std::uint8_t nonce[12] = { 0 };
        CHECK_THROWS_AS(ChaCha20(badKey, 16, nonce, 12), std::invalid_argument);
    }

    TEST_CASE("构造方式 3 异常：nonce 长度非法")
    {
        const std::uint8_t key[32] = { 0 };
        const std::uint8_t badNonce[8] = { 0 };
        CHECK_THROWS_AS(ChaCha20(key, 32, badNonce, 8), std::invalid_argument);
    }

    TEST_CASE("discard 与连续调用等价")
    {
        ChaCha20 a{ 999 };
        ChaCha20 b{ 999 };
        a.discard(100);
        for (int i = 0; i < 100; ++i) b();
        CHECK(a() == b());
    }

    TEST_CASE("不同种子产生不同序列")
    {
        ChaCha20 a{ 1 };
        ChaCha20 b{ 2 };
        bool allSame = true;
        for (int i = 0; i < 10; ++i)
        {
            if (a() != b()) allSame = false;
        }
        CHECK_FALSE(allSame);
    }

    TEST_CASE("原始输出均匀性 df=127")
    {
        // 使用 CHACHA20_CHI2_N（非 STAT_N）以避免触发自动 reseed，保持确定性
        constexpr double EXPECTED = static_cast<double>(CHACHA20_CHI2_N) / STAT_BINS_128;

        ChaCha20 rng{ 54321 };
        std::array<int, STAT_BINS_128> counts{};
        for (int i = 0; i < CHACHA20_CHI2_N; ++i)
            ++counts[rng() & 127];

        double chi2 = 0.0;
        for (int i = 0; i < STAT_BINS_128; ++i)
        {
            const double diff = counts[i] - EXPECTED;
            chi2 += diff * diff / EXPECTED;
        }
        CHECK(chi2 < CHI2_CRITICAL_DF127);
    }

    // ── 设计文档 §5.1 要求：字节缓存 / 自动 reseed / 手动 reseed() ──

    TEST_CASE("字节缓存：前 8 次同一 block，第 9 次触发新 block")
    {
        // 一个 block = 64 字节 = 8 个 uint64
        // 实例 A(counter=0) 的第 9 次输出 = 实例 B(counter=1) 的第 1 次输出
        const std::uint8_t key[32] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
        };
        const std::uint8_t nonce[12] = {
            0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00
        };
        ChaCha20 a(key, 32, nonce, 12, 0);
        // 消费前 8 个 uint64（block 0）
        for (int i = 0; i < 8; ++i) (void)a();
        // 第 9 次输出 = block 1 的第 1 个 uint64
        const std::uint64_t ninth = a();
        // 实例 B 从 counter=1 开始，第 1 次输出应等于 A 的第 9 次
        ChaCha20 b(key, 32, nonce, 12, 1);
        CHECK(ninth == b());
    }

    TEST_CASE("默认构造函数在 2^20 字节阈值后自动 reseed")
    {
        if (!IsOsCryptoEntropyAvailable()) return;
        // 默认构造函数启用 m_autoReseed = true
        // ChaCha20ReseedThreshold = 2^20 字节，每次 operator() 输出 8 字节
        // 注意：默认构造由 OS 熵播种，无法固定种子做双实例对比。
        // 本用例验证：① 越过阈值不崩溃 ② reseed 后输出非退化
        constexpr unsigned long long ReseedCalls = (1ULL << 20) / 8;
        ChaCha20 rng;
        // 越过 reseed 阈值
        rng.discard(ReseedCalls + 8);
        // reseed 后继续产生有效输出
        bool allZero = true;
        bool hasRepeat = true;
        std::uint64_t prev = rng();
        if (prev != 0) allZero = false;
        for (int i = 0; i < 16; ++i)
        {
            std::uint64_t cur = rng();
            if (cur != 0) allZero = false;
            if (cur != prev) hasRepeat = false;
            prev = cur;
        }
        CHECK_FALSE(allZero);
        CHECK_FALSE(hasRepeat);
    }

    TEST_CASE("确定性种子 ChaCha20(seed) 在超过 1MB 后依然保持完全确定性")
    {
        constexpr unsigned long long ReseedCalls = (1ULL << 20) / 8;
        ChaCha20 a{ 42 };
        ChaCha20 b{ 42 };
        a.discard(ReseedCalls + 16);
        b.discard(ReseedCalls + 16);
        for (int i = 0; i < 16; ++i)
        {
            CHECK(a() == b());
        }
    }

    TEST_CASE("reseed() 手动触发后输出序列改变")
    {
        if (!IsOsCryptoEntropyAvailable()) return;
        ChaCha20 a{ 42 };
        ChaCha20 b{ 42 };
        // reseed 前：a 和 b 输出一致（同种子确定性）
        for (int i = 0; i < 8; ++i) CHECK(a() == b());
        // b 手动 reseed（引入 OS 熵）
        b.reseed();
        // reseed 后：b 输出与 a 不同（概率性断言）
        bool different = false;
        for (int i = 0; i < 16; ++i)
        {
            if (a() != b()) { different = true; break; }
        }
        CHECK(different);
    }
}

// ============================================================================
// 密码学安全熵源（SecureRandomBytes / SecureSeed / IsOsCryptoEntropyAvailable）
// ============================================================================
TEST_SUITE("密码学安全熵源")
{
    TEST_CASE("IsOsCryptoEntropyAvailable 返回 bool")
    {
        // 仅验证返回类型与可调用性，不假设具体值（跨平台差异）
        const bool available = IsOsCryptoEntropyAvailable();
        (void)available;
    }

    TEST_CASE("SecureRandomBytes 填充缓冲区")
    {
        std::array<std::uint8_t, 64> buf{};
        SecureRandomBytes(buf.data(), buf.size());
        // 极低概率全零（2^-512），视为熵源异常
        bool allZero = true;
        for (auto b : buf) { if (b != 0) { allZero = false; break; } }
        CHECK_FALSE(allZero);
    }

    TEST_CASE("SecureRandomBytes 零长度调用安全完成")
    {
        SecureRandomBytes(nullptr, 0);
    }

    TEST_CASE("SecureSeed 返回不同值")
    {
        const auto a = SecureSeed();
        const auto b = SecureSeed();
        // 极低概率相同（2^-64）
        CHECK(a != b);
    }

    TEST_CASE("ChaCha20 默认构造成功初始化（OS 熵可用时）")
    {
        if (IsOsCryptoEntropyAvailable())
        {
            ChaCha20 rng;
            (void)rng();
        }
    }

    TEST_CASE("ChaCha20 默认构造两个实例序列不同")
    {
        if (IsOsCryptoEntropyAvailable())
        {
            ChaCha20 a;
            ChaCha20 b;
            bool allSame = true;
            for (int i = 0; i < 10; ++i)
            {
                if (a() != b()) allSame = false;
            }
            CHECK_FALSE(allSame);
        }
    }
}

TEST_SUITE("CSPRNG 对象生命周期与移动语义 (C++17)")
{
    TEST_CASE("ChaCha20 移动语义与拷贝禁用")
    {
        static_assert(!std::is_copy_constructible_v<RandX::ChaCha20>);
        static_assert(!std::is_copy_assignable_v<RandX::ChaCha20>);
        static_assert(std::is_move_constructible_v<RandX::ChaCha20>);
        static_assert(std::is_move_assignable_v<RandX::ChaCha20>);

        RandX::ChaCha20 rng1(12345ULL);
        RandX::ChaCha20 rng2 = std::move(rng1);
        (void)rng2();
    }
}

TEST_SUITE("数学分布与极端参数边界 (C++17)")
{
    TEST_CASE("RandBeta 极端非正规数参数保持正值输出")
    {
        RandX::Xoshiro256StarStar rng(123);
        double betaVal = RandX::RandBeta(rng, 1.0, 1e-300);
        CHECK(betaVal > 0.0);
    }
}

TEST_SUITE("容器与原生数组操作契约 (C++17)")
{
    TEST_CASE("RandElement 支持原生 C 风格数组")
    {
        int arr[5] = { 10, 20, 30, 40, 50 };
        int val = RandX::RandElement(arr);
        CHECK((val >= 10 && val <= 50));
    }
}

TEST_SUITE("API 契约与生命周期保障 (C++17)")
{
    TEST_CASE("RandUUID 格式正确与随机性")
    {
        std::string u1 = RandX::RandUUID();
        std::string u2 = RandX::RandUUID();
        CHECK(u1.length() == 36);
        CHECK(u1[8] == '-');
        CHECK(u1[13] == '-');
        CHECK(u1[14] == '4');
        CHECK(u1[18] == '-');
        CHECK((u1[19] == '8' || u1[19] == '9' || u1[19] == 'a' || u1[19] == 'b'));
        CHECK(u1[23] == '-');
        CHECK(u1 != u2);
    }

    TEST_CASE("RandUUID 在 32 位引擎下保持完整高位熵")
    {
        RandX::Xoshiro128StarStar rng32{ 12345 };
        std::string u = RandX::RandUUID(rng32);
        CHECK(u.length() == 36);
        CHECK(u.substr(0, 8) != "00000000");
        CHECK(u.substr(24, 8) != "00000000");
    }

    TEST_CASE("RandPoisson 期望为 0 时合法返回 0")
    {
        CHECK(RandX::RandPoisson(0.0) == 0);
        CHECK_THROWS_AS((void)RandX::RandPoisson<int>(1e15), std::invalid_argument);
    }

    TEST_CASE("RandWeighted 引擎重载与分布对象高频复用 C++17")
    {
        RandX::Xoshiro256StarStar rng{ 42 };
        std::vector<double> weights = { 10.0, 0.0, 0.0 };
        CHECK(RandX::RandWeighted(rng, weights) == 0);

        std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
        CHECK(RandX::RandWeighted(dist) == 0);
        CHECK(RandX::RandWeighted(rng, dist) == 0);
    }

}

TEST_SUITE("边界用例")
{
    TEST_CASE("RandInt(min, min) 始终返回 min")
    {
        RandX::Reseed(42);
        for (int i = 0; i < 100; ++i)
            CHECK(RandX::RandInt(7, 7) == 7);
        for (int i = 0; i < 100; ++i)
            CHECK(RandX::RandInt(-3, -3) == -3);
    }

    TEST_CASE("RandReal(min, min) 始终返回 min")
    {
        RandX::Reseed(42);
        for (int i = 0; i < 100; ++i)
            CHECK(RandX::RandReal(2.5, 2.5) == 2.5);
    }

    TEST_CASE("RandCanonical 直通 Bit-Extraction 浮点生成")
    {
        RandX::Reseed(12345);

        double d_sum = 0.0;
        constexpr int N = 10000;
        for (int i = 0; i < N; ++i)
        {
            double v = RandX::RandCanonicalDouble();
            CHECK((v >= 0.0 && v < 1.0));
            d_sum += v;
        }
        double d_mean = d_sum / N;
        CHECK((d_mean >= 0.45 && d_mean <= 0.55));

        float f_sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            float v = RandX::RandCanonicalFloat();
            CHECK((v >= 0.0f && v < 1.0f));
            f_sum += v;
        }
        float f_mean = f_sum / N;
        CHECK((f_mean >= 0.45f && f_mean <= 0.55f));

        // 32 位引擎专向兼容测试 (Xoshiro128StarStar)
        RandX::Xoshiro128StarStar rng32{ 54321 };
        double d32_sum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            double v = RandX::RandCanonical<double>(rng32);
            CHECK((v >= 0.0 && v < 1.0));
            d32_sum += v;
        }
        double d32_mean = d32_sum / N;
        CHECK((d32_mean >= 0.45 && d32_mean <= 0.55));

        float f32_sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            float v = RandX::RandCanonical<float>(rng32);
            CHECK((v >= 0.0f && v < 1.0f));
            f32_sum += v;
        }
        float f32_mean = f32_sum / N;
        CHECK((f32_mean >= 0.45f && f32_mean <= 0.55f));
    }

    TEST_CASE("RandBool(0.0) 始终 false / RandBool(1.0) 始终 true")
    {
        RandX::Reseed(42);
        for (int i = 0; i < 100; ++i)
            CHECK_FALSE(RandX::RandBool(0.0));
        for (int i = 0; i < 100; ++i)
            CHECK(RandX::RandBool(1.0));
    }

    TEST_CASE("RandElement 单元素容器始终返回该元素")
    {
        RandX::Reseed(42);
        std::vector<int> single = { 99 };
        for (int i = 0; i < 50; ++i)
            CHECK(RandX::RandElement(single) == 99);
    }

    TEST_CASE("RandElement 右值容器按值返回维持生命周期安全")
    {
        RandX::Reseed(42);
        int val = RandX::RandElement(std::vector<int>{10, 20, 30});
        CHECK((val == 10 || val == 20 || val == 30));
    }

    TEST_CASE("RandString(0) 返回空字符串")
    {
        CHECK(RandX::RandString(0).empty());
        CHECK(RandX::RandString(0, RandX::CharSet::Hex).empty());
    }

    TEST_CASE("RandString 指定 Engine 和 custom string_view 重载及空 charset 异常")
    {
        RandX::Xoshiro256StarStar rng{ 123456 };
        auto s = RandX::RandString(rng, 10, "ABC");
        CHECK(s.size() == 10);
        for (char c : s)
        {
            CHECK((c == 'A' || c == 'B' || c == 'C'));
        }
        CHECK_THROWS_AS((void)RandX::RandString(rng, 10, ""), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandString(10, ""), std::invalid_argument);
    }

    TEST_CASE("RandVector(..., 0) 返回空容器")
    {
        auto v = RandX::RandVector<int>(0, 10, 0);
        CHECK(v.empty());
    }

    TEST_CASE("RandSample(n=0) 返回空容器")
    {
        std::vector<int> src = {1, 2, 3, 4, 5};
        auto s = RandX::RandSample(src, 0);
        CHECK(s.empty());
    }

    TEST_CASE("RandInt 全范围 [min, max] 极值安全")
    {
        RandX::Reseed(42);
        auto v = RandX::RandInt<std::uint64_t>(0, (std::numeric_limits<std::uint64_t>::max)());
        CHECK(v >= 0);
        auto v2 = RandX::RandInt<std::int32_t>((std::numeric_limits<std::int32_t>::min)(), (std::numeric_limits<std::int32_t>::max)());
        CHECK(v2 >= (std::numeric_limits<std::int32_t>::min)());
    }

    TEST_CASE("RandWeighted 单元素权重始终返回 0")
    {
        RandX::Reseed(42);
        std::vector<double> w = {1.0};
        for (int i = 0; i < 50; ++i)
            CHECK(RandX::RandWeighted(w) == 0);
    }

    TEST_CASE("RandPermutation(0) 和 RandPermutation(1)")
    {
        auto p0 = RandX::RandPermutation(0);
        CHECK(p0.empty());
        auto p1 = RandX::RandPermutation(1);
        REQUIRE(p1.size() == 1);
        CHECK(p1[0] == 0);
    }
}

// ============================================================================
// 核心契约与边界完备性验证套件 (C++17)
// ============================================================================
TEST_SUITE("核心契约与边界完备性 (C++17)")
{
    TEST_CASE("Input Iterator RandElement 按值返回")
    {
        std::istringstream iss("10 20 30 40 50");
        std::istream_iterator<int> beg(iss), end;
        int val = RandX::RandElement(beg, end);
        CHECK((val == 10 || val == 20 || val == 30 || val == 40 || val == 50));
    }

    TEST_CASE("C++17 原生 C 数组支持 (RandElement / RandSample)")
    {
        int arr[5] = {10, 20, 30, 40, 50};
        int val = RandX::RandElement(arr);
        CHECK((val >= 10 && val <= 50));

        auto sampled = RandX::RandSample(arr, 3);
        CHECK(sampled.size() == 3);
    }

    TEST_CASE("Engine& 重载分布模板形参顺序推导")
    {
        RandX::Xoshiro256StarStar rng{ 12345 };
        double valNorm = RandX::RandNormal(rng);
        double valLog = RandX::RandLogNormal(rng);
        int valGeom = RandX::RandGeometric(rng);
        double valExp = RandX::RandExp(rng);
        (void)valNorm; (void)valLog; (void)valExp;
        CHECK(valGeom >= 0);
    }

    TEST_CASE("分布非法参数显式抛出 std::invalid_argument")
    {
        CHECK_THROWS_AS((void)RandX::RandGeometric(0.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandGeometric(1.5), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandPoisson(-1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandNormal(0.0, -1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandExp(-2.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandLogNormal(0.0, 0.0), std::invalid_argument);
    }

    TEST_CASE("RandSample 逆序迭代器边界")
    {
        std::vector<int> v = {1, 2, 3, 4, 5};
        auto res = RandX::RandSample(v.end(), v.begin(), 3);
        CHECK(res.empty());
    }

    TEST_CASE("ChaCha20 moved-from 状态保护")
    {
        RandX::ChaCha20 a{ 12345 };
        RandX::ChaCha20 b = std::move(a);
        CHECK_THROWS_AS(a(), std::logic_error);
        CHECK_THROWS_AS(a.discard(1), std::logic_error);
        const std::uint64_t val = b();
        CHECK(val != 0ULL);
    }

    TEST_CASE("RandBits 整型与自定义引擎")
    {
        std::uint32_t bits32 = RandX::RandBits<32, std::uint32_t>();
        (void)bits32;
        RandX::Xoshiro256StarStar rng{ 999 };
        std::uint32_t bitsRng = RandX::RandBits<16, std::uint32_t>(rng);
        CHECK(bitsRng <= 0xFFFFU);
    }

    TEST_CASE("ResetThreadLocalEngine 重置线程局部引擎")
    {
        RandX::ResetThreadLocalEngine();
        int val = RandX::RandInt(1, 100);
        CHECK((val >= 1 && val <= 100));
    }
}

TEST_SUITE("流状态隔离与数值范围约束 (C++17)")
{
    TEST_CASE("反序列化全零非法状态设置 failbit 并保持原状态，隔离流格式标志")
    {
        RandX::Xoshiro256StarStar rng{ 123456789ULL };
        const auto orig_state = rng.serialize();

        // 1. 全零状态反序列化测试
        std::istringstream bad_is("0 0 0 0");
        bad_is >> rng;
        CHECK(bad_is.fail());
        CHECK(rng.serialize() == orig_state);

        // 2. 带 std::hex 和 std::noskipws 的格式标志隔离测试
        std::ostringstream oss;
        oss << std::hex << rng;
        std::string serialized_str = oss.str();

        std::istringstream iss(serialized_str);
        iss >> std::hex >> std::noskipws >> rng;
        CHECK_FALSE(iss.fail());
        CHECK(rng.serialize() == orig_state);
    }

    TEST_CASE("RandReal 半开区间上界约束 [min, max)")
    {
        RandX::Xoshiro256StarStar rng{ 42 };
        for (int i = 0; i < 100000; ++i)
        {
            double r = RandX::RandReal(rng, 0.0, 1.0);
            CHECK(r >= 0.0);
            CHECK(r < 1.0);
        }
    }

    TEST_CASE("RandInt 8-bit 整型与 char 支持")
    {
        RandX::Xoshiro256StarStar rng{ 42 };
        std::uint8_t u8 = RandX::RandInt<std::uint8_t>(rng, 10, 20);
        CHECK(u8 >= 10);
        CHECK(u8 <= 20);

        std::int8_t i8 = RandX::RandInt<std::int8_t>(rng, -50, 50);
        CHECK(i8 >= -50);
        CHECK(i8 <= 50);

        char c = RandX::RandInt<char>(rng, 'a', 'z');
        CHECK(c >= 'a');
        CHECK(c <= 'z');
    }

    TEST_CASE("RandBeta 极端参数数值有效性验证")
    {
        RandX::Xoshiro256StarStar rng{ 100 };
        for (int i = 0; i < 1000; ++i)
        {
            try
            {
                double beta_small = RandX::RandBeta(rng, 0.001, 0.001);
                CHECK(std::isfinite(beta_small));
                CHECK(beta_small >= 0.0);
                CHECK(beta_small <= 1.0);
            }
            catch (const std::domain_error&)
            {
                // 极小参数下两个 Gamma 采样值均下溢为 0.0 时允许抛出合法数值异常
            }

            double beta_large = RandX::RandBeta(rng, 1e6, 1e6);
            CHECK(std::isfinite(beta_large));
            CHECK(beta_large >= 0.0);
            CHECK(beta_large <= 1.0);
        }
    }

    TEST_CASE("MakeStreamEngine 流编号跳跃一致性")
    {
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

    TEST_CASE("RandBits 边界位数测试")
    {
        RandX::Xoshiro256StarStar rng{ 777 };
        auto b64 = RandX::RandBits<64, std::uint64_t>(rng);
        (void)b64;
        auto b1 = RandX::RandBits<1, std::uint64_t>(rng);
        CHECK(b1 <= 1ULL);
    }
}

TEST_SUITE("功能契约与边界扩展验证 (C++17)")
{
    TEST_CASE("vector<bool> 抽样与洗牌代理引用支持")
    {
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

    struct Synthetic32BitWideType
    {
        using result_type = std::uint64_t;
        static constexpr std::uint64_t min() { return 0; }
        static constexpr std::uint64_t max() { return 0xFFFFFFFFULL; }
        std::uint64_t val = 0x12345678ULL;
        std::uint64_t operator()()
        {
            val = (val * 1664525ULL + 1013904223ULL) & 0xFFFFFFFFULL;
            return val;
        }
    };

    struct SyntheticNonZeroMinEngine
    {
        using result_type = std::uint32_t;
        static constexpr std::uint32_t min() { return 10; }
        static constexpr std::uint32_t max() { return 20; }
        std::uint32_t val = 10;
        std::uint32_t operator()()
        {
            val = 10 + (val - 10 + 1) % 11;
            return val;
        }
    };

    TEST_CASE("mt19937、minstd_rand 与合成窄字长引擎浮点生成正值保证")
    {
        std::mt19937 mt{ 42 };
        float f = RandX::RandCanonical<float>(mt);
        CHECK(f > 0.0f);
        CHECK(f < 1.0f);

        Synthetic32BitWideType synth;
        float f_synth = RandX::RandCanonical<float>(synth);
        double d_synth = RandX::RandCanonical<double>(synth);
        CHECK(f_synth > 0.0f);
        CHECK(f_synth < 1.0f);
        CHECK(d_synth > 0.0);
        CHECK(d_synth < 1.0);

        std::minstd_rand minstd{ 42 };
        float f_minstd = RandX::RandCanonical<float>(minstd);
        CHECK(f_minstd > 0.0f);
        CHECK(f_minstd < 1.0f);

        SyntheticNonZeroMinEngine nonZeroMin;
        float f_nz = RandX::RandCanonical<float>(nonZeroMin);
        CHECK(f_nz >= 0.0f);
        CHECK(f_nz < 1.0f);
    }

    TEST_CASE("RandBits 多引擎与全位宽矩阵验证")
    {
        RandX::Xoshiro256StarStar rng64{ 42 };
        RandX::Xoshiro128StarStar rng32{ 42 };
        SyntheticNonZeroMinEngine nzEng;

        CHECK(RandX::RandBits<1>(rng64) <= 1ULL);
        CHECK(RandX::RandBits<8>(rng64) <= 0xFFULL);
        CHECK(RandX::RandBits<24>(rng64) <= 0xFFFFFFULL);
        CHECK(RandX::RandBits<31>(rng64) <= 0x7FFFFFFFULL);
        CHECK(RandX::RandBits<32>(rng64) <= 0xFFFFFFFFULL);
        CHECK(RandX::RandBits<33>(rng64) <= 0x1FFFFFFFFULL);
        CHECK(RandX::RandBits<48>(rng64) <= 0xFFFFFFFFFFFFULL);
        CHECK(RandX::RandBits<53>(rng64) <= 0x1FFFFFFFFFFFFFULL);
        CHECK(RandX::RandBits<63>(rng64) <= 0x7FFFFFFFFFFFFFFFULL);
        (void)RandX::RandBits<64>(rng64);

        CHECK(RandX::RandBits<1>(rng32) <= 1ULL);
        CHECK(RandX::RandBits<8>(rng32) <= 0xFFULL);
        CHECK(RandX::RandBits<24>(rng32) <= 0xFFFFFFULL);
        CHECK(RandX::RandBits<31>(rng32) <= 0x7FFFFFFFULL);
        CHECK(RandX::RandBits<32>(rng32) <= 0xFFFFFFFFULL);
        CHECK(RandX::RandBits<33>(rng32) <= 0x1FFFFFFFFULL);
        CHECK(RandX::RandBits<48>(rng32) <= 0xFFFFFFFFFFFFULL);
        CHECK(RandX::RandBits<53>(rng32) <= 0x1FFFFFFFFFFFFFULL);
        CHECK(RandX::RandBits<63>(rng32) <= 0x7FFFFFFFFFFFFFFFULL);
        (void)RandX::RandBits<64>(rng32);

        CHECK(RandX::RandBits<1>(nzEng) <= 1ULL);
        CHECK(RandX::RandBits<8>(nzEng) <= 0xFFULL);
        CHECK(RandX::RandBits<24>(nzEng) <= 0xFFFFFFULL);
        CHECK(RandX::RandBits<31>(nzEng) <= 0x7FFFFFFFULL);
        CHECK(RandX::RandBits<32>(nzEng) <= 0xFFFFFFFFULL);
        CHECK(RandX::RandBits<33>(nzEng) <= 0x1FFFFFFFFULL);
        CHECK(RandX::RandBits<48>(nzEng) <= 0xFFFFFFFFFFFFULL);
        CHECK(RandX::RandBits<53>(nzEng) <= 0x1FFFFFFFFFFFFFULL);
        CHECK(RandX::RandBits<63>(nzEng) <= 0x7FFFFFFFFFFFFFFFULL);
        (void)RandX::RandBits<64>(nzEng);
    }

    struct ThrowingEngine
    {
        using result_type = std::uint64_t;
        static constexpr std::uint64_t min() { return 0; }
        static constexpr std::uint64_t max() { return UINT64_MAX; }
        std::uint64_t operator()()
        {
            throw std::runtime_error("simulated engine failure");
        }
    };

    TEST_CASE("ThrowingEngine 异常正常向上抛出保证异常安全")
    {
        ThrowingEngine te;
        CHECK_THROWS_AS((void)RandX::RandCanonical<double>(te), std::runtime_error);
        CHECK_THROWS_AS((void)(RandX::RandBits<16, std::uint32_t>(te)), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandReal(te, 0.0, 1.0), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandReal(te, 0.0, 2.0), std::runtime_error);
    }

    TEST_CASE("ChaCha20 counter 边界 0xFFFFFFFFU 与生命周期契约")
    {
        std::array<std::uint8_t, 32> key{};
        std::array<std::uint8_t, 12> nonce{};

        RandX::ChaCha20 chacha_max(key.data(), 32, nonce.data(), 12, 0xFFFFFFFFU);
        for (int i = 0; i < 8; ++i)
        {
            CHECK_NOTHROW((void)chacha_max());
        }
        CHECK_THROWS_AS((void)chacha_max(), std::overflow_error);

        RandX::ChaCha20 chacha_max_minus_1(key.data(), 32, nonce.data(), 12, 0xFFFFFFFEU);
        for (int i = 0; i < 16; ++i)
        {
            CHECK_NOTHROW((void)chacha_max_minus_1());
        }
        CHECK_THROWS_AS((void)chacha_max_minus_1(), std::overflow_error);
    }

    TEST_CASE("NormalizeBetaSample 归一化与非法/无穷/零值异常")
    {
        CHECK(RandX::detail::NormalizeBetaSample(1.0, 1.0) == 0.5);
        CHECK(doctest::Approx(RandX::detail::NormalizeBetaSample(1e308, 1e308)).epsilon(1e-12) == 0.5);
        double ratio = RandX::detail::NormalizeBetaSample(1e308, 5e307);
        CHECK(doctest::Approx(ratio).epsilon(1e-12) == (1.0 / 1.5));
        CHECK(RandX::detail::NormalizeBetaSample(0.0, 5.0) == 0.0);
        CHECK(RandX::detail::NormalizeBetaSample(5.0, 0.0) == 1.0);

        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(0.0, 0.0), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(-1.0, 1.0), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(1.0, -1.0), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(std::numeric_limits<double>::infinity(), 1.0), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(1.0, std::numeric_limits<double>::infinity()), std::domain_error);
        CHECK_THROWS_AS((void)RandX::detail::NormalizeBetaSample(std::numeric_limits<double>::quiet_NaN(), 1.0), std::domain_error);

        CHECK_THROWS_AS((void)RandX::RandBeta(0.0, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(1.0, 0.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(-1.0, 2.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(std::numeric_limits<double>::quiet_NaN(), 1.0), std::invalid_argument);
    }

    struct CustomValidSeedSeq
    {
        void generate(std::uint32_t* begin, std::uint32_t* end)
        {
            std::fill(begin, end, 0x12345678u);
        }
    };
    struct TypeLackingGenerate {};

    template <class Engine>
    void VerifyEngineSeedSeqContracts()
    {
        int seed_i = 12345;
        Engine rng_i(seed_i);
        std::uint64_t seed_u = 12345ULL;
        Engine rng_u(seed_u);
        const std::uint64_t seed_cu = 12345ULL;
        Engine rng_cu(seed_cu);

        const auto val_i = rng_i();
        const auto val_u = rng_u();
        const auto val_cu = rng_cu();
        CHECK(val_i == val_u);
        CHECK(val_u == val_cu);

        Engine rng_copy = rng_u;
        const auto val_copy = rng_copy();
        const auto val_u_next = rng_u();
        CHECK(val_copy == val_u_next);

        typename Engine::state_type st = rng_u.serialize();
        Engine rng_st(st);
        CHECK(rng_st() == rng_u());

        std::seed_seq seq{ 1, 2, 3, 4, 5 };
        Engine rng_seq(seq);
        CHECK(rng_seq() != typename Engine::result_type{0});

        static_assert(std::is_constructible_v<Engine, CustomValidSeedSeq&>);
        static_assert(!std::is_constructible_v<Engine, TypeLackingGenerate&>);

        CustomValidSeedSeq custom_seq;
        Engine rng_custom(custom_seq);
        CHECK(rng_custom() != typename Engine::result_type{0});
    }

    TEST_CASE("7大PRNG引擎的SeedSequence与参数契约全面覆盖")
    {
        VerifyEngineSeedSeqContracts<RandX::SplitMix64>();
        VerifyEngineSeedSeqContracts<RandX::Xoshiro256StarStar>();
        VerifyEngineSeedSeqContracts<RandX::Xoroshiro128StarStar>();
        VerifyEngineSeedSeqContracts<RandX::Xoshiro128StarStar>();
        VerifyEngineSeedSeqContracts<RandX::Xoroshiro64StarStar>();
        VerifyEngineSeedSeqContracts<RandX::SFC64>();
        VerifyEngineSeedSeqContracts<RandX::RomuDuoJr>();

        RandX::Xoshiro256StarStar rng_i{ 12345 };
        CHECK_THROWS_AS((void)RandX::RandInt(rng_i, 10, 5), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandReal(rng_i, 1.0, 0.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBool(rng_i, -0.1), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBool(rng_i, 1.1), std::invalid_argument);

        std::vector<int> dummy(5);
        CHECK_THROWS_AS((void)RandX::RandFill(dummy.begin(), dummy.end(), 10, 5), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandVector(10, 5, 5), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandWeighted(std::vector<int>{ -1, 2 }), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandWeighted(std::vector<int>{ 0, 0 }), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandString(10, ""), std::invalid_argument);
        std::vector<int> empty_v;
        CHECK_THROWS_AS((void)RandX::RandElement(empty_v), std::invalid_argument);
    }

    TEST_CASE("SplitMix64::discard 步进等价性")
    {
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

    struct Scripted64BitEngine
    {
        using result_type = std::uint64_t;
        static constexpr std::uint64_t min() { return 0; }
        static constexpr std::uint64_t max() { return UINT64_MAX; }
        std::vector<std::uint64_t> seq;
        std::size_t idx = 0;
        std::uint64_t operator()()
        {
            if (idx < seq.size()) return seq[idx++];
            return 0;
        }
    };

    struct Scripted32BitEngine
    {
        using result_type = std::uint32_t;
        static constexpr std::uint32_t min() { return 0; }
        static constexpr std::uint32_t max() { return 0xFFFFFFFFU; }
        std::vector<std::uint32_t> seq;
        std::size_t idx = 0;
        std::uint32_t operator()()
        {
            if (idx < seq.size()) return seq[idx++];
            return 0;
        }
    };

    TEST_CASE("Scripted 引擎精确受控 UUID 字段与 RFC 4122 变体验证")
    {
        Scripted64BitEngine s64{{ 0x0123456789abcdefULL, 0xfedcba9876543210ULL }, 0};
        std::string uuid64 = RandX::RandUUID(s64);
        CHECK(uuid64.length() == 36);
        CHECK(uuid64[8] == '-');
        CHECK(uuid64[13] == '-');
        CHECK(uuid64[14] == '4');
        CHECK(uuid64[18] == '-');
        CHECK(uuid64[19] == '8');
        CHECK(uuid64[23] == '-');
        CHECK(uuid64.substr(0, 8) == "fedcba98");
        CHECK(uuid64.substr(9, 4) == "7654");
        CHECK(uuid64.substr(15, 3) == "210");
        CHECK(uuid64.substr(20, 3) == "123");
        CHECK(uuid64.substr(24, 12) == "456789abcdef");

        // 32 位引擎路径：lo 先取，hi 后取
        Scripted32BitEngine s32{{ 0x89abcdefU, 0x01234567U, 0x76543210U, 0xfedcba98U }, 0};
        std::string uuid32 = RandX::RandUUID(s32);
        CHECK(uuid32 == uuid64);
    }

    TEST_CASE("RandChar 字符区间合法性异常契约")
    {
        CHECK_THROWS_AS((void)RandX::RandChar('z', 'a'), std::invalid_argument);
        RandX::Xoshiro256StarStar rng{ 42 };
        CHECK_THROWS_AS((void)RandX::RandChar(rng, '9', '0'), std::invalid_argument);
        CHECK_NOTHROW((void)RandX::RandChar('a', 'z'));
    }

    TEST_CASE("ChaCha20 自移动与移出态连续安全操作")
    {
        RandX::ChaCha20 a{ 12345 };
        // 自移动赋值安全
        auto& ref = a;
        a = std::move(ref);
        CHECK_NOTHROW((void)a());

        RandX::ChaCha20 b = std::move(a);
        // a 处于 moved-from 状态
        CHECK_THROWS_AS((void)a(), std::logic_error);
        CHECK_THROWS_AS((void)a.discard(5), std::logic_error);

        // 重复移动 moved-from 对象安全
        RandX::ChaCha20 c = std::move(a);
        CHECK_THROWS_AS((void)c(), std::logic_error);

        // 重新赋值 moved-from 对象可复活
        RandX::ChaCha20 revived{ 42 };
        a = std::move(revived);
        CHECK_NOTHROW((void)a());

        // 如果 OS 熵源可用，显式 reseed 可复活 moved-from 对象
        if (RandX::IsOsCryptoEntropyAvailable())
        {
            b.reseed();
            CHECK_NOTHROW((void)b());
        }
    }

    template <class ResultType, ResultType MinVal, ResultType MaxVal>
    struct CallCountingEngine
    {
        using result_type = ResultType;
        static constexpr result_type min() { return MinVal; }
        static constexpr result_type max() { return MaxVal; }
        std::size_t call_count = 0;
        ResultType value = static_cast<ResultType>(0x12345678);
        result_type operator()()
        {
            ++call_count;
            return value++;
        }
    };

    TEST_CASE("引擎调用次数与64/32位组合逻辑验收")
    {
        // 64 位引擎消耗次数
        {
            CallCountingEngine<std::uint64_t, 0, UINT64_MAX> eng64;
            (void)RandX::RandCanonical<float>(eng64);
            CHECK(eng64.call_count == 1);

            eng64.call_count = 0;
            (void)RandX::RandCanonical<double>(eng64);
            CHECK(eng64.call_count == 1);

            eng64.call_count = 0;
            (void)RandX::RandReal(eng64, 0.0f, 1.0f);
            CHECK(eng64.call_count == 1);

            eng64.call_count = 0;
            (void)RandX::RandReal(eng64, 0.0, 1.0);
            CHECK(eng64.call_count == 1);

            eng64.call_count = 0;
            (void)RandX::RandBits<32>(eng64);
            CHECK(eng64.call_count == 1);

            eng64.call_count = 0;
            (void)RandX::RandBits<64>(eng64);
            CHECK(eng64.call_count == 1);

            eng64.call_count = 0;
            (void)RandX::RandUUID(eng64);
            CHECK(eng64.call_count == 2);
        }

        // 32 位引擎消耗次数
        {
            CallCountingEngine<std::uint32_t, 0, 0xFFFFFFFFU> eng32;
            (void)RandX::RandCanonical<float>(eng32);
            CHECK(eng32.call_count == 1);

            eng32.call_count = 0;
            (void)RandX::RandCanonical<double>(eng32);
            CHECK(eng32.call_count == 2);

            eng32.call_count = 0;
            (void)RandX::RandReal(eng32, 0.0f, 1.0f);
            CHECK(eng32.call_count == 1);

            eng32.call_count = 0;
            (void)RandX::RandReal(eng32, 0.0, 1.0);
            CHECK(eng32.call_count == 2);

            eng32.call_count = 0;
            (void)RandX::RandBits<32>(eng32);
            CHECK(eng32.call_count == 1);

            eng32.call_count = 0;
            (void)RandX::RandBits<64>(eng32);
            CHECK(eng32.call_count == 2);

            eng32.call_count = 0;
            (void)RandX::RandUUID(eng32);
            CHECK(eng32.call_count == 4);
        }
    }

    TEST_CASE("vector<bool> 代理引用抽样与洗牌多重集合不变量")
    {
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

    template <class C, class = void>
    struct can_shuffle : std::false_type {};
    template <class C>
    struct can_shuffle<C, std::void_t<decltype(RandX::RandShuffle(std::declval<C&>()))>> : std::true_type {};

    template <int N, class T, class = void>
    struct can_rand_bits : std::false_type {};
    template <int N, class T>
    struct can_rand_bits<N, T, std::void_t<decltype(RandX::RandBits<N, T>())>> : std::true_type {};

    TEST_CASE("SFINAE 契约约束诊断验证")
    {
        static_assert(!can_shuffle<const std::vector<int>>::value, "RandShuffle must reject const containers");
        static_assert(!can_rand_bits<32, std::int32_t>::value, "RandBits must reject signed int32_t for 32 bits");
        static_assert(!can_rand_bits<64, std::int64_t>::value, "RandBits must reject signed int64_t for 64 bits");
        static_assert(!can_rand_bits<1, bool>::value, "RandBits must reject bool");
    }

    TEST_CASE("RandBeta 端到端数值行为与大参数验证")
    {
        RandX::Xoshiro256StarStar rng{ 12345 };
        double sum = 0.0;
        constexpr int n_samples = 1000;
        for (int i = 0; i < n_samples; ++i)
        {
            double val = RandX::RandBeta(rng, 2.0, 2.0);
            CHECK(val >= 0.0);
            CHECK(val <= 1.0);
            sum += val;
        }
        double mean = sum / n_samples;
        CHECK(doctest::Approx(mean).epsilon(0.05) == 0.5);

        const double largeShape = (std::numeric_limits<double>::max)();
        double big_sample = RandX::RandBeta(rng, largeShape, largeShape);
        CHECK(std::isfinite(big_sample));
        CHECK(big_sample == 0.5);

        double skewed_sample = RandX::RandBeta(rng, 2.0, largeShape);
        CHECK(std::isfinite(skewed_sample));
        CHECK(skewed_sample >= 0.0);
        CHECK(skewed_sample <= 1.0);
    }
}

TEST_SUITE("OverloadContract")
{
    struct ThrowingEngine
    {
        using result_type = std::uint32_t;
        static constexpr result_type min() noexcept { return 0; }
        static constexpr result_type max() noexcept { return 1000; }
        result_type operator()()
        {
            throw std::runtime_error("ThrowingEngine invoked");
        }
    };

    struct MoveOnlyEngine
    {
        using result_type = std::uint64_t;
        static constexpr result_type min() noexcept { return 0; }
        static constexpr result_type max() noexcept { return UINT64_MAX; }
        MoveOnlyEngine() = default;
        MoveOnlyEngine(const MoveOnlyEngine&) = delete;
        MoveOnlyEngine& operator=(const MoveOnlyEngine&) = delete;
        MoveOnlyEngine(MoveOnlyEngine&&) = default;
        MoveOnlyEngine& operator=(MoveOnlyEngine&&) = default;
        result_type operator()() noexcept { return 42; }
    };

    struct NotAnEngine { int val{ 0 }; };

    struct IncompleteEngineNoMinMax {
        using result_type = std::uint32_t;
        result_type operator()() { return 0; }
    };

    struct IncompleteEngineSignedResult {
        using result_type = int;
        static constexpr int min() { return 0; }
        static constexpr int max() { return 10; }
        int operator()() { return 0; }
    };

    struct IncompleteEngineBoolResult {
        using result_type = bool;
        static constexpr bool min() { return false; }
        static constexpr bool max() { return true; }
        bool operator()() { return false; }
    };

    TEST_CASE("RandomEngine 概念与特征检测")
    {
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoshiro256StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoroshiro128StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoshiro128StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::Xoroshiro64StarStar>);
        static_assert(RandX::detail::is_random_engine_v<RandX::SplitMix64>);
        static_assert(RandX::detail::is_random_engine_v<RandX::SFC64>);
        static_assert(RandX::detail::is_random_engine_v<RandX::RomuDuoJr>);
        static_assert(RandX::detail::is_random_engine_v<RandX::ChaCha20>);
        static_assert(RandX::detail::is_random_engine_v<ThrowingEngine>);
        static_assert(RandX::detail::is_random_engine_v<MoveOnlyEngine>);

        static_assert(!RandX::detail::is_random_engine_v<NotAnEngine>);
        static_assert(!RandX::detail::is_random_engine_v<IncompleteEngineNoMinMax>);
        static_assert(!RandX::detail::is_random_engine_v<IncompleteEngineSignedResult>);
        static_assert(!RandX::detail::is_random_engine_v<IncompleteEngineBoolResult>);
        static_assert(!RandX::detail::is_random_engine_v<int>);
        static_assert(!RandX::detail::is_random_engine_v<double>);
        static_assert(!RandX::detail::is_random_engine_v<bool>);
        static_assert(!RandX::detail::is_random_engine_v<std::string>);
    }

    TEST_CASE("数值左值与常量左值不触发引擎重载歧义")
    {
        RandX::Xoshiro256StarStar rng(12345);

        // RandNormal
        double m = 2.0;
        double s = 0.5;
        const double cm = 2.0;
        const double cs = 0.5;
        CHECK(std::isfinite(RandX::RandNormal(2.0, 0.5)));
        CHECK(std::isfinite(RandX::RandNormal(2.0)));
        CHECK(std::isfinite(RandX::RandNormal()));
        CHECK(std::isfinite(RandX::RandNormal(m, s)));
        CHECK(std::isfinite(RandX::RandNormal(m)));
        CHECK(std::isfinite(RandX::RandNormal(cm, cs)));
        CHECK(std::isfinite(RandX::RandNormal(cm)));
        CHECK(std::isfinite(RandX::RandNormal(rng, 2.0, 0.5)));
        CHECK(std::isfinite(RandX::RandNormal(rng, m, s)));
        CHECK(std::isfinite(RandX::RandNormal(rng, cm, cs)));
        CHECK(std::isfinite(RandX::RandNormal(rng, m)));
        CHECK(std::isfinite(RandX::RandNormal(rng)));

        // RandExp
        double lam = 1.5;
        const double clam = 1.5;
        CHECK(std::isfinite(RandX::RandExp(1.5)));
        CHECK(std::isfinite(RandX::RandExp(lam)));
        CHECK(std::isfinite(RandX::RandExp(clam)));
        CHECK(std::isfinite(RandX::RandExp(rng, 1.5)));
        CHECK(std::isfinite(RandX::RandExp(rng, lam)));
        CHECK(std::isfinite(RandX::RandExp(rng, clam)));
        CHECK(std::isfinite(RandX::RandExp(rng)));

        // RandGamma
        double a = 2.0, b = 1.5;
        const double ca = 2.0, cb = 1.5;
        CHECK(std::isfinite(RandX::RandGamma(2.0, 1.5)));
        CHECK(std::isfinite(RandX::RandGamma(a, b)));
        CHECK(std::isfinite(RandX::RandGamma(ca, cb)));
        CHECK(std::isfinite(RandX::RandGamma(a)));
        CHECK(std::isfinite(RandX::RandGamma(ca)));
        CHECK(std::isfinite(RandX::RandGamma(rng, a, b)));
        CHECK(std::isfinite(RandX::RandGamma(rng, ca, cb)));
        CHECK(std::isfinite(RandX::RandGamma(rng, a)));
        CHECK(std::isfinite(RandX::RandGamma(rng)));

        // RandBeta
        CHECK(std::isfinite(RandX::RandBeta(2.0, 1.5)));
        CHECK(std::isfinite(RandX::RandBeta(a, b)));
        CHECK(std::isfinite(RandX::RandBeta(ca, cb)));
        CHECK(std::isfinite(RandX::RandBeta(rng, a, b)));
        CHECK(std::isfinite(RandX::RandBeta(rng, ca, cb)));

        // RandLogNormal
        CHECK(std::isfinite(RandX::RandLogNormal(0.0, 1.0)));
        CHECK(std::isfinite(RandX::RandLogNormal(m, s)));
        CHECK(std::isfinite(RandX::RandLogNormal(cm, cs)));
        CHECK(std::isfinite(RandX::RandLogNormal(m)));
        CHECK(std::isfinite(RandX::RandLogNormal(cm)));
        CHECK(std::isfinite(RandX::RandLogNormal(rng, m, s)));
        CHECK(std::isfinite(RandX::RandLogNormal(rng, cm, cs)));
        CHECK(std::isfinite(RandX::RandLogNormal(rng, m)));
        CHECK(std::isfinite(RandX::RandLogNormal(rng)));

        // RandCauchy
        CHECK(std::isfinite(RandX::RandCauchy(0.0, 1.0)));
        CHECK(std::isfinite(RandX::RandCauchy(m, s)));
        CHECK(std::isfinite(RandX::RandCauchy(cm, cs)));
        CHECK(std::isfinite(RandX::RandCauchy(m)));
        CHECK(std::isfinite(RandX::RandCauchy(cm)));
        CHECK(std::isfinite(RandX::RandCauchy(rng, m, s)));
        CHECK(std::isfinite(RandX::RandCauchy(rng, cm, cs)));
        CHECK(std::isfinite(RandX::RandCauchy(rng, m)));
        CHECK(std::isfinite(RandX::RandCauchy(rng)));

        // RandWeibull
        CHECK(std::isfinite(RandX::RandWeibull(1.0, 2.0)));
        CHECK(std::isfinite(RandX::RandWeibull(a, b)));
        CHECK(std::isfinite(RandX::RandWeibull(ca, cb)));
        CHECK(std::isfinite(RandX::RandWeibull(a)));
        CHECK(std::isfinite(RandX::RandWeibull(ca)));
        CHECK(std::isfinite(RandX::RandWeibull(rng, a, b)));
        CHECK(std::isfinite(RandX::RandWeibull(rng, ca, cb)));
        CHECK(std::isfinite(RandX::RandWeibull(rng, a)));
        CHECK(std::isfinite(RandX::RandWeibull(rng)));

        // RandExtremeValue
        CHECK(std::isfinite(RandX::RandExtremeValue(0.0, 1.0)));
        CHECK(std::isfinite(RandX::RandExtremeValue(m, s)));
        CHECK(std::isfinite(RandX::RandExtremeValue(cm, cs)));
        CHECK(std::isfinite(RandX::RandExtremeValue(m)));
        CHECK(std::isfinite(RandX::RandExtremeValue(cm)));
        CHECK(std::isfinite(RandX::RandExtremeValue(rng, m, s)));
        CHECK(std::isfinite(RandX::RandExtremeValue(rng, cm, cs)));
        CHECK(std::isfinite(RandX::RandExtremeValue(rng, m)));
        CHECK(std::isfinite(RandX::RandExtremeValue(rng)));

        // RandChiSquared
        double deg = 3.0;
        const double cdeg = 3.0;
        CHECK(std::isfinite(RandX::RandChiSquared(3.0)));
        CHECK(std::isfinite(RandX::RandChiSquared(deg)));
        CHECK(std::isfinite(RandX::RandChiSquared(cdeg)));
        CHECK(std::isfinite(RandX::RandChiSquared(rng, deg)));
        CHECK(std::isfinite(RandX::RandChiSquared(rng, cdeg)));
        CHECK(std::isfinite(RandX::RandChiSquared(rng)));

        // RandStudentT
        CHECK(std::isfinite(RandX::RandStudentT(3.0)));
        CHECK(std::isfinite(RandX::RandStudentT(deg)));
        CHECK(std::isfinite(RandX::RandStudentT(cdeg)));
        CHECK(std::isfinite(RandX::RandStudentT(rng, deg)));
        CHECK(std::isfinite(RandX::RandStudentT(rng, cdeg)));
        CHECK(std::isfinite(RandX::RandStudentT(rng)));

        // RandFisherF
        CHECK(std::isfinite(RandX::RandFisherF(2.0, 3.0)));
        CHECK(std::isfinite(RandX::RandFisherF(a, b)));
        CHECK(std::isfinite(RandX::RandFisherF(ca, cb)));
        CHECK(std::isfinite(RandX::RandFisherF(a)));
        CHECK(std::isfinite(RandX::RandFisherF(ca)));
        CHECK(std::isfinite(RandX::RandFisherF(rng, a, b)));
        CHECK(std::isfinite(RandX::RandFisherF(rng, ca, cb)));
        CHECK(std::isfinite(RandX::RandFisherF(rng, a)));
        CHECK(std::isfinite(RandX::RandFisherF(rng)));

        // RandPoisson
        CHECK(RandX::RandPoisson(3.0) >= 0);
        CHECK(RandX::RandPoisson(deg) >= 0);
        CHECK(RandX::RandPoisson(cdeg) >= 0);
        CHECK(RandX::RandPoisson(rng, deg) >= 0);
        CHECK(RandX::RandPoisson(rng, cdeg) >= 0);
        CHECK(RandX::RandPoisson(rng) >= 0);

        // RandBinomial
        int trials = 10;
        const int ctrials = 10;
        double prob = 0.5;
        const double cprob = 0.5;
        CHECK(RandX::RandBinomial(10, 0.5) >= 0);
        CHECK(RandX::RandBinomial(trials, prob) >= 0);
        CHECK(RandX::RandBinomial(ctrials, cprob) >= 0);
        CHECK(RandX::RandBinomial(trials) >= 0);
        CHECK(RandX::RandBinomial(ctrials) >= 0);
        CHECK(RandX::RandBinomial(rng, trials, prob) >= 0);
        CHECK(RandX::RandBinomial(rng, ctrials, cprob) >= 0);
        CHECK(RandX::RandBinomial(rng, trials) >= 0);
        CHECK(RandX::RandBinomial(rng) >= 0);

        // RandGeometric
        CHECK(RandX::RandGeometric(0.5) >= 0);
        CHECK(RandX::RandGeometric(prob) >= 0);
        CHECK(RandX::RandGeometric(cprob) >= 0);
        CHECK(RandX::RandGeometric(rng, prob) >= 0);
        CHECK(RandX::RandGeometric(rng, cprob) >= 0);
        CHECK(RandX::RandGeometric(rng) >= 0);

        // RandInt
        int iv1 = 10, iv2 = 20;
        const int civ1 = 10, civ2 = 20;
        int r_int1 = RandX::RandInt(iv1, iv2);
        CHECK(r_int1 >= 10);
        CHECK(r_int1 <= 20);
        int r_int2 = RandX::RandInt(civ1, civ2);
        CHECK(r_int2 >= 10);
        CHECK(r_int2 <= 20);
        int r_int3 = RandX::RandInt(rng, iv1, iv2);
        CHECK(r_int3 >= 10);
        CHECK(r_int3 <= 20);

        // RandReal
        double rv1 = 1.0, rv2 = 5.0;
        const double crv1 = 1.0, crv2 = 5.0;
        double r_real1 = RandX::RandReal(rv1, rv2);
        CHECK(r_real1 >= 1.0);
        CHECK(r_real1 < 5.0);
        double r_real2 = RandX::RandReal(crv1, crv2);
        CHECK(r_real2 >= 1.0);
        CHECK(r_real2 < 5.0);
        double r_real3 = RandX::RandReal(rv1);
        CHECK(r_real3 >= 1.0);
        CHECK(r_real3 < 2.0);
        double r_real4 = RandX::RandReal(crv1);
        CHECK(r_real4 >= 1.0);
        CHECK(r_real4 < 2.0);
        double r_real5 = RandX::RandReal(rng, rv1, rv2);
        CHECK(r_real5 >= 1.0);
        CHECK(r_real5 < 5.0);

        // RandBool & RandBernoulli
        double bp = 0.8;
        const double cbp = 0.8;
        (void)RandX::RandBool(bp);
        (void)RandX::RandBool(cbp);
        (void)RandX::RandBool(rng, bp);
        (void)RandX::RandBernoulli(bp);
        (void)RandX::RandBernoulli(cbp);
        (void)RandX::RandBernoulli(rng, bp);

        // RandChar
        char ch1 = 'a', ch2 = 'z';
        const char cch1 = 'a', cch2 = 'z';
        char ch_out1 = RandX::RandChar(ch1, ch2);
        CHECK(ch_out1 >= 'a');
        CHECK(ch_out1 <= 'z');
        char ch_out2 = RandX::RandChar(cch1, cch2);
        CHECK(ch_out2 >= 'a');
        CHECK(ch_out2 <= 'z');
        char ch_out3 = RandX::RandChar(ch2);
        CHECK(ch_out3 <= 'z');
        char ch_out4 = RandX::RandChar(cch2);
        CHECK(ch_out4 <= 'z');
        char ch_out5 = RandX::RandChar(rng, ch1, ch2);
        CHECK(ch_out5 >= 'a');
        CHECK(ch_out5 <= 'z');
        char ch_out6 = RandX::RandChar(rng, ch2);
        CHECK(ch_out6 <= 'z');
        char ch_out7 = RandX::RandChar(rng, RandX::CharSet::Alpha);
        CHECK(std::isalpha(static_cast<unsigned char>(ch_out7)));

        // RandBits
        auto bits1 = RandX::RandBits<16>();
        CHECK(bits1 < (1ULL << 16));
        auto bits2 = RandX::RandBits<16>(rng);
        CHECK(bits2 < (1ULL << 16));

        // RandUUID
        auto uuid1 = RandX::RandUUID();
        CHECK(uuid1.size() == 36);
        auto uuid2 = RandX::RandUUID(rng);
        CHECK(uuid2.size() == 36);

        // RandVector
        std::size_t n_elem = 5;
        const std::size_t cn_elem = 5;
        auto vec_int1 = RandX::RandVector(iv1, iv2, n_elem);
        CHECK(vec_int1.size() == 5);
        auto vec_int2 = RandX::RandVector(rng, iv1, iv2, cn_elem);
        CHECK(vec_int2.size() == 5);
        auto vec_real1 = RandX::RandVector(rv1, rv2, n_elem);
        CHECK(vec_real1.size() == 5);
        auto vec_real2 = RandX::RandVector(rng, rv1, rv2, cn_elem);
        CHECK(vec_real2.size() == 5);
    }

    TEST_CASE("ThrowingEngine 异常可传播")
    {
        ThrowingEngine te;
        CHECK_THROWS_AS((void)RandX::RandNormal(te, 0.0, 1.0), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandInt(te, 1, 10), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandReal(te, 0.0, 1.0), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandBool(te, 0.5), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandExp(te, 1.0), std::runtime_error);
    }

    TEST_CASE("MoveOnlyEngine 支持")
    {
        MoveOnlyEngine moe;
        int val = RandX::RandInt(moe, 1, 100);
        CHECK(val >= 1);
        CHECK(val <= 100);
    }

    TEST_CASE("同种子下显式引擎与默认引擎一致性")
    {
        RandX::Xoshiro256StarStar e1(123456ULL);
        RandX::Xoshiro256StarStar e2(123456ULL);
        double v1 = RandX::RandNormal(e1, 3.5);
        double v2 = RandX::RandNormal(e2, 3.5);
        CHECK(v1 == v2);
    }
}

// ============================================================================
// WeightedScale：稳定权重采样验收测试
// ============================================================================
TEST_SUITE("WeightedScale")
{
    struct CountingEngine
    {
        using result_type = std::uint64_t;
        static constexpr result_type min() { return 0; }
        static constexpr result_type max() { return UINT64_MAX; }

        std::size_t call_count{ 0 };
        result_type operator()() noexcept
        {
            ++call_count;
            return 42ULL;
        }
    };

    TEST_CASE("大尺度与小尺度归一化")
    {
        // 1e308 尺度：两项求和会溢出为 inf，但归一化后各 50%
        std::vector<double> huge_w = { 1e308, 1e308 };
        int huge_c0 = 0;
        for (int i = 0; i < 200; ++i)
        {
            auto idx = RandX::RandWeighted(huge_w);
            CHECK((idx == 0 || idx == 1));
            if (idx == 0) ++huge_c0;
        }
        CHECK(huge_c0 > 20);
        CHECK(huge_c0 < 180);

        // 1e-300 尺度：小权重不退化
        std::vector<double> tiny_w = { 1e-300, 1e-300 };
        int tiny_c0 = 0;
        for (int i = 0; i < 200; ++i)
        {
            auto idx = RandX::RandWeighted(tiny_w);
            CHECK((idx == 0 || idx == 1));
            if (idx == 0) ++tiny_c0;
        }
        CHECK(tiny_c0 > 20);
        CHECK(tiny_c0 < 180);

        // 精确二次幂整体缩放性质：相同种子下输出完全一致
        RandX::Xoshiro256StarStar rng1{ 7777 }, rng2{ 7777 };
        std::vector<double> w_base = { 1.0, 2.0, 4.0 };
        std::vector<double> w_scaled = { 1e200, 2e200, 4e200 };
        for (int i = 0; i < 50; ++i)
        {
            auto idx1 = RandX::RandWeighted(rng1, w_base);
            auto idx2 = RandX::RandWeighted(rng2, w_scaled);
            CHECK(idx1 == idx2);
        }
    }

    TEST_CASE("零权重排除与原下标映射")
    {
        // 单个正权重：100% 只返回原下标 2
        std::vector<int> sparse1 = { 0, 0, 3, 0 };
        for (int i = 0; i < 1000; ++i)
        {
            CHECK(RandX::RandWeighted(sparse1) == 2);
        }

        // 多个正权重与零权重混合
        std::vector<double> sparse2 = { 0.0, 5.0, 0.0, 10.0, 0.0 };
        int c1 = 0, c3 = 0;
        for (int i = 0; i < 1000; ++i)
        {
            auto idx = RandX::RandWeighted(sparse2);
            CHECK((idx == 1 || idx == 3));
            if (idx == 1) ++c1;
            if (idx == 3) ++c3;
        }
        CHECK(c1 > 150);
        CHECK(c3 > 400);
    }

    TEST_CASE("不合法权重在消费引擎前抛出 invalid_argument")
    {
        std::vector<double> empty_w;
        std::vector<double> all_zero = { 0.0, 0.0, 0.0 };
        std::vector<double> has_neg = { -1.0, 2.0 };
        std::vector<double> has_nan = { std::numeric_limits<double>::quiet_NaN(), 2.0 };
        std::vector<double> has_inf = { std::numeric_limits<double>::infinity(), 2.0 };

        // 默认引擎版本
        CHECK_THROWS_AS((void)RandX::RandWeighted(empty_w), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandWeighted(all_zero), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandWeighted(has_neg), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandWeighted(has_nan), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandWeighted(has_inf), std::invalid_argument);

        // 显式 CountingEngine 验证在消费引擎前抛异常
        CountingEngine ce;
        CHECK_THROWS_AS((void)RandX::RandWeighted(ce, empty_w), std::invalid_argument);
        CHECK(ce.call_count == 0);
        CHECK_THROWS_AS((void)RandX::RandWeighted(ce, all_zero), std::invalid_argument);
        CHECK(ce.call_count == 0);
        CHECK_THROWS_AS((void)RandX::RandWeighted(ce, has_neg), std::invalid_argument);
        CHECK(ce.call_count == 0);
        CHECK_THROWS_AS((void)RandX::RandWeighted(ce, has_nan), std::invalid_argument);
        CHECK(ce.call_count == 0);
        CHECK_THROWS_AS((void)RandX::RandWeighted(ce, has_inf), std::invalid_argument);
        CHECK(ce.call_count == 0);
    }

    TEST_CASE("固定种子多标准误统计实验")
    {
        constexpr int N = 100000;
        RandX::Xoshiro256StarStar rng1{ 12345 };
        std::vector<double> huge_w = { 1e308, 1e308 };
        int count0 = 0;
        for (int i = 0; i < N; ++i)
        {
            if (RandX::RandWeighted(rng1, huge_w) == 0)
                ++count0;
        }
        // N=100000, p=0.5, sigma ≈ 158.11, 5 sigma ≈ 790
        CHECK(std::abs(count0 - 50000) < 790);

        RandX::Xoshiro256StarStar rng2{ 12345 };
        std::vector<double> norm_w = { 1.0, 1.0 };
        int norm_count0 = 0;
        for (int i = 0; i < N; ++i)
        {
            if (RandX::RandWeighted(rng2, norm_w) == 0)
                ++norm_count0;
        }
        CHECK(std::abs(norm_count0 - 50000) < 790);
    }

    TEST_CASE("极端动态范围下溢抛出 range_error")
    {
        std::vector<double> extreme = { 1e300, 1e-320 };
        CHECK_THROWS_AS((void)RandX::RandWeighted(extreme), std::range_error);
    }

    TEST_CASE("预构建 discrete_distribution 透明转发")
    {
        RandX::Xoshiro256StarStar rng{ 42 };
        std::discrete_distribution<int> dist({ 1.0, 2.0, 3.0 });
        auto v1 = RandX::RandWeighted(dist);
        CHECK((v1 >= 0 && v1 <= 2));
        auto v2 = RandX::RandWeighted(rng, dist);
        CHECK((v2 >= 0 && v2 <= 2));
    }
}

TEST_SUITE("RealInterval")
{
    struct IntervalCountingEngine
    {
        using result_type = std::uint64_t;
        static constexpr result_type min() { return 0; }
        static constexpr result_type max() { return UINT64_MAX; }

        std::size_t call_count{ 0 };
        result_type operator()() noexcept
        {
            ++call_count;
            return 1234567890ULL;
        }
    };

    template <typename T, typename Engine>
    void RunAdjacentIntervalTests(Engine& rng)
    {
        // 正区间 [1, nextafter(1, 2))
        const T min_pos = T{ 1 };
        const T max_pos = std::nextafter(min_pos, T{ 2 });
        for (int i = 0; i < 100; ++i)
        {
            CHECK(RandX::RandReal(rng, min_pos, max_pos) == min_pos);
        }

        // 负区间 [-2, nextafter(-2, -1))
        const T min_neg = T{ -2 };
        const T max_neg = std::nextafter(min_neg, T{ -1 });
        for (int i = 0; i < 100; ++i)
        {
            CHECK(RandX::RandReal(rng, min_neg, max_neg) == min_neg);
        }

        // 跨零区间 [0, nextafter(0, 1))
        const T min_zero = T{ 0 };
        const T max_zero = std::nextafter(min_zero, T{ 1 });
        for (int i = 0; i < 100; ++i)
        {
            CHECK(RandX::RandReal(rng, min_zero, max_zero) == min_zero);
        }

        // subnormal 附近区间
        const T min_sub = std::numeric_limits<T>::denorm_min();
        if (min_sub > T{ 0 })
        {
            const T max_sub = std::nextafter(min_sub, T{ 1 });
            for (int i = 0; i < 100; ++i)
            {
                CHECK(RandX::RandReal(rng, min_sub, max_sub) == min_sub);
            }
        }

        // RandFill 入口
        std::vector<T> buf(50);
        RandX::RandFill(rng, buf.begin(), buf.end(), min_pos, max_pos);
        for (auto val : buf)
        {
            CHECK(val == min_pos);
        }

        // RandVector 入口
        auto vec = RandX::RandVector(rng, min_pos, max_pos, 50);
        for (auto val : vec)
        {
            CHECK(val == min_pos);
        }
    }

    TEST_CASE("邻接浮点区间全部结果严格等于下界")
    {
        RandX::Xoshiro256StarStar rng{ 42 };
        RunAdjacentIntervalTests<float>(rng);
        RunAdjacentIntervalTests<double>(rng);
        RunAdjacentIntervalTests<long double>(rng);
    }

    TEST_CASE("退化区间返回端点且不消费引擎")
    {
        IntervalCountingEngine ce;
        // 单值
        double r1 = RandX::RandReal(ce, 3.14, 3.14);
        CHECK(r1 == 3.14);
        CHECK(ce.call_count == 0);

        // RandFill
        std::vector<double> buf(10, 0.0);
        RandX::RandFill(ce, buf.begin(), buf.end(), 2.718, 2.718);
        CHECK(ce.call_count == 0);
        for (double v : buf)
        {
            CHECK(v == 2.718);
        }

        // RandVector
        auto vec = RandX::RandVector(ce, 1.414, 1.414, 10);
        CHECK(ce.call_count == 0);
        CHECK(vec.size() == 10);
        for (double v : vec)
        {
            CHECK(v == 1.414);
        }
    }

    TEST_CASE("空范围与零大小不消费引擎但仍校验参数")
    {
        IntervalCountingEngine ce;
        std::vector<double> empty_buf;

        // 合法区间，空范围
        RandX::RandFill(ce, empty_buf.begin(), empty_buf.end(), 1.0, 2.0);
        CHECK(ce.call_count == 0);

        auto empty_vec = RandX::RandVector(ce, 1.0, 2.0, 0);
        CHECK(ce.call_count == 0);
        CHECK(empty_vec.empty());

        // 非法参数，即使是空范围也必须抛出 invalid_argument 且不消费引擎
        CHECK_THROWS_AS(RandX::RandFill(ce, empty_buf.begin(), empty_buf.end(), 2.0, 1.0), std::invalid_argument);
        CHECK(ce.call_count == 0);

        CHECK_THROWS_AS((void)RandX::RandVector(ce, 2.0, 1.0, 0), std::invalid_argument);
        CHECK(ce.call_count == 0);
    }

    TEST_CASE("非法输入与超宽区间拒绝")
    {
        IntervalCountingEngine ce;
        const double inf = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double dmax = std::numeric_limits<double>::max();

        // NaN / Inf / min > max
        CHECK_THROWS_AS((void)RandX::RandReal(ce, nan, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandReal(ce, 0.0, inf), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandReal(ce, 5.0, 1.0), std::invalid_argument);
        CHECK(ce.call_count == 0);

        // 超宽区间：[-max, max] 宽度溢出
        CHECK_THROWS_AS((void)RandX::RandReal(ce, -dmax, dmax), std::invalid_argument);
        CHECK(ce.call_count == 0);

        std::vector<double> buf(5);
        CHECK_THROWS_AS(RandX::RandFill(ce, buf.begin(), buf.end(), -dmax, dmax), std::invalid_argument);
        CHECK(ce.call_count == 0);

        CHECK_THROWS_AS((void)RandX::RandVector(ce, -dmax, dmax, 5), std::invalid_argument);
        CHECK(ce.call_count == 0);

        // float 超宽区间
        const float fmax = std::numeric_limits<float>::max();
        CHECK_THROWS_AS((void)RandX::RandReal<float>(ce, -fmax, fmax), std::invalid_argument);
        CHECK(ce.call_count == 0);
    }

    TEST_CASE("正常区间严格保证半开区间界限")
    {
        RandX::Xoshiro256StarStar rng{ 999 };
        for (int i = 0; i < 5000; ++i)
        {
            double v = RandX::RandReal(rng, 10.0, 20.0);
            CHECK(v >= 10.0);
            CHECK(v < 20.0);
        }
    }
}

TEST_SUITE("EngineStatePolicy")
{
    TEST_CASE("SFC64 经确定性状态转移演化为全零快照后可精确恢复")
    {
        RandX::SFC64 rng(RandX::SFC64::state_type{ 1, 0, 0, UINT64_MAX });
        (void)rng();
        auto snapshot = rng.serialize();

        // 验证演化后的快照确实全为 0
        CHECK(snapshot[0] == 0);
        CHECK(snapshot[1] == 0);
        CHECK(snapshot[2] == 0);
        CHECK(snapshot[3] == 0);

        // 状态构造精确恢复全零，不发生静默改写
        RandX::SFC64 restored(snapshot);
        CHECK(restored.serialize() == snapshot);
        CHECK(restored == rng);

        // deserialize 精确恢复全零
        RandX::SFC64 deser_rng(12345);
        deser_rng.deserialize(snapshot);
        CHECK(deser_rng.serialize() == snapshot);
        CHECK(deser_rng == rng);

        // 后续生成序列一致
        for (int i = 0; i < 10; ++i)
        {
            auto v1 = rng();
            auto v2 = restored();
            auto v3 = deser_rng();
            CHECK(v1 == v2);
            CHECK(v2 == v3);
        }
    }

    TEST_CASE("SFC64 文本反序列化成功接受全零状态")
    {
        std::istringstream iss("0 0 0 0");
        RandX::SFC64 rng(999);
        iss >> rng;
        CHECK(!iss.fail());
        CHECK(rng.serialize() == (RandX::SFC64::state_type{ 0, 0, 0, 0 }));
    }

    TEST_CASE("吸收态引擎文本反序列化拒绝全零且旧状态不变")
    {
        // Xoshiro256StarStar
        std::istringstream iss_x256("0 0 0 0");
        RandX::Xoshiro256StarStar xrng(12345);
        auto old_x256 = xrng.serialize();
        iss_x256 >> xrng;
        CHECK(iss_x256.fail());
        CHECK(xrng.serialize() == old_x256);

        // RomuDuoJr
        std::istringstream iss_romu("0 0");
        RandX::RomuDuoJr rrng(12345);
        auto old_romu = rrng.serialize();
        iss_romu >> rrng;
        CHECK(iss_romu.fail());
        CHECK(rrng.serialize() == old_romu);
    }

    TEST_CASE("吸收态引擎裸状态入口保留全零修正语义")
    {
        RandX::Xoshiro256StarStar zero_rng(RandX::Xoshiro256StarStar::state_type{ 0, 0, 0, 0 });
        CHECK(zero_rng.serialize()[0] == 1);

        RandX::RomuDuoJr zero_romu(RandX::RomuDuoJr::state_type{ 0, 0 });
        CHECK(zero_romu.serialize()[0] == 1);
    }
}

TEST_SUITE("StreamFormatGuard")
{
    struct FailingBuffer : public std::streambuf
    {
    protected:
        int_type overflow(int_type) override
        {
            return traits_type::eof();
        }
    };

    TEST_CASE("setfill 与 setw 不产生不可解析状态且格式恢复")
    {
        std::stringstream ss;
        ss.fill('x');
        ss.width(5);
        ss.setf(std::ios_base::hex, std::ios_base::basefield);
        ss.setf(std::ios_base::showbase);

        RandX::Xoshiro256StarStar rng(12345);
        ss << rng;

        // 检查调用方的 flags 和 fill 恢复
        CHECK(ss.fill() == 'x');
        CHECK((ss.flags() & std::ios_base::basefield) == std::ios_base::hex);
        CHECK((ss.flags() & std::ios_base::showbase));

        // 反序列化成功往返
        RandX::Xoshiro256StarStar restored(1);
        ss >> restored;
        CHECK(!ss.fail());
        CHECK(rng == restored);

        // wstringstream 正常往返
        std::wstringstream wss;
        wss << rng;
        RandX::Xoshiro256StarStar w_restored(1);
        wss >> w_restored;
        CHECK(!wss.fail());
        CHECK(rng == w_restored);
    }

    TEST_CASE("输入失败触发异常掩码且恢复格式保持原状态")
    {
        // 不足字段触发 failbit 异常
        std::istringstream iss("123 456");
        iss.setf(std::ios_base::hex, std::ios_base::basefield);
        iss.unsetf(std::ios_base::skipws);
        iss.fill('*');
        iss.exceptions(std::ios_base::failbit);

        RandX::Xoshiro256StarStar rng(12345);
        auto orig_state = rng.serialize();

        CHECK_THROWS_AS(iss >> rng, std::ios_base::failure);
        CHECK(rng.serialize() == orig_state);
        CHECK(iss.fill() == '*');
        CHECK((iss.flags() & std::ios_base::basefield) == std::ios_base::hex);
        CHECK((iss.rdstate() & std::ios_base::failbit));
    }

    TEST_CASE("受控写入失败设置 badbit 并恢复格式")
    {
        FailingBuffer fb;
        std::ostream os(&fb);
        os.fill('#');
        os.setf(std::ios_base::hex, std::ios_base::basefield);

        RandX::Xoshiro256StarStar rng(12345);
        os << rng;

        CHECK(os.bad());
        CHECK(os.fill() == '#');
        CHECK((os.flags() & std::ios_base::basefield) == std::ios_base::hex);
    }

    TEST_CASE("连续写入与读取两个引擎")
    {
        RandX::Xoshiro256StarStar e1(111);
        RandX::RomuDuoJr e2(222);

        std::stringstream ss;
        ss << e1 << ' ' << e2;

        RandX::Xoshiro256StarStar r1(1);
        RandX::RomuDuoJr r2(1);

        ss >> r1 >> r2;
        CHECK(!ss.fail());
        CHECK(e1 == r1);
        CHECK(e2 == r2);
    }

    TEST_CASE("单独字段输入溢出设置 failbit 并保持原状态")
    {
        std::istringstream iss("999999999999999999999999999999999999 0 0 0");
        RandX::Xoshiro256StarStar rng(12345);
        auto orig_state = rng.serialize();

        iss >> rng;
        CHECK(iss.fail());
        CHECK(rng.serialize() == orig_state);
    }

    TEST_CASE("外部 setw 掩码不截断反序列化并在退出后恢复原 width")
    {
        RandX::Xoshiro256StarStar orig(98765);
        std::stringstream ss;
        ss << orig;

        RandX::Xoshiro256StarStar restored(1);
        ss.width(2);
        ss >> restored;

        CHECK(!ss.fail());
        CHECK(orig == restored);
        CHECK(ss.width() == 2);
    }

    struct ThrowingWidenFacet : public std::ctype<wchar_t>
    {
    protected:
        wchar_t do_widen(char) const override
        {
            throw std::runtime_error("ctype widen failure");
        }
    };

    struct ThrowingFillStream : public std::basic_ios<wchar_t>
    {
        explicit ThrowingFillStream(const std::locale& loc)
        {
            this->init(nullptr);
            this->imbue(loc);
#if defined(__GLIBCXX__)
            this->_M_fill_init = false;
#endif
        }
    };

    TEST_CASE("构造期间获取 fill 抛出异常时可被捕获且不导致进程终止")
    {
        std::locale loc(std::locale::classic(), new ThrowingWidenFacet);
        ThrowingFillStream os(loc);

        bool threw = false;
        try
        {
            RandX::detail::StreamFormatGuard<wchar_t, std::char_traits<wchar_t>> guard(os);
        }
        catch (const std::runtime_error& e)
        {
            threw = true;
            CHECK(std::string(e.what()) == "ctype widen failure");
        }
        CHECK(threw);
    }
}

TEST_SUITE("BetaDistributionScale")
{
    TEST_CASE("基本与非对称参数统计特性检验")
    {
        RandX::Xoshiro256StarStar rng{ 42 };
        constexpr int N = 20000;

        auto test_beta_stats = [&](double a, double b) {
            double sum = 0.0;
            double sum_sq = 0.0;
            for (int i = 0; i < N; ++i)
            {
                double val = RandX::RandBeta(rng, a, b);
                CHECK(val >= 0.0);
                CHECK(val <= 1.0);
                sum += val;
                sum_sq += val * val;
            }
            double mean = sum / N;
            double var = (sum_sq / N) - (mean * mean);

            double expected_mean = a / (a + b);
            double expected_var = (a * b) / ((a + b) * (a + b) * (a + b + 1.0));
            double se_mean = std::sqrt(expected_var / N);

            // 4 个标准误容差
            CHECK(std::abs(mean - expected_mean) < 4.0 * se_mean);
            CHECK(std::abs(var - expected_var) < 0.05);
        };

        test_beta_stats(2.0, 2.0);
        test_beta_stats(0.5, 0.5);
        test_beta_stats(2.0, 5.0);
        test_beta_stats(5.0, 2.0);
    }

    TEST_CASE("float 在 2^46 阈值附近保留随机波动非固定均值")
    {
        RandX::Xoshiro256StarStar rng{ 12345 };
        const float a = 7.0368744e13f; // ~2^46
        const float b = 7.0368744e13f;

        std::set<float> distinct_samples;
        float sum = 0.0f;
        for (int i = 0; i < 500; ++i)
        {
            float val = RandX::RandBeta(rng, a, b);
            CHECK(val >= 0.0f);
            CHECK(val <= 1.0f);
            distinct_samples.insert(val);
            sum += val;
        }

        // 不应全部退化为唯一常数 0.5f
        CHECK(distinct_samples.size() > 1);

        float mean = sum / 500.0f;
        CHECK(std::abs(mean - 0.5f) < 1e-3f);
    }

    TEST_CASE("double 在 2^104 阈值附近及其前后正常采样")
    {
        RandX::Xoshiro256StarStar rng{ 777 };
        const double a = 2.028240960365167e31; // ~2^104
        const double b = 2.028240960365167e31;

        double sum = 0.0;
        for (int i = 0; i < 200; ++i)
        {
            double val = RandX::RandBeta(rng, a, b);
            CHECK(val >= 0.0);
            CHECK(val <= 1.0);
            sum += val;
        }

        double mean = sum / 200.0;
        CHECK(std::abs(mean - 0.5) < 1e-4);
    }

    TEST_CASE("极大有限量级与混合尺度")
    {
        RandX::Xoshiro256StarStar rng{ 999 };

        // 极大对称量级 1e300
        for (int i = 0; i < 50; ++i)
        {
            double val = RandX::RandBeta(rng, 1e300, 1e300);
            CHECK(std::isfinite(val));
            CHECK(val >= 0.0);
            CHECK(val <= 1.0);
        }

        // 极大非对称量级 1e300 与 2e300，理论均值 1/3
        double sum_asym = 0.0;
        for (int i = 0; i < 100; ++i)
        {
            double val = RandX::RandBeta(rng, 1e300, 2e300);
            CHECK(std::isfinite(val));
            CHECK(val >= 0.0);
            CHECK(val <= 1.0);
            sum_asym += val;
        }
        CHECK(std::abs((sum_asym / 100.0) - (1.0 / 3.0)) < 1e-4);

        // 混合尺度 (mixed-small-large): 0.5 与 1e50
        for (int i = 0; i < 50; ++i)
        {
            double val = RandX::RandBeta(rng, 0.5, 1e50);
            CHECK(std::isfinite(val));
            CHECK(val >= 0.0);
            CHECK(val < 1e-10);
        }

        // 混合尺度反向: 1e50 与 0.5
        for (int i = 0; i < 50; ++i)
        {
            double val = RandX::RandBeta(rng, 1e50, 0.5);
            CHECK(std::isfinite(val));
            CHECK(val > 1.0 - 1e-10);
            CHECK(val <= 1.0);
        }
    }

    TEST_CASE("RandBeta 极不对称参数与浮点下溢边界测试")
    {
        RandX::Xoshiro256StarStar rng{ 12345 };

        // 验证 1.0 与 1e303, 1e306, 1e308 的输出为可表示的非零正数
        for (int i = 0; i < 50; ++i)
        {
            double val303 = RandX::RandBeta(rng, 1.0, 1e303);
            CHECK(std::isfinite(val303));
            CHECK(val303 > 0.0);
            CHECK(val303 < 1e-300);

            double val306 = RandX::RandBeta(rng, 1.0, 1e306);
            CHECK(std::isfinite(val306));
            CHECK(val306 > 0.0);
            CHECK(val306 < 1e-300);

            double val308 = RandX::RandBeta(rng, 1.0, 1e308);
            CHECK(std::isfinite(val308));
            CHECK(val308 > 0.0);
            CHECK(val308 < 1e-300);
        }

        // 验证对数域 helper 避免除法直接溢出 (d_a = 2/3, d_b = DBL_MAX)
        double helper_val = RandX::detail::ComputeBetaSampleFromLogScale(
            2.0 / 3.0, 0.0, std::numeric_limits<double>::max(), 0.0);
        CHECK(std::isfinite(helper_val));
        CHECK(helper_val > 0.0);

        // 验证双小尺度参数走对数尺度不抛异常且输出合法
        for (int i = 0; i < 20; ++i)
        {
            double val_small1 = RandX::RandBeta(rng, 1e-10, 1e-10);
            CHECK(std::isfinite(val_small1));
            CHECK(val_small1 >= 0.0);
            CHECK(val_small1 <= 1.0);

            double val_small2 = RandX::RandBeta(rng, 1e-100, 1e-100);
            CHECK(std::isfinite(val_small2));
            CHECK(val_small2 >= 0.0);
            CHECK(val_small2 <= 1.0);
        }

#if defined(LDBL_MAX_10_EXP) && (LDBL_MAX_10_EXP > 308)
        // 若平台支持扩展精度 long double，验证更大指数范围
        for (int i = 0; i < 20; ++i)
        {
            long double val400 = RandX::RandBeta(rng, 1.0L, 1e400L);
            CHECK(std::isfinite(val400));
            CHECK(val400 > 0.0L);
        }
#endif
    }

    TEST_CASE("非法形状参数抛出 invalid_argument")
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        CHECK_THROWS_AS((void)RandX::RandBeta(0.0, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(1.0, -1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(nan, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(1.0, inf), std::invalid_argument);
    }
}


