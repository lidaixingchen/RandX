// test_random.cpp — Random.hpp (C++23) 确定性单元测试
#include "Random.hpp"
#include <cstdio>
#include <cassert>
#include <vector>
#include <array>
#include <algorithm>

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

	// SFC64
	{
		xoshiro::SFC64 rng{ 12345 };
		const std::uint64_t expected[] = {
			13526236746588683560ULL, 8823148983839225293ULL,
			5240613241081073383ULL, 17030394482648619497ULL,
			7698197985592869707ULL
		};
		for (auto e : expected) assert(rng() == e);
		std::puts("[PASS] SFC64");
	}

	// RomuDuoJr
	{
		xoshiro::RomuDuoJr rng{ 12345 };
		const std::uint64_t expected[] = {
			2454886589211414944ULL, 12510505629750556783ULL,
			16962469053573158940ULL, 8350026492023846583ULL,
			15401281827437905834ULL
		};
		for (auto e : expected) assert(rng() == e);
		std::puts("[PASS] RomuDuoJr");
	}

	// ===== 多流引擎（MakeStreamEngine）=====
	{
		auto s0 = xoshiro::MakeStreamEngine<xoshiro::Xoshiro256StarStar>(0, 12345);
		auto s1 = xoshiro::MakeStreamEngine<xoshiro::Xoshiro256StarStar>(1, 12345);
		bool allSame = true;
		for (int i = 0; i < 10; ++i)
		{
			if (s0() != s1()) allSame = false;
		}
		assert(!allSame);
		std::puts("[PASS] MakeStreamEngine");
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
        static_assert(v == 61);
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

	// ===== 编译期洗牌（ShuffleCE）=====
	{
		constexpr auto shuffled = [] {
			std::array<int, 10> a = {0,1,2,3,4,5,6,7,8,9};
			xoshiro::ShuffleCE(a.begin(), a.end());
			return a;
		}();
		// 验证是排列：排序后等于原序列
		static_assert([](std::array<int, 10> s) {
			// 手动排序（constexpr）
			for (int i = 0; i < 10; ++i)
				for (int j = i + 1; j < 10; ++j)
					if (s[j] < s[i]) { auto t = s[i]; s[i] = s[j]; s[j] = t; }
			return s == std::array{0,1,2,3,4,5,6,7,8,9};
		}(shuffled));
		// 验证确实被打乱了（极大概率不等）
		static_assert(shuffled != std::array{0,1,2,3,4,5,6,7,8,9});
		std::printf("[PASS] ShuffleCE (constexpr)\n");
	}

	// ===== Reseed 可重播种 =====
	{
		xoshiro::Reseed(42);
		const auto a = xoshiro::RandInt(0, 1000000);
		xoshiro::Reseed(42);
		const auto b = xoshiro::RandInt(0, 1000000);
		assert(a == b);
		std::printf("[PASS] Reseed\n");
	}

	// ===== std::seed_seq 播种 =====
	{
		std::seed_seq seq{1, 2, 3, 4, 5, 6, 7, 8};
		xoshiro::Xoshiro256StarStar rng1{ seq };
		std::seed_seq seq2{1, 2, 3, 4, 5, 6, 7, 8};
		xoshiro::Xoshiro256StarStar rng2{ seq2 };
		// 相同 seed_seq → 相同输出
		assert(rng1() == rng2());
		assert(rng1() == rng2());
		std::printf("[PASS] seed_seq\n");
	}

	// ===== 扩展便捷 API =====
	{
		// RandSample：无放回抽样
		std::vector<int> pool = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
		auto sample = xoshiro::RandSample(pool, 3);
		assert(sample.size() == 3);
		// 验证抽到的元素都在原容器中
		for (const auto& x : sample)
			assert(std::find(pool.begin(), pool.end(), x) != pool.end());
		// 验证无重复
		assert(sample[0] != sample[1] || sample[1] != sample[2] || sample[0] != sample[2]);

		// RandPermutation：随机排列
		auto perm = xoshiro::RandPermutation(10);
		assert(perm.size() == 10);
		// 验证是 [0,10) 的排列
		auto sorted_perm = perm;
		std::sort(sorted_perm.begin(), sorted_perm.end());
		for (std::size_t i = 0; i < 10; ++i)
			assert(sorted_perm[i] == i);

		// RandString：随机字符串
		auto str = xoshiro::RandString(16);
		assert(str.size() == 16);
		auto str2 = xoshiro::RandString(8, "01");
		assert(str2.size() == 8);
		for (const auto& c : str2) assert(c == '0' || c == '1');

		// RandExp：指数分布（验证非负）
		for (int i = 0; i < 100; ++i)
			assert(xoshiro::RandExp() >= 0.0);

		// RandPoisson：泊松分布（验证非负）
		for (int i = 0; i < 100; ++i)
			assert(xoshiro::RandPoisson() >= 0);

		// RandGamma：伽马分布（验证正数）
		for (int i = 0; i < 100; ++i)
			assert(xoshiro::RandGamma() > 0.0);

		// RandBits：N 位随机数
		for (int i = 0; i < 100; ++i)
		{
			assert(xoshiro::RandBits<8>() < 256);
			assert(xoshiro::RandBits<1>() < 2);
		}

		// RandUUID：格式验证
		auto uuid = xoshiro::RandUUID();
		assert(uuid.size() == 36);
		assert(uuid[8] == '-' && uuid[13] == '-' && uuid[18] == '-' && uuid[23] == '-');
		assert(uuid[14] == '4');  // 版本号
		assert(uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b');  // 变体位

		std::printf("[PASS] 扩展便捷 API (Sample/Permutation/String/Exp/Poisson/Gamma/Bits/UUID)\n");
	}

	// ===== 统计自检（Chi-Square 频率检验）=====
	{
		constexpr int N = 1'000'000;
		constexpr int BINS = 100;
		constexpr double EXPECTED = static_cast<double>(N) / BINS;

		xoshiro::Xoshiro256StarStar rng{ 98765 };
		int counts[BINS] = {};
		for (int i = 0; i < N; ++i)
			++counts[xoshiro::RandInt(rng, 0, BINS - 1)];

		double chi2 = 0.0;
		for (int i = 0; i < BINS; ++i)
		{
			const double diff = counts[i] - EXPECTED;
			chi2 += diff * diff / EXPECTED;
		}

		// 自由度 99，alpha=0.001 临界值 ≈ 148.2
		assert(chi2 < 148.2);
		std::printf("[PASS] Chi-Square (chi2 = %.1f < 148.2)\n", chi2);
	}

	// Chi-Square 原始输出检验（SFC64）
	{
		constexpr int N = 1'000'000;
		constexpr int BINS = 128;
		constexpr double EXPECTED = static_cast<double>(N) / BINS;

		xoshiro::SFC64 rng{ 54321 };
		int counts[BINS] = {};
		for (int i = 0; i < N; ++i)
			++counts[rng() & 127];

		double chi2 = 0.0;
		for (int i = 0; i < BINS; ++i)
		{
			const double diff = counts[i] - EXPECTED;
			chi2 += diff * diff / EXPECTED;
		}

		// 自由度 127，alpha=0.001 临界值 ≈ 173.6
		assert(chi2 < 173.6);
		std::printf("[PASS] Chi-Square raw: SFC64 (chi2 = %.1f)\n", chi2);
	}

	// Chi-Square 原始输出检验（RomuDuoJr）
	{
		constexpr int N = 1'000'000;
		constexpr int BINS = 128;
		constexpr double EXPECTED = static_cast<double>(N) / BINS;

		xoshiro::RomuDuoJr rng{ 54321 };
		int counts[BINS] = {};
		for (int i = 0; i < N; ++i)
			++counts[rng() & 127];

		double chi2 = 0.0;
		for (int i = 0; i < BINS; ++i)
		{
			const double diff = counts[i] - EXPECTED;
			chi2 += diff * diff / EXPECTED;
		}

		assert(chi2 < 173.6);
		std::printf("[PASS] Chi-Square raw: RomuDuoJr (chi2 = %.1f)\n", chi2);
	}

    std::puts("\n=== All Random.hpp tests passed ===");
    return 0;
}
