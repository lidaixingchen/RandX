// 跨标准序列一致性生成器（参考 gen_sequences.cpp）
//
// 同一份源文件分别编译两次：
//   g++ -std=c++23 gen_parity_sequences.cpp                          → 使用 RandX.hpp
//   g++ -std=c++17 -DRANDX_PARITY_CPP17 gen_parity_sequences.cpp    → 使用 RandX_Cpp17.hpp
// 固定种子输出各引擎与便捷 API 的序列，供 CI 逐字节比对（cmp/fc），
// 用于机械保证「双头文件输出序列完全一致」这一核心不变量。
// 仅使用两个头文件的公共交集 API，不涉及编译期随机 / ranges 特性。
#ifdef RANDX_PARITY_CPP17
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	constexpr std::uint64_t kSeed = 12345;

	// 原始输出序列：固定种子，打印前 count 个输出
	template <class Engine>
	void DumpEngine(const char* name, const int count = 64)
	{
		Engine rng{ kSeed };
		std::printf("[engine] %s seed=%" PRIu64 "\n", name, kSeed);
		for (int i = 0; i < count; ++i)
			std::printf("  %llu\n", static_cast<unsigned long long>(rng()));
	}

	// jump / longJump / discard 后的序列
	template <class Engine>
	void DumpJumpDiscard(const char* name)
	{
		std::printf("[jump] %s seed=%" PRIu64 "\n", name, kSeed);
		{
			Engine rng{ kSeed };
			rng.jump();
			for (int i = 0; i < 8; ++i)
				std::printf("  %llu\n", static_cast<unsigned long long>(rng()));
		}
		std::printf("[longJump] %s seed=%" PRIu64 "\n", name, kSeed);
		{
			Engine rng{ kSeed };
			rng.longJump();
			for (int i = 0; i < 8; ++i)
				std::printf("  %llu\n", static_cast<unsigned long long>(rng()));
		}
		std::printf("[discard(1000)] %s seed=%" PRIu64 "\n", name, kSeed);
		{
			Engine rng{ kSeed };
			rng.discard(1000);
			for (int i = 0; i < 8; ++i)
				std::printf("  %llu\n", static_cast<unsigned long long>(rng()));
		}
	}

	// 多流并行：MakeStreamEngine 各子流前几个输出
	template <class Engine>
	void DumpStreams(const char* name)
	{
		std::printf("[streams] %s seed=%" PRIu64 "\n", name, kSeed);
		for (std::uint64_t stream = 0; stream < 4; ++stream)
		{
			Engine rng = RandX::MakeStreamEngine<Engine>(stream, kSeed);
			std::printf("  stream %llu:\n", static_cast<unsigned long long>(stream));
			for (int i = 0; i < 4; ++i)
				std::printf("    %llu\n", static_cast<unsigned long long>(rng()));
		}
	}

	// 序列化状态（推进 100 步后），验证状态演化一致
	template <class Engine>
	void DumpSerializedState(const char* name)
	{
		Engine rng{ kSeed };
		rng.discard(100);
		const auto state = rng.serialize();
		std::printf("[serialize] %s seed=%" PRIu64 " after discard(100)\n", name, kSeed);
		for (const auto v : state)
			std::printf("  %llu\n", static_cast<unsigned long long>(v));
	}

	// 便捷 API：显式引擎重载，每个 API 使用独立的固定种子引擎
	void DumpConvenienceApis()
	{
		using RandX::Xoshiro256StarStar;

		std::printf("[api] RandInt(engine, -1000, 1000)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 32; ++i)
				std::printf("  %d\n", RandX::RandInt(eng, -1000, 1000));
		}
		std::printf("[api] RandInt<uint64>(engine, 0, max)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 8; ++i)
				std::printf("  %" PRIu64 "\n",
					RandX::RandInt<std::uint64_t>(eng, 0, UINT64_MAX));
		}
		std::printf("[api] RandReal(engine, 0.0, 1.0)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 32; ++i)
				std::printf("  %a\n", RandX::RandReal(eng, 0.0, 1.0));
		}
		std::printf("[api] RandCanonical<double>(engine)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %a\n", RandX::RandCanonical<double>(eng));
		}
		std::printf("[api] RandBool(engine, 0.3)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 32; ++i)
				std::printf("  %d\n", RandX::RandBool(eng, 0.3) ? 1 : 0);
		}
		std::printf("[api] RandBernoulli(engine, 0.7)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 32; ++i)
				std::printf("  %d\n", RandX::RandBernoulli(eng, 0.7) ? 1 : 0);
		}
		std::printf("[api] RandChar(engine, 'a', 'z')\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 32; ++i)
				std::printf("  %d\n", static_cast<int>(RandX::RandChar(eng, 'a', 'z')));
		}
		std::printf("[api] RandBits<N>(engine)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 8; ++i)
				std::printf("  %" PRIu64 "\n", RandX::RandBits<1>(eng));
			for (int i = 0; i < 8; ++i)
				std::printf("  %" PRIu64 "\n", RandX::RandBits<17>(eng));
			for (int i = 0; i < 8; ++i)
				std::printf("  %" PRIu64 "\n", RandX::RandBits<64>(eng));
		}
		std::printf("[api] RandNormal(engine, 0.0, 1.0)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %a\n", RandX::RandNormal(eng, 0.0, 1.0));
		}
		std::printf("[api] RandExp(engine, 1.5)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %a\n", RandX::RandExp(eng, 1.5));
		}
		std::printf("[api] RandPoisson(engine, 4.0)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %d\n", RandX::RandPoisson(eng, 4.0));
		}
		std::printf("[api] RandGamma(engine, 2.0, 3.0)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %a\n", RandX::RandGamma(eng, 2.0, 3.0));
		}
		std::printf("[api] RandBinomial(engine, 20, 0.4)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %d\n", RandX::RandBinomial(eng, 20, 0.4));
		}
		std::printf("[api] RandLogNormal(engine, 0.0, 0.5)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %a\n", RandX::RandLogNormal(eng, 0.0, 0.5));
		}
		std::printf("[api] RandGeometric(engine, 0.25)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %d\n", RandX::RandGeometric(eng, 0.25));
		}
		std::printf("[api] RandCauchy(engine, 0.0, 1.0)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 16; ++i)
				std::printf("  %a\n", RandX::RandCauchy(eng, 0.0, 1.0));
		}
		std::printf("[api] RandElement(engine, first, last)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			std::vector<int> v;
			for (int i = 0; i < 100; ++i)
				v.push_back(i);
			for (int i = 0; i < 8; ++i)
				std::printf("  %d\n", *RandX::RandElement(eng, v.begin(), v.end()));
		}
		std::printf("[api] RandSample(engine, first, last, 10)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			std::vector<int> v;
			for (int i = 0; i < 100; ++i)
				v.push_back(i);
			const auto sample = RandX::RandSample(eng, v.begin(), v.end(), 10);
			for (const int x : sample)
				std::printf("  %d\n", x);
		}
		std::printf("[api] RandFill(engine, first, last, 0, 999)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			std::vector<int> v(16);
			RandX::RandFill(eng, v.begin(), v.end(), 0, 999);
			for (const int x : v)
				std::printf("  %d\n", x);
		}
		std::printf("[api] RandVector(engine, 0, 999, 16)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			const auto v = RandX::RandVector(eng, 0, 999, 16);
			for (const int x : v)
				std::printf("  %d\n", x);
		}
		std::printf("[api] RandWeighted(engine, weights)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			const std::vector<double> weights{ 1.0, 2.0, 3.0, 4.0 };
			for (int i = 0; i < 16; ++i)
				std::printf("  %zu\n", RandX::RandWeighted(eng, weights));
		}
		std::printf("[api] RandString(engine, 32, CharSet)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			std::printf("  %s\n", RandX::RandString(eng, 32, RandX::CharSet::Alphanumeric).c_str());
			std::printf("  %s\n", RandX::RandString(eng, 32, RandX::CharSet::Hex).c_str());
			std::printf("  %s\n", RandX::RandString(eng, 32, RandX::CharSet::Base64UrlSafe).c_str());
			std::printf("  %s\n", RandX::RandString(eng, 32, "abcdef").c_str());
		}
		std::printf("[api] RandUUID(engine)\n");
		{
			Xoshiro256StarStar eng{ kSeed };
			for (int i = 0; i < 4; ++i)
				std::printf("  %s\n", RandX::RandUUID(eng).c_str());
		}
	}

	// 便捷 API：默认线程局部引擎路径，通过 Reseed 固定种子
	void DumpDefaultEngineApis()
	{
		std::printf("[default] RandInt(-100, 100)\n");
		RandX::Reseed(kSeed);
		for (int i = 0; i < 16; ++i)
			std::printf("  %d\n", RandX::RandInt(-100, 100));

		std::printf("[default] RandReal()\n");
		RandX::Reseed(kSeed);
		for (int i = 0; i < 8; ++i)
			std::printf("  %a\n", RandX::RandReal());

		std::printf("[default] RandShuffle(v)\n");
		RandX::Reseed(kSeed);
		{
			std::vector<int> v;
			for (int i = 0; i < 16; ++i)
				v.push_back(i);
			RandX::RandShuffle(v);
			for (const int x : v)
				std::printf("  %d\n", x);
		}

		std::printf("[default] RandPermutation(16)\n");
		RandX::Reseed(kSeed);
		for (const std::size_t x : RandX::RandPermutation(16))
			std::printf("  %zu\n", x);

		std::printf("[default] RandString(16, CharSet::Hex)\n");
		RandX::Reseed(kSeed);
		std::printf("  %s\n", RandX::RandString(16, RandX::CharSet::Hex).c_str());

		std::printf("[default] RandUUID()\n");
		RandX::Reseed(kSeed);
		std::printf("  %s\n", RandX::RandUUID().c_str());
	}
}

