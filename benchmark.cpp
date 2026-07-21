//----------------------------------------------------------------------------------------
//
//	benchmark.cpp — RandX.hpp 全引擎性能基准测试
//
//	编译：g++ -std=c++23 -O2 benchmark.cpp -o benchmark.exe
//
//	测试项目：
//		1. 原始吞吐量（Mops/s, MB/s）
//		2. jump() 跳跃开销（仅支持 jump 的引擎）
//		3. RandInt(0, 999999) 便捷 API 吞吐量
//		4. RandFill 容器填充吞吐量
//		5. RandVector 生成新 vector 吞吐量（含分配开销）
//
//----------------------------------------------------------------------------------------

#include "RandX.hpp"
#include <chrono>
#include <cstdio>
#include <list>
#include <type_traits>
#include <vector>

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
		DoNotOptimize(RandX::RandInt(rng, 0, 999999));
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double mops = N / ms / 1000.0;

	std::printf("  %-24s %8.1f Mops/s  (%6.1f ms)\n", name, mops, ms);
	return mops;
}

//----------------------------------------------------------------------------------------
//
//	RandFill 容器填充吞吐量基准测试
//
//----------------------------------------------------------------------------------------

// RandFill 基准测试的单次填充元素数（1M，循环 N/FillN 次凑够 N）
static constexpr int FillN = 1'000'000;

// 测量 RandFill(engine, first, last, 0, 999999) 填充预分配容器的吞吐量
template <class Engine>
static double BenchmarkRandFill(const char* name)
{
	Engine rng{ 42 };
	std::vector<int> buf(static_cast<std::size_t>(FillN));

	const auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N / FillN; ++i)
		RandX::RandFill(rng, buf.begin(), buf.end(), 0, 999999);
	const auto end = std::chrono::high_resolution_clock::now();

	DoNotOptimize(buf[0]);

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double mops = N / ms / 1000.0;

	std::printf("  %-24s %8.1f Mops/s  (%6.1f ms)\n", name, mops, ms);
	return mops;
}

//----------------------------------------------------------------------------------------
//
//	RandVector 生成新 vector 吞吐量基准测试
//
//----------------------------------------------------------------------------------------

// 测量 RandVector(engine, 0, 999999, VecN) 生成新 vector 的吞吐量（含分配开销）
template <class Engine>
static double BenchmarkRandVector(const char* name)
{
	Engine rng{ 42 };

	const auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N / FillN; ++i)
	{
		auto v = RandX::RandVector(rng, 0, 999999, FillN);
		DoNotOptimize(v[0]);
	}
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double mops = N / ms / 1000.0;

	std::printf("  %-24s %8.1f Mops/s  (%6.1f ms)\n", name, mops, ms);
	return mops;
}

//----------------------------------------------------------------------------------------
//
//	RandSample 迭代器版性能基准测试（v1.2 新增）
//
//----------------------------------------------------------------------------------------

// RandSample 基准测试的容器大小（大 N 小 n 场景）
static constexpr int SampleSize = 1'000'000;
// 抽样数量（n² < size，强制走 hash-set 分支）
static constexpr int SampleN_HashSet = 100;
// 抽样数量（n² >= size，走索引数组分支）
static constexpr int SampleN_Index = 2000;
// 测试轮数
static constexpr int SampleTrials = 100;

// 测量 RandSample 容器版（复制整个容器到 pool）
static double BenchmarkRandSampleContainer(const char* name)
{
	std::vector<int> data(static_cast<std::size_t>(SampleSize));
	for (int i = 0; i < SampleSize; ++i) data[static_cast<std::size_t>(i)] = i;

	const auto start = std::chrono::high_resolution_clock::now();
	for (int t = 0; t < SampleTrials; ++t)
	{
		auto s = RandX::RandSample(data, static_cast<std::size_t>(SampleN_HashSet));
		DoNotOptimize(s[0]);
	}
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double opsPerSec = SampleTrials / ms * 1000.0;
	std::printf("  %-40s %10.1f ops/s  (%6.1f ms / %d trials)\n",
		name, opsPerSec, ms, SampleTrials);
	return opsPerSec;
}

