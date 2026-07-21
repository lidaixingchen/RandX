// test_randx.cpp — RandX.hpp (C++23) 确定性单元测试（doctest 版）
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "RandX.hpp"

#include <array>
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <list>
#include <ranges>
#include <sstream>
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
}

// ============================================================================
// 已知序列断言（seed=12345）— 14 引擎 KAT
// ============================================================================
TEST_SUITE("已知序列 seed=12345")
{
    TEST_CASE("Xoshiro256StarStar")
    {
        RandX::Xoshiro256StarStar rng{ 12345 };
        const std::uint64_t expected[] = {
            13720838825685603483ULL, 2398916695208396998ULL,
            17770384849984869256ULL, 891717726879801395ULL,
            10241316046318454344ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("Xoshiro256PlusPlus")
    {
        RandX::Xoshiro256PlusPlus rng{ 12345 };
        const std::uint64_t expected[] = {
            10201931350592234856ULL, 3780764549115216544ULL,
            1570246627180645737ULL, 3237956550421933520ULL,
            4899705286669081817ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("Xoshiro256Plus")
    {
        RandX::Xoshiro256Plus rng{ 12345 };
        const std::uint64_t expected[] = {
            5703686706282124394ULL, 15181128508879479020ULL,
            11713703072819584576ULL, 2395620858144650628ULL,
            8055391375587558944ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("Xoroshiro128PlusPlus")
    {
        RandX::Xoroshiro128PlusPlus rng{ 12345 };
        const std::uint64_t expected[] = {
            16181086164699823776ULL, 14214852713950817264ULL,
            5918739589371211168ULL, 10279317896082661690ULL,
            3028767600443116799ULL
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("Xoshiro128StarStar")
    {
        RandX::Xoshiro128StarStar rng{ 12345 };
        const std::uint32_t expected[] = {
            1096865841U, 933661059U, 3314798965U, 1305642763U, 1040785987U
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("Xoroshiro64StarStar")
    {
        RandX::Xoroshiro64StarStar rng{ 12345 };
        const std::uint32_t expected[] = {
            63958076U, 2181105171U, 532052331U, 3458610118U, 2965685819U
        };
        for (auto e : expected)
            CHECK(rng() == e);
    }

    TEST_CASE("SplitMix64")
    {
        RandX::SplitMix64 rng{ 12345 };
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
        RandX::SFC64 rng{ 12345 };
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
        RandX::RomuDuoJr rng{ 12345 };
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
        auto s0 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(0, 12345);
        auto s1 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(1, 12345);
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

    TEST_CASE("discard 与连续调用等价")
    {
        RandX::Xoshiro256StarStar a{ 42 };
        RandX::Xoshiro256StarStar b{ 42 };
        a.discard(100);
        for (int i = 0; i < 100; ++i) b();
        CHECK(a() == b());
    }

    TEST_CASE("operator<=> 相等性")
    {
        RandX::Xoshiro256StarStar a{ 1 }, b{ 1 }, c{ 2 };
        CHECK(a == b);
        CHECK(a != c);
    }

    TEST_CASE("jump / longJump 不崩溃")
    {
        RandX::Xoshiro256StarStar rng{ 777 };
        rng.jump();
        rng.longJump();
        (void)rng();
    }

    TEST_CASE("std::seed_seq 播种确定性")
    {
        std::seed_seq seq{ 1, 2, 3, 4, 5, 6, 7, 8 };
        RandX::Xoshiro256StarStar rng1{ seq };
        std::seed_seq seq2{ 1, 2, 3, 4, 5, 6, 7, 8 };
        RandX::Xoshiro256StarStar rng2{ seq2 };
        CHECK(rng1() == rng2());
        CHECK(rng1() == rng2());
    }
}

// ============================================================================
// 编译期 API（C++23 专属；RandIntCE 依赖 __uint128_t,MSVC 跳过）
// ============================================================================
TEST_SUITE("编译期 API")
{
#if !defined(_MSC_VER) || defined(__SIZEOF_INT128__)
    TEST_CASE("RandIntCE constexpr")
    {
        constexpr int v = RandX::RandIntCE(0, 100);
        static_assert(v == 61);
        static_assert(RandX::RandIntCE<int, 42>(1, 6) >= 1);
        static_assert(RandX::RandIntCE<int, 42>(1, 6) <= 6);
        CHECK(v == 61);
    }
#endif

    TEST_CASE("ShuffleCE constexpr 洗牌")
    {
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
            int v = RandX::RandInt(1, 6);
            CHECK(v >= 1);
            CHECK(v <= 6);
        }
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            double v = RandX::RandReal();
            CHECK(v >= 0.0);
            CHECK(v < 1.0);
        }
    }

    TEST_CASE("RandNormal 均值接近 0")
    {
        double sum = 0;
        for (int i = 0; i < MC_TRIALS_10K; ++i)
            sum += RandX::RandNormal(0.0, 1.0);
        double mean = sum / MC_TRIALS_10K;
        CHECK(mean > -0.1);
        CHECK(mean < 0.1);
    }

    TEST_CASE("RandShuffle 保持元素集合")
    {
        std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        std::vector<int> orig = v;
        RandX::RandShuffle(v);
        CHECK(v != orig);
        std::sort(v.begin(), v.end());
        CHECK(v == orig);
    }

    TEST_CASE("RandWeighted 零权重不被选中")
    {
        std::vector<double> weights = { 0.0, 0.0, 1.0, 0.0 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandWeighted(weights) == 2);
    }

    TEST_CASE("RandElement 容器版返回元素引用")
    {
        std::array<int, 3> arr = { 10, 20, 30 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            int v = RandX::RandElement(arr);
            CHECK((v == 10 || v == 20 || v == 30));
        }
    }

    TEST_CASE("Reseed 可重播种确定性")
    {
        RandX::Reseed(42);
        const auto a = RandX::RandInt(0, 1000000);
        RandX::Reseed(42);
        const auto b = RandX::RandInt(0, 1000000);
        CHECK(a == b);
    }

    TEST_CASE("扩展便捷 API 集成")
    {
        // RandSample：无放回抽样
        std::vector<int> pool = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        auto sample = RandX::RandSample(pool, 3);
        CHECK(sample.size() == 3);
        for (const auto& x : sample)
            CHECK(std::find(pool.begin(), pool.end(), x) != pool.end());
        CHECK((sample[0] != sample[1] || sample[1] != sample[2] || sample[0] != sample[2]));

        // RandPermutation：随机排列
        auto perm = RandX::RandPermutation(10);
        CHECK(perm.size() == 10);
        auto sorted_perm = perm;
        std::sort(sorted_perm.begin(), sorted_perm.end());
        for (std::size_t i = 0; i < 10; ++i)
            CHECK(sorted_perm[i] == static_cast<int>(i));

        // RandString：随机字符串
        auto str = RandX::RandString(16);
        CHECK(str.size() == 16);
        auto str2 = RandX::RandString(8, "01");
        CHECK(str2.size() == 8);
        for (const auto& c : str2) CHECK((c == '0' || c == '1'));

        // RandExp：指数分布（验证非负）
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandExp() >= 0.0);

        // RandPoisson：泊松分布（验证非负）
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandPoisson() >= 0);

        // RandGamma：伽马分布（验证正数）
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandGamma() > 0.0);

        // RandBits：N 位随机数
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            CHECK(RandX::RandBits<8>() < 256);
            CHECK(RandX::RandBits<1>() < 2);
        }

        // RandUUID：格式验证
        auto uuid = RandX::RandUUID();
        CHECK(uuid.size() == 36);
        CHECK(uuid[8] == '-');
        CHECK(uuid[13] == '-');
        CHECK(uuid[18] == '-');
        CHECK(uuid[23] == '-');
        CHECK(uuid[14] == '4');  // 版本号
        CHECK((uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b'));
    }

    // ============================================================
    // 新增分布（v1.2）：Bernoulli/Binomial/LogNormal/Geometric/
    // Cauchy/Weibull/ExtremeValue/ChiSquared/StudentT/FisherF/Beta
    // ============================================================
    TEST_CASE("RandBernoulli 与 RandBool 引擎重载等价")
    {
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandBernoulli(rng1, 0.3) == RandX::RandBool(rng2, 0.3));
    }

    TEST_CASE("RandBinomial 范围与均值")
    {
        // t=60, p=0.5：理论均值 30，方差 15
        double sum = 0;
        for (int i = 0; i < MC_TRIALS_10K; ++i)
        {
            int v = RandX::RandBinomial(60, 0.5);
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
            CHECK(RandX::RandLogNormal(0.0, 1.0) > 0.0);
    }

    TEST_CASE("RandGeometric 非负")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandGeometric(0.3) >= 0);
    }

    TEST_CASE("RandCauchy 有限值占绝大多数")
    {
        // Cauchy 重尾，理论上 P(|x|<1e6)≈0.99968，inf 极罕见
        int finiteCount = 0;
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            const double v = RandX::RandCauchy(0.0, 1.0);
            if (std::isfinite(v)) ++finiteCount;
        }
        CHECK(finiteCount >= MC_TRIALS_100 - 5);
    }

    TEST_CASE("RandWeibull 非负")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandWeibull(2.0, 1.0) >= 0.0);
    }

    TEST_CASE("RandExtremeValue 有限值")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            const double v = RandX::RandExtremeValue(0.0, 1.0);
            CHECK(std::isfinite(v));
        }
    }

    TEST_CASE("RandChiSquared 非负")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandChiSquared(4.0) >= 0.0);
    }

    TEST_CASE("RandStudentT 有限值占绝大多数")
    {
        // 自由度 4：有限方差但重尾
        int finiteCount = 0;
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            const double v = RandX::RandStudentT(4.0);
            if (std::isfinite(v)) ++finiteCount;
        }
        CHECK(finiteCount >= MC_TRIALS_100 - 5);
    }

    TEST_CASE("RandFisherF 非负")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
            CHECK(RandX::RandFisherF(5.0, 5.0) >= 0.0);
    }

    TEST_CASE("RandBeta 范围 [0,1] 与均值")
    {
        // a=2, b=2：理论均值 0.5，方差 0.05
        double sum = 0;
        for (int i = 0; i < MC_TRIALS_10K; ++i)
        {
            const double v = RandX::RandBeta(2.0, 2.0);
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
        RandX::Xoshiro256StarStar rng1{ 12345 }, rng2{ 12345 };
        CHECK(RandX::RandBinomial(rng1, 10, 0.5) == RandX::RandBinomial(rng2, 10, 0.5));
        CHECK(RandX::RandLogNormal(rng1, 0.0, 1.0) == RandX::RandLogNormal(rng2, 0.0, 1.0));
        CHECK(RandX::RandGeometric(rng1, 0.3) == RandX::RandGeometric(rng2, 0.3));
        CHECK(RandX::RandCauchy(rng1, 0.0, 1.0) == RandX::RandCauchy(rng2, 0.0, 1.0));
        CHECK(RandX::RandWeibull(rng1, 2.0, 1.0) == RandX::RandWeibull(rng2, 2.0, 1.0));
        CHECK(RandX::RandExtremeValue(rng1, 0.0, 1.0) == RandX::RandExtremeValue(rng2, 0.0, 1.0));
        CHECK(RandX::RandChiSquared(rng1, 4.0) == RandX::RandChiSquared(rng2, 4.0));
        CHECK(RandX::RandStudentT(rng1, 4.0) == RandX::RandStudentT(rng2, 4.0));
        CHECK(RandX::RandFisherF(rng1, 5.0, 5.0) == RandX::RandFisherF(rng2, 5.0, 5.0));
        CHECK(RandX::RandBeta(rng1, 2.0, 2.0) == RandX::RandBeta(rng2, 2.0, 2.0));
    }
}