int main()
{
	// 8 引擎原始序列（ChaCha20 使用显式种子构造，确定性）
	DumpEngine<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");
	DumpEngine<RandX::Xoroshiro128StarStar>("Xoroshiro128StarStar");
	DumpEngine<RandX::Xoshiro128StarStar>("Xoshiro128StarStar");
	DumpEngine<RandX::Xoroshiro64StarStar>("Xoroshiro64StarStar");
	DumpEngine<RandX::SplitMix64>("SplitMix64");
	DumpEngine<RandX::SFC64>("SFC64");
	DumpEngine<RandX::RomuDuoJr>("RomuDuoJr");
	DumpEngine<RandX::ChaCha20>("ChaCha20");

	// jump / longJump / discard
	DumpJumpDiscard<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");
	DumpJumpDiscard<RandX::Xoroshiro128StarStar>("Xoroshiro128StarStar");
	DumpJumpDiscard<RandX::Xoshiro128StarStar>("Xoshiro128StarStar");

	// 多流并行
	DumpStreams<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");
	DumpStreams<RandX::Xoroshiro128StarStar>("Xoroshiro128StarStar");

	// 序列化状态
	DumpSerializedState<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");
	DumpSerializedState<RandX::Xoroshiro128StarStar>("Xoroshiro128StarStar");

	// 便捷 API（显式引擎 + 默认引擎两条路径）
	DumpConvenienceApis();
	DumpDefaultEngineApis();
	return 0;
}
