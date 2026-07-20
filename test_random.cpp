// test_random.cpp — Random.hpp (C++23) 确定性单元测试
#include "Random.hpp"
#include <cstdio>
#include <cassert>
#include <vector>
#include <array>

int main()
{
    // ===== 已知序列断言（seed=12345）=====

    // Xoshiro256StarStar
    {
        xoshiro::Xoshiro256StarStar rng{ 12345 };
        const std::uint64_t expected[] = {
            13720838825685603483ULL, 2398916695208396998ULL,
            17770384849984869256ULL, 891717726879801395ULL,
            10241316046318454344ULL
        };
        for (auto e : expected) assert(rng() == e);
        std::puts("[PASS] Xoshiro256StarStar");
    }

    // Xoshiro256PlusPlus
    {
        xoshiro::Xoshiro256PlusPlus rng{ 12345 };
        const std::uint64_t expected[] = {
            10201931350592234856ULL, 3780764549115216544ULL,
            1570246627180645737ULL, 3237956550421933520ULL,
            4899705286669081817ULL
        };
        for (auto e : expected) assert(rng() == e);
        std::puts("[PASS] Xoshiro256PlusPlus");
    }

    // Xoshiro256Plus
    {
        xoshiro::Xoshiro256Plus rng{ 12345 };
        const std::uint64_t expected[] = {
            5703686706282124394ULL, 15181128508879479020ULL,
            11713703072819584576ULL, 2395620858144650628ULL,
            8055391375587558944ULL
        };
        for (auto e : expected) assert(rng() == e);
        std::puts("[PASS] Xoshiro256Plus");
    }

    // Xoroshiro128PlusPlus
    {
        xoshiro::Xoroshiro128PlusPlus rng{ 12345 };
        const std::uint64_t expected[] = {
            16181086164699823776ULL, 14214852713950817264ULL,
            5918739589371211168ULL, 10279317896082661690ULL,
            3028767600443116799ULL
        };
        for (auto e : expected) assert(rng() == e);
        std::puts("[PASS] Xoroshiro128PlusPlus");
    }

    // Xoshiro128StarStar
    {
        xoshiro::Xoshiro128StarStar rng{ 12345 };
        const std::uint32_t expected[] = {
            1096865841U, 933661059U, 3314798965U, 1305642763U, 1040785987U
        };
        for (auto e : expected) assert(rng() == e);
        std::puts("[PASS] Xoshiro128StarStar");
    }

    // Xoroshiro64StarStar
    {
        xoshiro::Xoroshiro64StarStar rng{ 12345 };
        const std::uint32_t expected[] = {
            63958076U, 2181105171U, 532052331U, 3458610118U, 2965685819U
        };
        for (auto e : expected) assert(rng() == e);
        std::puts("[PASS] Xoroshiro64StarStar");
    }

    // SplitMix64
    {
        xoshiro::SplitMix64 rng{ 12345 };
        const std::uint64_t expected[] = {
            2454886589211414944ULL, 3778200017661327597ULL,
            2205171434679333405ULL, 3248800117070709450ULL,
            9350289611492784363ULL
        };
        for (auto e : expected) assert(rng() == e);
        std::puts("[PASS] SplitMix64");
    }

    // ===== serialize / deserialize =====
    {
        xoshiro::Xoshiro256StarStar rng{ 99999 };
        rng.discard(10);
        auto state = rng.serialize();
        xoshiro::Xoshiro256StarStar rng2{ state };
        for (int i = 0; i < 10; ++i)
            assert(rng() == rng2());
        std::puts("[PASS] serialize/deserialize");
    }

    // ===== discard =====
    {
        xoshiro::Xoshiro256StarStar a{ 42 };
        xoshiro::Xoshiro256StarStar b{ 42 };
        a.discard(100);
        for (int i = 0; i < 100; ++i) b();
        assert(a() == b());
        std::puts("[PASS] discard");
    }

    // ===== operator<=> =====
    {
        xoshiro::Xoshiro256StarStar a{ 1 }, b{ 1 }, c{ 2 };
        assert(a == b);
        assert(a != c);
        std::puts("[PASS] operator<=>");
    }

    // ===== constexpr RandIntCE =====
    {
        constexpr int v = xoshiro::RandIntCE(0, 100);
        static_assert(v == 34);
        static_assert(xoshiro::RandIntCE<int, 42>(1, 6) >= 1);
        static_assert(xoshiro::RandIntCE<int, 42>(1, 6) <= 6);
        std::puts("[PASS] RandIntCE (constexpr)");
    }

    // ===== 便捷 API 基本范围检查 =====
    {
        for (int i = 0; i < 1000; ++i)
        {
            int v = xoshiro::RandInt(1, 6);
            assert(v >= 1 && v <= 6);
        }
        for (int i = 0; i < 1000; ++i)
        {
            double v = xoshiro::RandReal();
            assert(v >= 0.0 && v < 1.0);
        }
        std::puts("[PASS] RandInt / RandReal range");
    }

    // ===== RandNormal =====
    {
        double sum = 0;
        const int N = 10000;
        for (int i = 0; i < N; ++i)
            sum += xoshiro::RandNormal(0.0, 1.0);
        double mean = sum / N;
        assert(mean > -0.1 && mean < 0.1);  // 均值应接近 0
        std::puts("[PASS] RandNormal");
    }

    // ===== RandShuffle =====
    {
        std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        std::vector<int> orig = v;
        xoshiro::RandShuffle(v);
        // 打乱后不太可能完全相同（概率 1/10!）
        assert(v != orig);
        // 元素集合不变
        std::sort(v.begin(), v.end());
        assert(v == orig);
        std::puts("[PASS] RandShuffle");
    }

    // ===== RandWeighted =====
    {
        std::vector<double> weights = {0.0, 0.0, 1.0, 0.0};
        for (int i = 0; i < 100; ++i)
            assert(xoshiro::RandWeighted(weights) == 2);
        std::puts("[PASS] RandWeighted");
    }

    // ===== RandElement =====
    {
        std::array<int, 3> arr = {10, 20, 30};
        for (int i = 0; i < 100; ++i)
        {
            int v = xoshiro::RandElement(arr);
            assert(v == 10 || v == 20 || v == 30);
        }
        std::puts("[PASS] RandElement");
    }

    // ===== jump / longJump 不崩溃 =====
    {
        xoshiro::Xoshiro256StarStar rng{ 777 };
        rng.jump();
        rng.longJump();
        (void)rng();
        std::puts("[PASS] jump / longJump");
    }

    std::puts("\n=== All Random.hpp tests passed ===");
    return 0;
}