// ============================================================================
// 新增 API（C++23）：RandChar / RandElement 迭代器版 / RandFill / RandVector / operator<<>> 
// ============================================================================
TEST_SUITE("新增 API")
{
    TEST_CASE("RandChar 范围与类型")
    {
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char c = RandX::RandChar('a', 'z');
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            wchar_t wc = RandX::RandChar<wchar_t>(L'a', L'z');
            CHECK(wc >= L'a');
            CHECK(wc <= L'z');
        }
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char16_t c16 = RandX::RandChar<char16_t>(u'a', u'z');
            CHECK(c16 >= u'a');
            CHECK(c16 <= u'z');
        }
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char32_t c32 = RandX::RandChar<char32_t>(U'a', U'z');
            CHECK(c32 >= U'a');
            CHECK(c32 <= U'z');
        }
        // RandChar(max) 单参版
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char c = RandX::RandChar<char>('z');
            CHECK(c >= char{});
            CHECK(c <= 'z');
        }
    }

#if defined(__cpp_char8_t) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    TEST_CASE("RandChar char8_t（C++20+ 条件启用）")
    {
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char8_t c = RandX::RandChar<char8_t>(u8'a', u8'z');
            CHECK(c >= u8'a');
            CHECK(c <= u8'z');
        }
    }