// 测量 RandSample 迭代器版（hash-set 分支：n² < size）
static double BenchmarkRandSampleHashSet(const char* name)
{
	std::vector<int> data(static_cast<std::size_t>(SampleSize));
	for (int i = 0; i < SampleSize; ++i) data[static_cast<std::size_t>(i)] = i;

	const auto start = std::chrono::high_resolution_clock::now();
	for (int t = 0; t < SampleTrials; ++t)
	{
		auto s = RandX::RandSample(data.begin(), data.end(), SampleN_HashSet);
		DoNotOptimize(s[0]);
	}
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double opsPerSec = SampleTrials / ms * 1000.0;
	std::printf("  %-40s %10.1f ops/s  (%6.1f ms / %d trials)\n",
		name, opsPerSec, ms, SampleTrials);
	return opsPerSec;
}

// 测量 RandSample 迭代器版（索引数组分支：n² >= size）
static double BenchmarkRandSampleIndex(const char* name)
{
	std::vector<int> data(static_cast<std::size_t>(SampleSize));
	for (int i = 0; i < SampleSize; ++i) data[static_cast<std::size_t>(i)] = i;

	const auto start = std::chrono::high_resolution_clock::now();
	for (int t = 0; t < SampleTrials; ++t)
	{
		auto s = RandX::RandSample(data.begin(), data.end(), SampleN_Index);
		DoNotOptimize(s[0]);
	}
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double opsPerSec = SampleTrials / ms * 1000.0;
	std::printf("  %-40s %10.1f ops/s  (%6.1f ms / %d trials)\n",
		name, opsPerSec, ms, SampleTrials);
	return opsPerSec;
}

// 测量 RandSample 迭代器版（reservoir：std::list 输入迭代器）
static double BenchmarkRandSampleReservoir(const char* name)
{
	std::list<int> data;
	for (int i = 0; i < SampleSize; ++i) data.push_back(i);

	const auto start = std::chrono::high_resolution_clock::now();
	for (int t = 0; t < SampleTrials; ++t)
	{
		auto s = RandX::RandSample(data.begin(), data.end(), SampleN_HashSet);
		DoNotOptimize(s[0]);
	}
	const auto end = std::chrono::high_resolution_clock::now();

	const double ms = std::chrono::duration<double, std::milli>(end - start).count();
	const double opsPerSec = SampleTrials / ms * 1000.0;
	std::printf("  %-40s %10.1f ops/s  (%6.1f ms / %d trials)\n",
		name, opsPerSec, ms, SampleTrials);
	return opsPerSec;
}

//----------------------------------------------------------------------------------------
//
//	主函数
//
//----------------------------------------------------------------------------------------

