#ifndef RANDX_TESTS_COMMON_CONVENIENCE_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_CONVENIENCE_CONTRACTS_HPP

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

TEST_SUITE("公共/基础/便捷接口")
{
    TEST_CASE("便捷接口参数契约")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("Base64 vs Base64UrlSafe 仅末两字符不同（引擎重载同种子）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 引擎重载下 uniform_int_distribution(0,63) 产生相同索引序列，
        // 仅 index=62（Base64:'+' / UrlSafe:'-'）与 index=63（Base64:'/' / UrlSafe:'_'）处字符不同
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const std::string urlSafe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            char c1 = RandX::RandChar(rng1, RandX::CharSet::Base64);
            char c2 = RandX::RandChar(rng2, RandX::CharSet::Base64UrlSafe);
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
    TEST_CASE("Engine& 重载分布模板形参顺序推导")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 12345 };
        double valNorm = RandX::RandNormal(rng);
        double valLog = RandX::RandLogNormal(rng);
        int valGeom = RandX::RandGeometric(rng);
        double valExp = RandX::RandExp(rng);
        (void)valNorm; (void)valLog; (void)valExp;
        CHECK(valGeom >= 0);

    }
    TEST_CASE("MoveOnlyEngine 支持")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandXTest::OverloadFixtures::MoveOnlyEngine moe;
        int val = RandX::RandInt(moe, 1, 100);
        CHECK(val >= 1);
        CHECK(val <= 100);

    }
    TEST_CASE("RandBits 多引擎与全位宽矩阵验证")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng64{ 42 };
        RandX::Xoshiro128StarStar rng32{ 42 };
        RandXTest::ExtendedFixtures::SyntheticNonZeroMinEngine nzEng;

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
    TEST_CASE("RandBits 整型与自定义引擎")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::uint32_t bits32 = RandX::RandBits<32, std::uint32_t>();
        (void)bits32;
        RandX::Xoshiro256StarStar rng{ 999 };
        std::uint32_t bitsRng = RandX::RandBits<16, std::uint32_t>(rng);
        CHECK(bitsRng <= 0xFFFFU);

    }
    TEST_CASE("RandBits 边界位数测试")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 777 };
        auto b64 = RandX::RandBits<64, std::uint64_t>(rng);
        (void)b64;
        auto b1 = RandX::RandBits<1, std::uint64_t>(rng);
        CHECK(b1 <= 1ULL);

    }
    TEST_CASE("RandCanonical 直通 Bit-Extraction 浮点生成")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(12345);

        double d_sum = 0.0;
        constexpr int SampleCount = RandXTest::TestConstants::kRandCanonicalSampleCount;
        for (int i = 0; i < SampleCount; ++i)
        {
            double v = RandX::RandCanonicalDouble();
            CHECK((v >= 0.0 && v < 1.0));
            d_sum += v;
        }
        double d_mean = d_sum / SampleCount;
        CHECK((d_mean >= RandXTest::TestConstants::kRandCanonicalMeanLowerBound
            && d_mean <= RandXTest::TestConstants::kRandCanonicalMeanUpperBound));

        float f_sum = 0.0f;
        for (int i = 0; i < SampleCount; ++i)
        {
            float v = RandX::RandCanonicalFloat();
            CHECK((v >= 0.0f && v < 1.0f));
            f_sum += v;
        }
        float f_mean = f_sum / SampleCount;
        CHECK((f_mean >= RandXTest::TestConstants::kRandCanonicalMeanLowerBound
            && f_mean <= RandXTest::TestConstants::kRandCanonicalMeanUpperBound));

        // 32 位引擎专向兼容测试 (Xoshiro128StarStar)
        RandX::Xoshiro128StarStar rng32{ 54321 };
        double d32_sum = 0.0;
        for (int i = 0; i < SampleCount; ++i)
        {
            double v = RandX::RandCanonical<double>(rng32);
            CHECK((v >= 0.0 && v < 1.0));
            d32_sum += v;
        }
        double d32_mean = d32_sum / SampleCount;
        CHECK((d32_mean >= RandXTest::TestConstants::kRandCanonicalMeanLowerBound
            && d32_mean <= RandXTest::TestConstants::kRandCanonicalMeanUpperBound));

        float f32_sum = 0.0f;
        for (int i = 0; i < SampleCount; ++i)
        {
            float v = RandX::RandCanonical<float>(rng32);
            CHECK((v >= 0.0f && v < 1.0f));
            f32_sum += v;
        }
        float f32_mean = f32_sum / SampleCount;
        CHECK((f32_mean >= RandXTest::TestConstants::kRandCanonicalMeanLowerBound
            && f32_mean <= RandXTest::TestConstants::kRandCanonicalMeanUpperBound));

    }
    TEST_CASE("RandChar 9 种字符集全覆盖")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        using CS = RandX::CharSet;
        const std::string alnum = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        const std::string alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        const std::string lower = "abcdefghijklmnopqrstuvwxyz";
        const std::string upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const std::string digit = "0123456789";
        const std::string hexS = "0123456789abcdef";
        const std::string printable = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const std::string base64UrlSafe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            CHECK(alnum.find(RandX::RandChar(CS::Alphanumeric)) != std::string::npos);
            CHECK(alpha.find(RandX::RandChar(CS::Alpha)) != std::string::npos);
            CHECK(lower.find(RandX::RandChar(CS::Lower)) != std::string::npos);
            CHECK(upper.find(RandX::RandChar(CS::Upper)) != std::string::npos);
            CHECK(digit.find(RandX::RandChar(CS::Digit)) != std::string::npos);
            CHECK(hexS.find(RandX::RandChar(CS::Hex)) != std::string::npos);
            CHECK(printable.find(RandX::RandChar(CS::Printable)) != std::string::npos);
            CHECK(base64.find(RandX::RandChar(CS::Base64)) != std::string::npos);
            CHECK(base64UrlSafe.find(RandX::RandChar(CS::Base64UrlSafe)) != std::string::npos);
        }

    }
    TEST_CASE("RandChar 字符区间合法性异常契约")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        CHECK_THROWS_AS((void)RandX::RandChar('z', 'a'), std::invalid_argument);
        RandX::Xoshiro256StarStar rng{ 42 };
        CHECK_THROWS_AS((void)RandX::RandChar(rng, '9', '0'), std::invalid_argument);
        CHECK_NOTHROW((void)RandX::RandChar('a', 'z'));

    }
    TEST_CASE("RandChar 引擎重载")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 12345 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            char c = RandX::RandChar<char>(rng, 'a', 'z');
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            char c = RandX::RandChar<char>(rng, 'z');
            CHECK(c >= char{});
            CHECK(c <= 'z');
        }

    }
    TEST_CASE("RandChar 引擎重载确定性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            CHECK(RandX::RandChar(rng1, RandX::CharSet::Hex)
                == RandX::RandChar(rng2, RandX::CharSet::Hex));
        }

    }
    TEST_CASE("RandChar 支持异构自定义引擎重载")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 验证 RandChar(Engine&, CharSet) 对 SFC64 / RomuDuoJr 等任意引擎均可编译且确定性成立
        RandX::SFC64 sfc1{ 42 }, sfc2{ 42 };
        RandX::RomuDuoJr romu1{ 42 }, romu2{ 42 };
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            CHECK(RandX::RandChar(sfc1, RandX::CharSet::Hex)
                == RandX::RandChar(sfc2, RandX::CharSet::Hex));
            CHECK(RandX::RandChar(romu1, RandX::CharSet::Lower)
                == RandX::RandChar(romu2, RandX::CharSet::Lower));
        }

    }
    TEST_CASE("RandChar 范围与类型")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            char c = RandX::RandChar('a', 'z');
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            wchar_t wc = RandX::RandChar<wchar_t>(L'a', L'z');
            CHECK(wc >= L'a');
            CHECK(wc <= L'z');
        }
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            char16_t c16 = RandX::RandChar<char16_t>(u'a', u'z');
            CHECK(c16 >= u'a');
            CHECK(c16 <= u'z');
        }
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            char32_t c32 = RandX::RandChar<char32_t>(U'a', U'z');
            CHECK(c32 >= U'a');
            CHECK(c32 <= U'z');
        }
        // RandChar(max) 单参版
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            char c = RandX::RandChar<char>('z');
            CHECK(c >= char{});
            CHECK(c <= 'z');
        }

    }
    TEST_CASE("RandChar(CharSet::Base64UrlSafe) 全部属于 URL-safe 字母表")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::string urlSafeSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            char c = RandX::RandChar(RandX::CharSet::Base64UrlSafe);
            CHECK(urlSafeSet.find(c) != std::string::npos);
        }

    }
    TEST_CASE("RandChar(CharSet::Hex) 全部属于 [0-9a-f]")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::string hexSet = "0123456789abcdef";
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            char c = RandX::RandChar(RandX::CharSet::Hex);
            CHECK(hexSet.find(c) != std::string::npos);
        }

    }
    TEST_CASE("RandChar(CharSet::Lower) 全部属于 [a-z]")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kMonteCarloTrials1000; ++i)
        {
            char c = RandX::RandChar(RandX::CharSet::Lower);
            CHECK(c >= 'a');
            CHECK(c <= 'z');
        }

    }
    TEST_CASE("RandFill 填充范围")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("RandPermutation(0) 和 RandPermutation(1)")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        auto p0 = RandX::RandPermutation(0);
        CHECK(p0.empty());
        auto p1 = RandX::RandPermutation(1);
        REQUIRE(p1.size() == 1);
        CHECK(p1[0] == 0);

    }
    TEST_CASE("RandString 引擎重载确定性与字符集")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 验证 RandString(Engine&, n, CharSet) 引擎重载
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        const std::string hexSet = "0123456789abcdef";
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
    TEST_CASE("RandString 指定 Engine 和 custom string_view 重载及空 charset 异常")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("RandString(0) 返回空字符串")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        CHECK(RandX::RandString(0).empty());
        CHECK(RandX::RandString(0, RandX::CharSet::Hex).empty());

    }
    TEST_CASE("RandString(12, CharSet::Base64) RFC 4648 合规")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        auto s = RandX::RandString(12, RandX::CharSet::Base64);
        CHECK(s.size() == 12);
        for (char c : s)
            CHECK(base64.find(c) != std::string::npos);

    }
    TEST_CASE("RandString(12, CharSet::Base64UrlSafe) RFC 4648 §5 合规")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::string urlSafeSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        auto s = RandX::RandString(12, RandX::CharSet::Base64UrlSafe);
        CHECK(s.size() == 12);
        for (char c : s)
            CHECK(urlSafeSet.find(c) != std::string::npos);

    }
    TEST_CASE("RandString(16, CharSet::Hex) 长度与字符集")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::string hexSet = "0123456789abcdef";
        auto s = RandX::RandString(16, RandX::CharSet::Hex);
        CHECK(s.size() == 16);
        for (char c : s)
            CHECK(hexSet.find(c) != std::string::npos);

    }
    TEST_CASE("RandString(n, CharSet::Digit) 全部属于 [0-9]")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::string digit = "0123456789";
        auto s = RandX::RandString(100, RandX::CharSet::Digit);
        CHECK(s.size() == 100);
        for (char c : s)
            CHECK(digit.find(c) != std::string::npos);

    }
    TEST_CASE("RandUUID 在 32 位引擎下保持完整高位熵")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro128StarStar rng32{ 12345 };
        std::string u = RandX::RandUUID(rng32);
        CHECK(u.length() == 36);
        CHECK(u.substr(0, 8) != "00000000");
        CHECK(u.substr(24, 8) != "00000000");

    }
    TEST_CASE("RandUUID 格式正确与随机性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("RandVector 生成 vector")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("RandVector(..., 0) 返回空容器")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        auto v = RandX::RandVector<int>(0, 10, 0);
        CHECK(v.empty());

    }
    TEST_CASE("Reseed 可重播种确定性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Reseed(42);
        const auto a = RandX::RandInt(0, 1000000);
        RandX::Reseed(42);
        const auto b = RandX::RandInt(0, 1000000);
        CHECK(a == b);

    }
    TEST_CASE("ResetThreadLocalEngine 重置线程局部引擎")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::ResetThreadLocalEngine();
        int val = RandX::RandInt(1, 100);
        CHECK((val >= 1 && val <= 100));

    }
    TEST_CASE("Scripted 引擎精确受控 UUID 字段与 RFC 4122 变体验证")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandXTest::ExtendedFixtures::Scripted64BitEngine s64{{ 0x0123456789abcdefULL, 0xfedcba9876543210ULL }, 0};
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
        RandXTest::ExtendedFixtures::Scripted32BitEngine s32{{ 0x89abcdefU, 0x01234567U, 0x76543210U, 0xfedcba98U }, 0};
        std::string uuid32 = RandX::RandUUID(s32);
        CHECK(uuid32 == uuid64);

    }
    TEST_CASE("ThrowingEngine 异常可传播")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandXTest::OverloadFixtures::ThrowingEngine te;
        CHECK_THROWS_AS((void)RandX::RandNormal(te, 0.0, 1.0), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandInt(te, 1, 10), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandReal(te, 0.0, 1.0), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandBool(te, 0.5), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandExp(te, 1.0), std::runtime_error);

    }
    TEST_CASE("ThrowingEngine 异常正常向上抛出保证异常安全")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandXTest::ExtendedFixtures::ThrowingEngine te;
        CHECK_THROWS_AS((void)RandX::RandCanonical<double>(te), std::runtime_error);
        CHECK_THROWS_AS((void)(RandX::RandBits<16, std::uint32_t>(te)), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandReal(te, 0.0, 1.0), std::runtime_error);
        CHECK_THROWS_AS((void)RandX::RandReal(te, 0.0, 2.0), std::runtime_error);

    }
    TEST_CASE("double 在 2^104 阈值附近及其前后正常采样")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("float 在 2^46 阈值附近保留随机波动非固定均值")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("mt19937、minstd_rand 与合成窄字长引擎浮点生成正值保证")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::mt19937 mt{ 42 };
        float f = RandX::RandCanonical<float>(mt);
        CHECK(f > 0.0f);
        CHECK(f < 1.0f);

        RandXTest::ExtendedFixtures::Synthetic32BitWideType synth;
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

        RandXTest::ExtendedFixtures::SyntheticNonZeroMinEngine nonZeroMin;
        float f_nz = RandX::RandCanonical<float>(nonZeroMin);
        CHECK(f_nz >= 0.0f);
        CHECK(f_nz < 1.0f);

    }
    TEST_CASE("不合法权重在消费引擎前抛出 invalid_argument")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
        RandXTest::WeightedFixtures::CountingEngine ce;
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
    TEST_CASE("分布非法参数显式抛出 std::invalid_argument")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        CHECK_THROWS_AS((void)RandX::RandGeometric(0.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandGeometric(1.5), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandPoisson(-1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandNormal(0.0, -1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandExp(-2.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandLogNormal(0.0, 0.0), std::invalid_argument);

    }
    TEST_CASE("同种子下显式引擎与默认引擎一致性")
    {
        constexpr double Mean = 3.5;
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar explicitEngine(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            CHECK(RandX::RandNormal(explicitEngine, Mean) == RandX::RandNormal(Mean));
            CHECK(RandX::DefaultEngine() == explicitEngine);
        }

    }
    TEST_CASE("基本与非对称参数统计特性检验")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 42 };
        constexpr int SampleCount = 20'000;
        constexpr double MeanStandardErrorMultiplier = 4.0;
        constexpr double VarianceTolerance = RandXTest::TestConstants::kRandBetaVarianceTolerance;

        auto test_beta_stats = [&](double a, double b) {
            double sum = 0.0;
            double sum_sq = 0.0;
            for (int i = 0; i < SampleCount; ++i)
            {
                double val = RandX::RandBeta(rng, a, b);
                CHECK(val >= 0.0);
                CHECK(val <= 1.0);
                sum += val;
                sum_sq += val * val;
            }
            double mean = sum / SampleCount;
            double var = (sum_sq / SampleCount) - (mean * mean);

            double expected_mean = a / (a + b);
            double expected_var = (a * b) / ((a + b) * (a + b) * (a + b + 1.0));
            double se_mean = std::sqrt(expected_var / SampleCount);

            // 4 个标准误容差
            CHECK(std::abs(mean - expected_mean) < MeanStandardErrorMultiplier * se_mean);
            CHECK(std::abs(var - expected_var) < VarianceTolerance);
        };

        test_beta_stats(2.0, 2.0);
        test_beta_stats(0.5, 0.5);
        test_beta_stats(2.0, 5.0);
        test_beta_stats(5.0, 2.0);

    }
    TEST_CASE("大尺度与小尺度归一化")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("引擎调用次数与64/32位组合逻辑验收")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 64 位引擎消耗次数
        {
            RandXTest::ExtendedFixtures::CallCountingEngine<std::uint64_t, 0, UINT64_MAX> eng64;
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
            RandXTest::ExtendedFixtures::CallCountingEngine<std::uint32_t, 0, 0xFFFFFFFFU> eng32;
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
    TEST_CASE("引擎重载确定性（输入迭代器 reservoir）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::list<int> lst = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19 };
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        auto s1 = RandX::RandSample(rng1, lst.begin(), lst.end(), 5);
        auto s2 = RandX::RandSample(rng2, lst.begin(), lst.end(), 5);
        CHECK(s1 == s2);

    }
    TEST_CASE("引擎重载确定性（随机访问）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v(1000);
        for (int i = 0; i < 1000; ++i) v[static_cast<std::size_t>(i)] = i;
        RandX::Xoshiro256StarStar rng1{ 42 }, rng2{ 42 };
        auto s1 = RandX::RandSample(rng1, v.begin(), v.end(), 10);
        auto s2 = RandX::RandSample(rng2, v.begin(), v.end(), 10);
        CHECK(s1 == s2);

    }
    TEST_CASE("扩展便捷 API 集成")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // RandSample：无放回抽样
        std::vector<int> pool = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        auto sample = RandX::RandSample(pool, 3);
        CHECK(sample.size() == 3);
        for (const auto& x : sample)
            CHECK(std::find(pool.begin(), pool.end(), x) != pool.end());
        CHECK(sample[0] != sample[1]);
        CHECK(sample[0] != sample[2]);
        CHECK(sample[1] != sample[2]);

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
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandExp() >= 0.0);

        // RandPoisson：泊松分布（验证非负）
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandPoisson() >= 0);

        // RandGamma：伽马分布（验证正数）
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
            CHECK(RandX::RandGamma() > 0.0);

        // RandBits：N 位随机数
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
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
    TEST_CASE("数值左值与常量左值不触发引擎重载歧义")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("新分布引擎重载确定性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("极大有限量级与混合尺度")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("极端动态范围下溢抛出 range_error")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<double> extreme = { 1e300, 1e-320 };
        CHECK_THROWS_AS((void)RandX::RandWeighted(extreme), std::range_error);

    }
    TEST_CASE("正常区间严格保证半开区间界限")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 999 };
        for (int i = 0; i < 5000; ++i)
        {
            double v = RandX::RandReal(rng, 10.0, 20.0);
            CHECK(v >= 10.0);
            CHECK(v < 20.0);
        }

    }
    TEST_CASE("流式输入：std::istream_iterator 走 reservoir")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("空范围与零大小不消费引擎但仍校验参数")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandXTest::RealIntervalFixtures::IntervalCountingEngine ce;
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
    TEST_CASE("输入路径：std::list 走 reservoir sampling")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("边界：n=0 返回空")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 1, 2, 3, 4, 5 };
        auto s1 = RandX::RandSample(v.begin(), v.end(), 0);
        CHECK(s1.empty());
        std::list<int> lst = { 1, 2, 3 };
        auto s2 = RandX::RandSample(lst.begin(), lst.end(), 0);
        CHECK(s2.empty());

    }
    TEST_CASE("边界：n>=size 返回全部")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> empty;
        auto s1 = RandX::RandSample(empty.begin(), empty.end(), 3);
        CHECK(s1.empty());
        std::list<int> emptyLst;
        auto s2 = RandX::RandSample(emptyLst.begin(), emptyLst.end(), 3);
        CHECK(s2.empty());

    }
    TEST_CASE("退化区间返回端点且不消费引擎")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandXTest::RealIntervalFixtures::IntervalCountingEngine ce;
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
    TEST_CASE("邻接浮点区间全部结果严格等于下界")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 42 };
        RandXTest::RealIntervalFixtures::RunAdjacentIntervalTests<float>(rng);
        RandXTest::RealIntervalFixtures::RunAdjacentIntervalTests<double>(rng);
        RandXTest::RealIntervalFixtures::RunAdjacentIntervalTests<long double>(rng);

    }
    TEST_CASE("随机访问路径（hash-set 分支）：n·64<size 走 hash-set")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v(10000);
        for (int i = 0; i < 10000; ++i) v[static_cast<std::size_t>(i)] = i;
        // n=5, size=10000, n·64=320 < 10000 → hash-set 分支
        auto sample = RandX::RandSample(v.begin(), v.end(), 5);
        CHECK(sample.size() == 5);
        for (int x : sample)
        {
            CHECK(x >= 0);
            CHECK(x <= 9999);
            CHECK(std::count(sample.begin(), sample.end(), x) == 1);
        }

    }
    TEST_CASE("随机访问路径（索引分支）：n·64>=size 走索引数组")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::vector<int> v = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        // n=5, size=10, n·64=320 >= 10 → 索引数组分支
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
    TEST_CASE("零权重排除与原下标映射")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
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
    TEST_CASE("非法形状参数抛出 invalid_argument")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        CHECK_THROWS_AS((void)RandX::RandBeta(0.0, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(1.0, -1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(nan, 1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)RandX::RandBeta(1.0, inf), std::invalid_argument);

    }
    TEST_CASE("非法输入与超宽区间拒绝")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandXTest::RealIntervalFixtures::IntervalCountingEngine ce;
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
    TEST_CASE("预构建 discrete_distribution 透明转发")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 42 };
        std::discrete_distribution<int> dist({ 1.0, 2.0, 3.0 });
        auto v1 = RandX::RandWeighted(dist);
        CHECK((v1 >= 0 && v1 <= 2));
        auto v2 = RandX::RandWeighted(rng, dist);
        CHECK((v2 >= 0 && v2 <= 2));

    }
}

#if defined(__cpp_char8_t) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
TEST_SUITE("公共/条件/char8_t/便捷接口")
{
    TEST_CASE("RandChar char8_t（C++20+ 条件启用）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        for (int i = 0; i < RandXTest::TestConstants::kConvenienceTrials100; ++i)
        {
            char8_t c = RandX::RandChar<char8_t>(u8'a', u8'z');
            CHECK(c >= u8'a');
            CHECK(c <= u8'z');
        }

    }
}
#endif

#endif