#endif

    TEST_CASE("RandChar 引擎重载")
    {
        RandX::Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char c = RandX::RandChar<char>(rng, 'a', 'z');
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            char c = RandX::RandChar<char>(rng, 'z');
            CHECK(c >= char{});
            CHECK(c <= 'z');
        }
    }

    TEST_CASE("RandElement 迭代器版（随机访问）")
    {
        std::vector<int> v = { 10, 20, 30, 40, 50 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            auto it = RandX::RandElement(v.begin(), v.end());
            CHECK(it >= v.begin());
            CHECK(it < v.end());
            CHECK(*it >= 10);
            CHECK(*it <= 50);
        }
        // 引擎重载
        RandX::Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            auto it = RandX::RandElement(rng, v.begin(), v.end());
            CHECK(it >= v.begin());
            CHECK(it < v.end());
        }
    }

    TEST_CASE("RandElement 迭代器版（输入迭代器 reservoir sampling）")
    {
        std::list<int> l = { 100, 200, 300, 400, 500 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            auto it = RandX::RandElement(l.begin(), l.end());
            CHECK(*it >= 100);
            CHECK(*it <= 500);
        }
        // 引擎重载
        RandX::Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            auto it = RandX::RandElement(rng, l.begin(), l.end());
            CHECK(*it >= 100);
            CHECK(*it <= 500);
        }
    }

    TEST_CASE("RandElement 空范围抛异常")
    {
        std::vector<int> empty;
        CHECK_THROWS_AS((void)RandX::RandElement(empty.begin(), empty.end()), std::invalid_argument);
    }

    TEST_CASE("RandFill 填充范围")
    {
        std::array<int, 100> arr{};
        RandX::RandFill(arr.begin(), arr.end(), 0, 99);
        for (const auto& v : arr)
        {
            CHECK(v >= 0);
            CHECK(v <= 99);
        }
        // 浮点版
        std::vector<double> v(100);
        RandX::RandFill(v.begin(), v.end(), 0.0, 1.0);
        for (const auto& d : v)
        {
            CHECK(d >= 0.0);
            CHECK(d < 1.0);
        }
        // 引擎重载
        RandX::Xoshiro256StarStar rng{ 12345 };
        std::vector<int> v2(100);
        RandX::RandFill(rng, v2.begin(), v2.end(), 1, 10);
        for (const auto& i : v2)
        {
            CHECK(i >= 1);
            CHECK(i <= 10);
        }
    }

    TEST_CASE("RandVector 生成 vector")
    {
        auto vi = RandX::RandVector(0, 99, 100);
        CHECK(vi.size() == 100);
        for (const auto& v : vi)
        {
            CHECK(v >= 0);
            CHECK(v <= 99);
        }
        auto vd = RandX::RandVector(0.0, 1.0, 100);
        CHECK(vd.size() == 100);
        for (const auto& v : vd)
        {
            CHECK(v >= 0.0);
            CHECK(v < 1.0);
        }
        // 引擎重载
        RandX::Xoshiro256StarStar rng{ 12345 };
        auto vi2 = RandX::RandVector(rng, 1, 6, 50);
        CHECK(vi2.size() == 50);
        for (const auto& v : vi2)
        {
            CHECK(v >= 1);
            CHECK(v <= 6);
        }
        auto vd2 = RandX::RandVector(rng, 0.0, 10.0, 50);
        CHECK(vd2.size() == 50);
        for (const auto& v : vd2)
        {
            CHECK(v >= 0.0);
            CHECK(v < 10.0);
        }
    }

    TEST_CASE("operator<< / operator>> 流式序列化")
    {
        RandX::Xoshiro256StarStar rng1{ 12345 };
        // 推进若干步
        for (int i = 0; i < 10; ++i) (void)rng1();

        std::stringstream ss;
        ss << rng1;
        CHECK((ss.good() || ss.eof()));

        RandX::Xoshiro256StarStar rng2{ 99999 };
        ss >> rng2;
        CHECK((ss.good() || ss.eof()));

        // 恢复后两个引擎状态一致，后续输出相同
        CHECK(rng1 == rng2);
        CHECK(rng1() == rng2());
    }

    TEST_CASE("operator>> 失败时设置 failbit 且状态不变")
    {
        RandX::Xoshiro256StarStar rng1{ 12345 };
        RandX::Xoshiro256StarStar rng2{ 12345 };
        // 推进若干步，使状态非默认
        for (int i = 0; i < 5; ++i) (void)rng1(), (void)rng2();

        std::stringstream ss("not a number");  // 故意错误输入
        ss >> rng1;
        CHECK((ss.failbit & ss.rdstate()) != 0);
        // 状态未改变
        CHECK(rng1 == rng2);
    }

    TEST_CASE("operator<< 不应匹配 SplitMix64（标量 state_type）")
    {
        // 此测试主要保证编译期 SFINAE 排除标量引擎
        // 如果 SFINAE 失败，下行将触发 static_assert
        static_assert(!RandX::detail::SerializableEngine<RandX::SplitMix64>);
        static_assert(RandX::detail::SerializableEngine<RandX::Xoshiro256StarStar>);
    }
}

