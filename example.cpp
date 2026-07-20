//----------------------------------------------------------------------------------------
//
//	example.cpp
//	Random.hpp (C++23) 功能演示
//
//----------------------------------------------------------------------------------------

# include "Random.hpp"
# include <iostream>
# include <array>
# include <list>
# include <sstream>
# include <vector>
# include <random>

int main()
{
	using namespace xoshiro;

	// ===== 便捷 API（零配置）=====
	std::cout << "=== 便捷 API ===\n";
	std::cout << "RandInt(1, 6)   = " << RandInt(1, 6) << '\n';
	std::cout << "RandReal()      = " << RandReal() << '\n';
	std::cout << "RandBool(0.3)   = " << RandBool(0.3) << '\n';

	std::vector<int> items = { 10, 20, 30, 40, 50 };
	std::cout << "RandElement     = " << RandElement(items) << '\n';
	std::cout << "RandNormal()    = " << RandNormal() << '\n';

	// ===== 新增 API 演示 =====
	std::cout << "\n=== 新增 API ===\n";
	{
		// RandChar：类型安全的字符随机
		std::cout << "RandChar('a','z') = " << RandChar('a', 'z') << '\n';

		// RandElement 迭代器版：支持非随机访问容器（如 std::list）
		std::list<int> lst = { 100, 200, 300, 400, 500 };
		auto it = RandElement(lst.begin(), lst.end());
		std::cout << "RandElement(list) = " << *it << '\n';

		// RandFill：填充已有容器（STL 风格）
		std::array<int, 5> arr{};
		RandFill(arr.begin(), arr.end(), 0, 99);
		std::cout << "RandFill(array)   = ";
		for (const auto& v : arr) std::cout << v << ' ';
		std::cout << '\n';

		// RandVector：直接生成新 vector
		auto vi = RandVector(0, 99, 5);
		std::cout << "RandVector(int)   = ";
		for (const auto& v : vi) std::cout << v << ' ';
		std::cout << '\n';

		auto vd = RandVector(0.0, 1.0, 3);
		std::cout << "RandVector(double)= ";
		for (const auto& v : vd) std::cout << v << ' ';
		std::cout << '\n';

		// ⚠️ RandFill 类型推导陷阱：min/max 字面量类型须与容器元素一致
		// std::vector<double> v(5); RandFill(v.begin(), v.end(), 0, 1);   // ❌ T=int，精度丢失
		// std::vector<double> v(5); RandFill(v.begin(), v.end(), 0.0, 1.0); // ✅ T=double
	}

	// ===== operator<< / operator>> 流式序列化 =====
	std::cout << "\n=== operator<< / operator>> ===\n";
	{
		Xoshiro256StarStar rng{ 777 };
		for (int i = 0; i < 3; ++i) (void)rng();

		std::stringstream ss;
		ss << rng;  // 流式输出引擎状态（空格分隔的十进制数序列）
		std::cout << "rng state: " << ss.str() << '\n';

		Xoshiro256StarStar rng2{};
		ss >> rng2;  // 流式恢复
		std::cout << "restored equal: " << (rng == rng2 ? "true" : "false") << '\n';
	}

	// ===== 手动管理引擎 =====
	std::cout << "\n=== 手动引擎 ===\n";
	{
		Xoshiro256StarStar rng{ 777 };
		std::cout << "Xoshiro256**: " << rng() << '\n';

		// 配合标准库 distribution
		std::uniform_int_distribution<int> dist(1, 100);
		std::cout << "dist(1,100)   = " << dist(rng) << '\n';

		// 序列化 / 反序列化
		auto state = rng.serialize();
		const auto next1 = rng();
		rng.deserialize(state);
		const auto next2 = rng();
		std::cout << "serialize OK  = " << (next1 == next2 ? "true" : "false") << '\n';
	}

	// ===== std::seed_seq 播种 =====
	std::cout << "\n=== seed_seq 播种 ===\n";
	{
		std::seed_seq seq{ 1, 2, 3, 4, 5, 6, 7, 8 };
		Xoshiro256PlusPlus rng{ seq };
		std::cout << "from seed_seq = " << rng() << '\n';
	}

	// ===== 多流并行（MakeStreamEngine）=====
	std::cout << "\n=== 多流并行 ===\n";
	{
		auto stream0 = MakeStreamEngine<Xoshiro256StarStar>(0, 42);
		auto stream1 = MakeStreamEngine<Xoshiro256StarStar>(1, 42);
		std::cout << "stream[0]     = " << RandInt(stream0, 0, 999) << '\n';
		std::cout << "stream[1]     = " << RandInt(stream1, 0, 999) << '\n';
	}

	// ===== 编译期随机（constexpr）=====
	std::cout << "\n=== constexpr ===\n";
	{
		constexpr int v = RandIntCE(0, 100);
		std::cout << "RandIntCE     = " << v << " (编译期确定)\n";

		constexpr auto shuffled = ShuffledArray<int, 5>(
			std::array{ 1, 2, 3, 4, 5 });
		std::cout << "ShuffledArray = ";
		for (const auto& x : shuffled) std::cout << x << ' ';
		std::cout << "(编译期洗牌)\n";
	}

	// ===== Reseed 复现 =====
	std::cout << "\n=== Reseed ===\n";
	{
		Reseed(12345);
		const auto a = RandInt(0, 1000000);
		Reseed(12345);
		const auto b = RandInt(0, 1000000);
		std::cout << "same seed     = " << (a == b ? "true" : "false") << '\n';
	}

	// ===== jump 并行子序列 =====
	std::cout << "\n=== jump ===\n";
	{
		Xoshiro256StarStar rng{ 999 };
		rng.jump();      // 前进 2^128 步
		std::cout << "after jump    = " << rng() << '\n';
		rng.longJump();  // 前进 2^192 步
		std::cout << "after longJmp = " << rng() << '\n';
	}

	// ===== 高速引擎 =====
	std::cout << "\n=== SFC64 / RomuDuoJr ===\n";
	{
		SFC64 sfc{ 42 };
		RomuDuoJr romu{ 42 };
		std::cout << "SFC64         = " << sfc() << '\n';
		std::cout << "RomuDuoJr     = " << romu() << '\n';
	}
}
