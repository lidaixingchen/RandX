//----------------------------------------------------------------------------------------
//
//	benchmark.cpp — Random.hpp 全引擎性能基准测试
//
//	编译：g++ -std=c++23 -O2 benchmark.cpp -o benchmark.exe
//
//	测试项目：
//		1. 原始吞吐量（Mops/s, MB/s）
//		2. jump() 跳跃开销（仅支持 jump 的引擎）
//		3. RandInt(0, 999999) 便捷 API 吞吐量
//
//----------------------------------------------------------------------------------------

#include "Random.hpp"
#include <chrono>
#include <cstdio>
#include <type_traits>

// 迭代次数
static constexpr int N = 100'000'000;

// jump 基准测试的迭代次数（jump 本身开销较大，减少次数）
static constexpr int JumpN = 1'000'000;

// 防止编译器优化掉生成结果
template <class T>
static void DoNotOptimize(T value)
{
	static volatile T sink;
	sink = value;
}

//----------------------------------------------------------------------------------------
//
//	原始吞吐量基准测试
//
//----------------------------------------------------------------------------------------

// 测量引擎原始吞吐量，输出 Mops/s 和 MB/s
template <class Engine>
static double BenchmarkRaw(const char* name)
{
	Engine rng{ 42 };

	const auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i)
		DoNotOptimize(rng());
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double mops = N / ms / 1000.0;
	const double mbs = mops * sizeof(typename Engine::result_type);

	std::printf("  %-24s %8.1f Mops/s  %8.1f MB/s  (%6.1f ms)\n", name, mops, mbs, ms);
	return mops;
}

//----------------------------------------------------------------------------------------
//
//	jump() 跳跃开销基准测试
//
//----------------------------------------------------------------------------------------

// 检测引擎是否拥有 jump() 成员函数
template <class Engine, class = void>
struct HasJump : std::false_type {};

template <class Engine>
struct HasJump<Engine, std::void_t<decltype(std::declval<Engine&>().jump())>> : std::true_type {};

template <class Engine>
static constexpr bool HasJumpV = HasJump<Engine>::value;

// 测量 jump() 调用开销（仅对支持 jump 的引擎有效）
template <class Engine>
static double BenchmarkJump(const char* name)
{
	Engine rng{ 42 };

	const auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < JumpN; ++i)
		rng.jump();
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double jumpsPerSec = JumpN / ms * 1000.0;

	// 防止引擎状态被优化掉
	DoNotOptimize(rng());

	std::printf("  %-24s %12.0f jumps/s  (%6.1f ms / %d jumps)\n", name, jumpsPerSec, ms, JumpN);
	return jumpsPerSec;
}

//----------------------------------------------------------------------------------------
//
//	RandInt 便捷 API 吞吐量基准测试
//
//----------------------------------------------------------------------------------------

// 测量 RandInt(engine, 0, 999999) 的吞吐量
template <class Engine>
static double BenchmarkRandInt(const char* name)
{
	Engine rng{ 42 };

	const auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i)
		DoNotOptimize(xoshiro::RandInt(rng, 0, 999999));
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double mops = N / ms / 1000.0;

	std::printf("  %-24s %8.1f Mops/s  (%6.1f ms)\n", name, mops, ms);
	return mops;
}

//----------------------------------------------------------------------------------------
//
//	主函数
//
//----------------------------------------------------------------------------------------