// ============================================================================
// RandSample 迭代器版（v1.2 新增）
// ============================================================================
TEST_SUITE("RandSample 迭代器版")
{
    TEST_CASE("随机访问路径（索引分支）：n²>=size 走索引数组")
    {
        std::vector<int> v = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        // n=5, size=10, n²=25 >= 10 → 索引数组分支
        auto sample = RandX::RandSample(v.begin(), v.end(), 5);
        CHECK(sample.size() == 5);
        for (int x : sample)
        {
            CHECK(x >= 0);
            CHECK(x <= 9);
            // 无放回：检查不重复
            CHECK(std::count(sample.begin(), sample.end(), x) == 1);
        }
    }

    TEST_CASE("随机访问路径（hash-set 分支）：n²<size 走 hash-set")
    {
        std::vector<int> v(10000);
        for (int i = 0; i < 10000; ++i) v[static_cast<std::size_t>(i)] = i;
        // n=5, size=10000, n²=25 < 10000 → hash-set 分支
        auto sample = RandX::RandSample(v.begin(), v.end(), 5);
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
        auto sample = RandX::RandSample(lst.begin(), lst.end(), 3);
        CHECK(sample.size() == 3);
        for (int x : sample)
        {
            bool found = false;
            for (int e : lst) if (e == x) { found = true; break; }
            CHECK(found);
        }
        // 无放回：不重复
        std::sort(sample.begin(), sample.end());
        for (std::size_t i = 1; i < sample.size(); ++i)
            CHECK(sample[i] != sample[i - 1]);
    }

    TEST_CASE("流式输入：std::istream_iterator 走 reservoir")
    {
        std::istringstream iss("1 2 3 4 5 6 7 8 9 10");
        std::istream_iterator<int> first(iss);
        std::istream_iterator<int> last;
        auto sample = RandX::RandSample(first, last, 3);
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
        auto s1 = RandX::RandSample(v.begin(), v.end(), 0);
        CHECK(s1.empty());
        std::list<int> lst = { 1, 2, 3 };
        auto s2 = RandX::RandSample(lst.begin(), lst.end(), 0);
        CHECK(s2.empty());
    }

    TEST_CASE("边界：n>=size 返回全部")
    {
        std::vector<int> v = { 1, 2, 3, 4, 5 };
        auto s1 = RandX::RandSample(v.begin(), v.end(), 5);
        CHECK(s1.size() == 5);
        auto s2 = RandX::RandSample(v.begin(), v.end(), 10);
        CHECK(s2.size() == 5);
        std::list<int> lst = { 10, 20, 30 };
        auto s3 = RandX::RandSample(lst.begin(), lst.end(), 3);
        CHECK(s3.size() == 3);
        // 元素不足 n：返回所有已收集元素
        auto s4 = RandX::RandSample(lst.begin(), lst.end(), 10);
        CHECK(s4.size() == 3);
    }

    TEST_CASE("边界：空范围返回空")
    {
        std::vector<int> empty;
        auto s1 = RandX::RandSample(empty.begin(), empty.end(), 3);
        CHECK(s1.empty());
        std::list<int> emptyLst;
        auto s2 = RandX::RandSample(emptyLst.begin(), emptyLst.end(), 3);
        CHECK(s2.empty());
    }

    TEST_CASE("引擎重载确定性（随机访问）")
    {
        std::vector<int> v(1000);
        for (int i = 0; i < 1000; ++i) v[static_cast<std::size_t>(i)] = i;
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        auto s1 = RandX::RandSample(rng1, v.begin(), v.end(), 10);
        auto s2 = RandX::RandSample(rng2, v.begin(), v.end(), 10);
        CHECK(s1 == s2);
    }

    TEST_CASE("引擎重载确定性（输入迭代器 reservoir）")
    {
        std::list<int> lst = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19 };
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        auto s1 = RandX::RandSample(rng1, lst.begin(), lst.end(), 5);
        auto s2 = RandX::RandSample(rng2, lst.begin(), lst.end(), 5);
        CHECK(s1 == s2);
    }

    TEST_CASE("均匀性（reservoir）：前后半段均衡，验证 i+1 修复")
    {
        // N=200, n=10, TRIALS=10000；期望各半段被选 50000 次
        // 修复前 BoundedRand(rng,i) 会使后半段过选；修复后 BoundedRand(rng,i+1) 均匀
        constexpr int N = 200;
        constexpr int n = 10;
        constexpr int TRIALS = 10000;
        std::vector<int> v(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i) v[static_cast<std::size_t>(i)] = i;
        int firstHalf = 0, secondHalf = 0;
        for (int t = 0; t < TRIALS; ++t)
        {
            auto s = RandX::RandSample(v.begin(), v.end(), n);
            for (int x : s)
            {
                if (x < N / 2) ++firstHalf;
                else ++secondHalf;
            }
        }
        // ±10% 容差（修复前差值极大，可稳定捕获 BUG）
        CHECK(firstHalf > 45000);
        CHECK(firstHalf < 55000);
        CHECK(secondHalf > 45000);
        CHECK(secondHalf < 55000);
    }

    TEST_CASE("均匀性（hash-set）：前后半段均衡")
    {
        // n² < size 强制走 hash-set 分支：N=10000, n=5, n²=25<10000
        constexpr int N = 10000;
        constexpr int n = 5;
        constexpr int TRIALS = 10000;
        std::vector<int> v(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i) v[static_cast<std::size_t>(i)] = i;
        int firstHalf = 0, secondHalf = 0;
        for (int t = 0; t < TRIALS; ++t)
        {
            auto s = RandX::RandSample(v.begin(), v.end(), n);
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

        RandX::Xoshiro256StarStar rng{ 98765 };
        std::array<int, STAT_BINS_100> counts{};
        for (int i = 0; i < STAT_N; ++i)
            ++counts[RandX::RandInt(rng, 0, STAT_BINS_100 - 1)];

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

        RandX::SFC64 rng{ 54321 };
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

        RandX::RomuDuoJr rng{ 54321 };
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
// ranges 风格 API（C++23 专属）
// ============================================================================
TEST_SUITE("ranges 风格 API")
{
    TEST_CASE("RandElement 容器直传返回值拷贝")
    {
        std::vector<int> v = { 10, 20, 30, 40, 50 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            int x = RandX::ranges::RandElement(v);
            CHECK((x == 10 || x == 20 || x == 30 || x == 40 || x == 50));
        }
    }

    TEST_CASE("RandElement 与 views::filter 组合")
    {
        std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        auto evens = v | std::views::filter([](int x) { return x % 2 == 0; });
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            int x = RandX::ranges::RandElement(evens);
            CHECK(x % 2 == 0);
            CHECK(x >= 2);
            CHECK(x <= 10);
        }
    }

    TEST_CASE("RandSample 容器直传")
    {
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

    TEST_CASE("RandShuffle 容器直传保持元素集合")
    {
        std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        std::vector<int> orig = v;
        RandX::ranges::RandShuffle(v);
        // 排序后应与原集合一致
        std::sort(v.begin(), v.end());
        CHECK(v == orig);
    }

    TEST_CASE("RandFill 模板推导 int")
    {
        std::vector<int> buf(100);
        RandX::ranges::RandFill(buf, 0, 99);
        for (int x : buf)
        {
            CHECK(x >= 0);
            CHECK(x <= 99);
        }
    }

    TEST_CASE("RandFill 模板推导 double")
    {
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
        std::vector<float> buf(100);
        RandX::ranges::RandFill(buf, 0.0f, 1.0f);
        for (float x : buf)
        {
            CHECK(x >= 0.0f);
            CHECK(x <= 1.0f);
        }
    }
}

// ============================================================================
// RandChar / RandString 预设字符集（v1.2 新增）
// ============================================================================
TEST_SUITE("RandChar 预设字符集")
{
    TEST_CASE("RandChar(CharSet::Lower) 全部属于 [a-z]")
    {
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char c = RandX::RandChar(RandX::CharSet::Lower);
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }
    }

    TEST_CASE("RandChar(CharSet::Hex) 全部属于 [0-9a-fA-F]")
    {
        const std::string hexSet = "0123456789abcdefABCDEF";
        for (int i = 0; i < MC_TRIALS_1K; ++i)
        {
            char c = RandX::RandChar(RandX::CharSet::Hex);
            CHECK(hexSet.find(c) != std::string::npos);
        }
    }

    TEST_CASE("RandChar 8 种字符集全覆盖")
    {
        using CS = RandX::CharSet;
        const std::string alnum = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        const std::string alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        const std::string lower = "abcdefghijklmnopqrstuvwxyz";
        const std::string upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const std::string digit = "0123456789";
        const std::string hexS = "0123456789abcdefABCDEF";
        const std::string printable = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            CHECK(alnum.find(RandX::RandChar(CS::Alphanumeric)) != std::string::npos);
            CHECK(alpha.find(RandX::RandChar(CS::Alpha)) != std::string::npos);
            CHECK(lower.find(RandX::RandChar(CS::Lower)) != std::string::npos);
            CHECK(upper.find(RandX::RandChar(CS::Upper)) != std::string::npos);
            CHECK(digit.find(RandX::RandChar(CS::Digit)) != std::string::npos);
            CHECK(hexS.find(RandX::RandChar(CS::Hex)) != std::string::npos);
            CHECK(printable.find(RandX::RandChar(CS::Printable)) != std::string::npos);
            CHECK(base64.find(RandX::RandChar(CS::Base64)) != std::string::npos);
        }
    }

    TEST_CASE("RandChar 引擎重载确定性")
    {
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            CHECK(RandX::RandChar(rng1, RandX::CharSet::Hex)
                == RandX::RandChar(rng2, RandX::CharSet::Hex));
        }
    }

    TEST_CASE("RandChar 引擎重载支持非 Xoshiro256StarStar 引擎")
    {
        // 验证 RandChar(Engine&, CharSet) 对 SFC64 / RomuDuoJr 等任意引擎均可编译且确定性成立
        RandX::SFC64 sfc1{ 42 }, sfc2{ 42 };
        RandX::RomuDuoJr romu1{ 42 }, romu2{ 42 };
        for (int i = 0; i < MC_TRIALS_100; ++i)
        {
            CHECK(RandX::RandChar(sfc1, RandX::CharSet::Hex)
                == RandX::RandChar(sfc2, RandX::CharSet::Hex));
            CHECK(RandX::RandChar(romu1, RandX::CharSet::Lower)
                == RandX::RandChar(romu2, RandX::CharSet::Lower));
        }
    }

    TEST_CASE("RandString 引擎重载确定性与字符集")
    {
        // 验证 RandString(Engine&, n, CharSet) 引擎重载
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        const std::string hexSet = "0123456789abcdefABCDEF";
        auto s1 = RandX::RandString(rng1, 32, RandX::CharSet::Hex);
        auto s2 = RandX::RandString(rng2, 32, RandX::CharSet::Hex);
        CHECK(s1.size() == 32);
        CHECK(s1 == s2);
        for (char c : s1)
            CHECK(hexSet.find(c) != std::string::npos);

        // 非默认引擎也应可工作
        RandX::SFC64 sfc{ 7 };
        auto s3 = RandX::RandString(sfc, 16, RandX::CharSet::Base64);
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        CHECK(s3.size() == 16);
        for (char c : s3)
            CHECK(base64.find(c) != std::string::npos);
    }

    TEST_CASE("RandString(n, CharSet::Digit) 全部属于 [0-9]")
    {
        const std::string digit = "0123456789";
        auto s = RandX::RandString(100, RandX::CharSet::Digit);
        CHECK(s.size() == 100);
        for (char c : s)
            CHECK(digit.find(c) != std::string::npos);
    }

    TEST_CASE("RandString(16, CharSet::Hex) 长度与字符集")
    {
        const std::string hexSet = "0123456789abcdefABCDEF";
        auto s = RandX::RandString(16, RandX::CharSet::Hex);
        CHECK(s.size() == 16);
        for (char c : s)
            CHECK(hexSet.find(c) != std::string::npos);
    }

    TEST_CASE("RandString(12, CharSet::Base64) RFC 4648 合规")
    {
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        auto s = RandX::RandString(12, RandX::CharSet::Base64);
        CHECK(s.size() == 12);
        for (char c : s)
            CHECK(base64.find(c) != std::string::npos);
    }

    TEST_CASE("RandChar(CharSet::Hex) 均匀性 ±3σ")
    {
        // Hex 字符集 = "0123456789abcdefABCDEF" 共 22 个字符
        // 各字符期望频率 1/22，N=22000 采样，每字符期望 1000 次
        // σ = sqrt(N * p * (1-p)) = sqrt(22000 * 1/22 * 21/22) ≈ 30.91
        const std::string hexSet = "0123456789abcdefABCDEF";
        const std::size_t cat = hexSet.size();
        constexpr int N = 22000;
        const int expected = N / static_cast<int>(cat);
        const double p = 1.0 / static_cast<double>(cat);
        const double sigma = std::sqrt(static_cast<double>(N) * p * (1.0 - p));
        std::vector<int> counts(cat, 0);
        for (int i = 0; i < N; ++i)
        {
            char c = RandX::RandChar(RandX::CharSet::Hex);
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