int main()
{
	std::printf("================================================================\n");
	std::printf("  RandX.hpp 性能基准测试 (N = %d)\n", N);
	std::printf("================================================================\n\n");

	//----------------------------------------------------------------
	// 第一部分：原始吞吐量
	//----------------------------------------------------------------
	std::printf("[1] 原始吞吐量 (operator())\n");
	std::printf("----------------------------------------------------------------\n");

	// 64 位输出引擎
	std::printf("  --- 64-bit 引擎 ---\n");
	BenchmarkRaw<RandX::SplitMix64>("SplitMix64");
	BenchmarkRaw<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");
	BenchmarkRaw<RandX::Xoroshiro128StarStar>("Xoroshiro128StarStar");
	BenchmarkRaw<RandX::SFC64>("SFC64");
	BenchmarkRaw<RandX::RomuDuoJr>("RomuDuoJr");

	// 32 位输出引擎
	std::printf("  --- 32-bit 引擎 ---\n");
	BenchmarkRaw<RandX::Xoshiro128StarStar>("Xoshiro128StarStar");
	BenchmarkRaw<RandX::Xoroshiro64StarStar>("Xoroshiro64StarStar");

	std::printf("\n");

	//----------------------------------------------------------------
	// 第二部分：jump() 跳跃开销
	//----------------------------------------------------------------
	std::printf("[2] jump() 跳跃开销 (JumpN = %d)\n", JumpN);
	std::printf("----------------------------------------------------------------\n");

	// Xoshiro256 系列（jump 等价于 2^128 步）
	std::printf("  --- Xoshiro256 系列 (jump = 2^128 步) ---\n");
	BenchmarkJump<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");

	// Xoroshiro128 系列（jump 等价于 2^64 步）
	std::printf("  --- Xoroshiro128 系列 (jump = 2^64 步) ---\n");
	BenchmarkJump<RandX::Xoroshiro128StarStar>("Xoroshiro128StarStar");

	// Xoshiro128 系列（jump 等价于 2^64 步）
	std::printf("  --- Xoshiro128 系列 (jump = 2^64 步) ---\n");
	BenchmarkJump<RandX::Xoshiro128StarStar>("Xoshiro128StarStar");

	std::printf("\n");

	//----------------------------------------------------------------
	// 第三部分：RandInt 便捷 API 吞吐量
	//----------------------------------------------------------------
	std::printf("[3] RandInt(engine, 0, 999999) 便捷 API 吞吐量\n");
	std::printf("----------------------------------------------------------------\n");

	// 64 位引擎
	std::printf("  --- 64-bit 引擎 ---\n");
	BenchmarkRandInt<RandX::SplitMix64>("SplitMix64");
	BenchmarkRandInt<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");
	BenchmarkRandInt<RandX::Xoroshiro128StarStar>("Xoroshiro128StarStar");
	BenchmarkRandInt<RandX::SFC64>("SFC64");
	BenchmarkRandInt<RandX::RomuDuoJr>("RomuDuoJr");

	// 32 位引擎
	std::printf("  --- 32-bit 引擎 ---\n");
	BenchmarkRandInt<RandX::Xoshiro128StarStar>("Xoshiro128StarStar");
	BenchmarkRandInt<RandX::Xoroshiro64StarStar>("Xoroshiro64StarStar");

	std::printf("\n");

	//----------------------------------------------------------------
	// 第四部分：RandFill 容器填充吞吐量
	//----------------------------------------------------------------
	std::printf("[4] RandFill(engine, first, last, 0, 999999) 容器填充吞吐量\n");
	std::printf("    (预分配 %d 元素 vector，循环 %d 次)\n", FillN, N / FillN);
	std::printf("----------------------------------------------------------------\n");

	BenchmarkRandFill<RandX::SplitMix64>("SplitMix64");
	BenchmarkRandFill<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");
	BenchmarkRandFill<RandX::SFC64>("SFC64");

	std::printf("\n");

	//----------------------------------------------------------------
	// 第五部分：RandVector 生成新 vector 吞吐量
	//----------------------------------------------------------------
	std::printf("[5] RandVector(engine, 0, 999999, %d) 生成新 vector 吞吐量\n", FillN);
	std::printf("    (含 vector 分配开销，循环 %d 次)\n", N / FillN);
	std::printf("----------------------------------------------------------------\n");

	BenchmarkRandVector<RandX::SplitMix64>("SplitMix64");
	BenchmarkRandVector<RandX::Xoshiro256StarStar>("Xoshiro256StarStar");
	BenchmarkRandVector<RandX::SFC64>("SFC64");

	std::printf("\n");

	//----------------------------------------------------------------
	// 第六部分：RandSample 迭代器版性能对比（v1.2 新增）
	//----------------------------------------------------------------
	std::printf("[6] RandSample 迭代器版性能对比 (SampleSize=%d, trials=%d)\n", SampleSize, SampleTrials);
	std::printf("    大 N 小 n 场景：1M 元素中抽 %d（hash-set 分支）/ %d（索引分支）\n",
		SampleN_HashSet, SampleN_Index);
	std::printf("----------------------------------------------------------------\n");

	BenchmarkRandSampleContainer("RandSample(container, n=100)");
	BenchmarkRandSampleHashSet("RandSample(iter, n=100, hash-set)");
	BenchmarkRandSampleIndex("RandSample(iter, n=2000, index)");
	BenchmarkRandSampleReservoir("RandSample(list, n=100, reservoir)");

	std::printf("\n");
	std::printf("================================================================\n");
	std::printf("  基准测试完成\n");
	std::printf("================================================================\n");

	return 0;
}