int main()
{
	std::printf("================================================================\n");
	std::printf("  Random.hpp 性能基准测试 (N = %d)\n", N);
	std::printf("================================================================\n\n");

	//----------------------------------------------------------------
	// 第一部分：原始吞吐量
	//----------------------------------------------------------------
	std::printf("[1] 原始吞吐量 (operator())\n");
	std::printf("----------------------------------------------------------------\n");

	// 64 位输出引擎
	std::printf("  --- 64-bit 引擎 ---\n");
	BenchmarkRaw<xoshiro::SplitMix64>("SplitMix64");
	BenchmarkRaw<xoshiro::Xoshiro256Plus>("Xoshiro256Plus");
	BenchmarkRaw<xoshiro::Xoshiro256PlusPlus>("Xoshiro256PlusPlus");
	BenchmarkRaw<xoshiro::Xoshiro256StarStar>("Xoshiro256StarStar");
	BenchmarkRaw<xoshiro::Xoroshiro128Plus>("Xoroshiro128Plus");
	BenchmarkRaw<xoshiro::Xoroshiro128PlusPlus>("Xoroshiro128PlusPlus");
	BenchmarkRaw<xoshiro::Xoroshiro128StarStar>("Xoroshiro128StarStar");
	BenchmarkRaw<xoshiro::SFC64>("SFC64");
	BenchmarkRaw<xoshiro::RomuDuoJr>("RomuDuoJr");

	// 32 位输出引擎
	std::printf("  --- 32-bit 引擎 ---\n");
	BenchmarkRaw<xoshiro::Xoshiro128Plus>("Xoshiro128Plus");
	BenchmarkRaw<xoshiro::Xoshiro128PlusPlus>("Xoshiro128PlusPlus");
	BenchmarkRaw<xoshiro::Xoshiro128StarStar>("Xoshiro128StarStar");
	BenchmarkRaw<xoshiro::Xoroshiro64Star>("Xoroshiro64Star");
	BenchmarkRaw<xoshiro::Xoroshiro64StarStar>("Xoroshiro64StarStar");

	std::printf("\n");

	//----------------------------------------------------------------
	// 第二部分：jump() 跳跃开销
	//----------------------------------------------------------------
	std::printf("[2] jump() 跳跃开销 (JumpN = %d)\n", JumpN);
	std::printf("----------------------------------------------------------------\n");

	// Xoshiro256 系列（jump 等价于 2^128 步）
	std::printf("  --- Xoshiro256 系列 (jump = 2^128 步) ---\n");
	BenchmarkJump<xoshiro::Xoshiro256Plus>("Xoshiro256Plus");
	BenchmarkJump<xoshiro::Xoshiro256PlusPlus>("Xoshiro256PlusPlus");
	BenchmarkJump<xoshiro::Xoshiro256StarStar>("Xoshiro256StarStar");

	// Xoroshiro128 系列（jump 等价于 2^64 步）
	std::printf("  --- Xoroshiro128 系列 (jump = 2^64 步) ---\n");
	BenchmarkJump<xoshiro::Xoroshiro128Plus>("Xoroshiro128Plus");
	BenchmarkJump<xoshiro::Xoroshiro128PlusPlus>("Xoroshiro128PlusPlus");
	BenchmarkJump<xoshiro::Xoroshiro128StarStar>("Xoroshiro128StarStar");

	// Xoshiro128 系列（jump 等价于 2^64 步）
	std::printf("  --- Xoshiro128 系列 (jump = 2^64 步) ---\n");
	BenchmarkJump<xoshiro::Xoshiro128Plus>("Xoshiro128Plus");
	BenchmarkJump<xoshiro::Xoshiro128PlusPlus>("Xoshiro128PlusPlus");
	BenchmarkJump<xoshiro::Xoshiro128StarStar>("Xoshiro128StarStar");

	std::printf("\n");

	//----------------------------------------------------------------
	// 第三部分：RandInt 便捷 API 吞吐量
	//----------------------------------------------------------------
	std::printf("[3] RandInt(engine, 0, 999999) 便捷 API 吞吐量\n");
	std::printf("----------------------------------------------------------------\n");

	// 64 位引擎
	std::printf("  --- 64-bit 引擎 ---\n");
	BenchmarkRandInt<xoshiro::SplitMix64>("SplitMix64");
	BenchmarkRandInt<xoshiro::Xoshiro256Plus>("Xoshiro256Plus");
	BenchmarkRandInt<xoshiro::Xoshiro256PlusPlus>("Xoshiro256PlusPlus");
	BenchmarkRandInt<xoshiro::Xoshiro256StarStar>("Xoshiro256StarStar");
	BenchmarkRandInt<xoshiro::Xoroshiro128Plus>("Xoroshiro128Plus");
	BenchmarkRandInt<xoshiro::Xoroshiro128PlusPlus>("Xoroshiro128PlusPlus");
	BenchmarkRandInt<xoshiro::Xoroshiro128StarStar>("Xoroshiro128StarStar");
	BenchmarkRandInt<xoshiro::SFC64>("SFC64");
	BenchmarkRandInt<xoshiro::RomuDuoJr>("RomuDuoJr");

	// 32 位引擎
	std::printf("  --- 32-bit 引擎 ---\n");
	BenchmarkRandInt<xoshiro::Xoshiro128Plus>("Xoshiro128Plus");
	BenchmarkRandInt<xoshiro::Xoshiro128PlusPlus>("Xoshiro128PlusPlus");
	BenchmarkRandInt<xoshiro::Xoshiro128StarStar>("Xoshiro128StarStar");
	BenchmarkRandInt<xoshiro::Xoroshiro64Star>("Xoroshiro64Star");
	BenchmarkRandInt<xoshiro::Xoroshiro64StarStar>("Xoroshiro64StarStar");

	std::printf("\n");
	std::printf("================================================================\n");
	std::printf("  基准测试完成\n");
	std::printf("================================================================\n");

	return 0;
}
