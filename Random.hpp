//----------------------------------------------------------------------------------------
//
//	Random.hpp — 基于 Xoshiro 的伪随机数生成器封装库（C++23）
//
//	原始算法：David Blackman & Sebastiano Vigna (http://prng.di.unimi.it/)
//	原始 C++ 封装：Ryo Suzuki <reputeless@gmail.com> (Xoshiro-cpp)
//
//========================================================================================
//
//	快速上手
//
//		#include "Random.hpp"
//
//		// 最简用法：直接调用便捷函数（内部使用线程局部 Xoshiro256StarStar）
//		int   dice  = xoshiro::RandInt(1, 6);        // [1, 6] 闭区间整数
//		double coin = xoshiro::RandReal();            // [0.0, 1.0) 浮点数
//		bool  flag  = xoshiro::RandBool(0.3);         // 30% 概率为 true
//
//		std::vector<int> v = {10, 20, 30, 40};
//		auto& elem = xoshiro::RandElement(v);         // 随机取一个元素
//
//	手动管理引擎
//
//		// 用真随机种子创建引擎
//		xoshiro::Xoshiro256StarStar rng{ xoshiro::RandomSeed() };
//
//		// 指定引擎的便捷函数重载
//		int val = xoshiro::RandInt(rng, 0, 99);
//
//		// 配合标准库 distribution 使用（满足 UniformRandomBitGenerator）
//		std::normal_distribution<double> norm(0.0, 1.0);
//		double sample = norm(rng);
//
//	序列化 / 反序列化（保存和恢复状态）
//
//		auto state = rng.serialize();
//		// ... 稍后恢复 ...
//		rng.deserialize(state);
//
//	跳跃（并行计算中生成不重叠子序列）
//
//		rng.jump();      // 等价于前进 2^128 步（xoshiro256 系列）
//		rng.longJump();  // 等价于前进 2^192 步
//
//	跳过指定次数
//
//		rng.discard(1000);  // 跳过 1000 个输出
//
//	引擎选择指南
//
//		引擎                    输出   周期        状态   适用场景
//		─────────────────────────────────────────────────────────────
//		Xoshiro256StarStar     64-bit  2^256-1    32B    通用首选，统计质量最优
//		Xoshiro256PlusPlus     64-bit  2^256-1    32B    通用，略快于 **
//		Xoshiro256Plus         64-bit  2^256-1    32B    最快，低位质量稍弱
//		Xoroshiro128PlusPlus   64-bit  2^128-1    16B    内存受限场景
//		Xoroshiro128StarStar   64-bit  2^128-1    16B    同上，统计更优
//		Xoroshiro128Plus       64-bit  2^128-1    16B    同上，最快
//		Xoshiro128PlusPlus     32-bit  2^128-1    16B    32 位平台
//		Xoshiro128StarStar     32-bit  2^128-1    16B    32 位平台，统计更优
//		Xoshiro128Plus         32-bit  2^128-1    16B    32 位平台，最快
//		Xoroshiro64StarStar    32-bit  2^64-1      8B    极端内存受限
//		Xoroshiro64Star        32-bit  2^64-1      8B    极端内存受限，最快
//		SplitMix64             64-bit  2^64        8B    种子扩展 / 哈希，非通用 PRNG
//
//----------------------------------------------------------------------------------------

# pragma once
# include <cstdint>
# include <array>
# include <limits>
# include <concepts>
# include <random>
# include <algorithm>
# include <cassert>
# include <type_traits>
# if defined(_MSC_VER) && (defined(__x86_64__) || defined(_M_X64))
#	include <immintrin.h>
# endif

namespace xoshiro
{
	// 生成器的默认种子值
	inline constexpr std::uint64_t DefaultSeed = 1234567890ULL;

	// 将给定的 uint32 值 `i` 转换为 [0.0f, 1.0f) 范围内的 32 位浮点数
	template <std::same_as<std::uint32_t> Uint32>
	[[nodiscard]]
	inline constexpr float FloatFromBits(Uint32 i) noexcept;

	// 将给定的 uint64 值 `i` 转换为 [0.0, 1.0) 范围内的 64 位浮点数
	template <std::same_as<std::uint64_t> Uint64>
	[[nodiscard]]
	inline constexpr double DoubleFromBits(Uint64 i) noexcept;

	// SplitMix64
	// 输出：64 位
	// 周期：2^64
	// 状态大小：8 字节
	// 原始实现：http://prng.di.unimi.it/splitmix64.c
	class SplitMix64
	{
	public:

		using state_type	= std::uint64_t;	
		using result_type	= std::uint64_t;
		
		[[nodiscard]]
		explicit constexpr SplitMix64(state_type state = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, SplitMix64>)
		[[nodiscard]]
		explicit constexpr SplitMix64(SeedSeq& seq) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		template <std::size_t N>
		[[nodiscard]]
		constexpr std::array<std::uint64_t, N> generateSeedSequence() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const SplitMix64&, const SplitMix64&) = default;
	
	private:

		state_type m_state;
	};

	// xoshiro256+
	// 输出：64 位
	// 周期：2^256 - 1
	// 状态大小：32 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro256plus.c
	// 版本：1.0
	class Xoshiro256Plus
	{
	public:

		using state_type	= std::array<std::uint64_t, 4>;
		using result_type	= std::uint64_t;

		[[nodiscard]]
		explicit constexpr Xoshiro256Plus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro256Plus>)
		[[nodiscard]]
		explicit constexpr Xoshiro256Plus(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoshiro256Plus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^128 次 operator()；
		// 可用于生成 2^128 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^192 次 next()；
		// 可用于生成 2^64 个起始点，每个起始点可通过 jump()
		// 生成 2^64 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoshiro256Plus&, const Xoshiro256Plus&) = default;

	private:

		state_type m_state;
	};

	// xoshiro256++
	// 输出：64 位
	// 周期：2^256 - 1
	// 状态大小：32 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro256plusplus.c
	// 版本：1.0
	class Xoshiro256PlusPlus
	{
	public:

		using state_type	= std::array<std::uint64_t, 4>;
		using result_type	= std::uint64_t;

		[[nodiscard]]
		explicit constexpr Xoshiro256PlusPlus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro256PlusPlus>)
		[[nodiscard]]
		explicit constexpr Xoshiro256PlusPlus(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoshiro256PlusPlus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^128 次 next()；
		// 可用于生成 2^128 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^192 次 next()；
		// 可用于生成 2^64 个起始点，每个起始点可通过 jump()
		// 生成 2^64 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoshiro256PlusPlus&, const Xoshiro256PlusPlus&) = default;

	private:

		state_type m_state;
	};

	// xoshiro256**
	// 输出：64 位
	// 周期：2^256 - 1
	// 状态大小：32 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro256starstar.c
	// 版本：1.0
	class Xoshiro256StarStar
	{
	public:

		using state_type	= std::array<std::uint64_t, 4>;
		using result_type	= std::uint64_t;

		[[nodiscard]]
		explicit constexpr Xoshiro256StarStar(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro256StarStar>)
		[[nodiscard]]
		explicit constexpr Xoshiro256StarStar(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoshiro256StarStar(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^128 次 next()；
		// 可用于生成 2^128 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^192 次 next()；
		// 可用于生成 2^64 个起始点，每个起始点可通过 jump()
		// 生成 2^64 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoshiro256StarStar&, const Xoshiro256StarStar&) = default;

	private:

		state_type m_state;
	};

	// xoroshiro128+
	// 输出：64 位
	// 周期：2^128 - 1
	// 状态大小：16 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro128plus.c
	// 版本：1.0
	class Xoroshiro128Plus
	{
	public:

		using state_type	= std::array<std::uint64_t, 2>;
		using result_type	= std::uint64_t;

		[[nodiscard]]
		explicit constexpr Xoroshiro128Plus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro128Plus>)
		[[nodiscard]]
		explicit constexpr Xoroshiro128Plus(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoroshiro128Plus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^64 次 next()；
		// 可用于生成 2^64 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^96 次 next()；
		// 可用于生成 2^32 个起始点，每个起始点可通过 jump()
		// 生成 2^32 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoroshiro128Plus&, const Xoroshiro128Plus&) = default;

	private:

		state_type m_state;
	};

	// xoroshiro128++
	// 输出：64 位
	// 周期：2^128 - 1
	// 状态大小：16 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro128plusplus.c
	// 版本：1.0
	class Xoroshiro128PlusPlus
	{
	public:

		using state_type	= std::array<std::uint64_t, 2>;
		using result_type	= std::uint64_t;

		[[nodiscard]]
		explicit constexpr Xoroshiro128PlusPlus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro128PlusPlus>)
		[[nodiscard]]
		explicit constexpr Xoroshiro128PlusPlus(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoroshiro128PlusPlus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^64 次 next()；
		// 可用于生成 2^64 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^96 次 next()；
		// 可用于生成 2^32 个起始点，每个起始点可通过 jump()
		// 生成 2^32 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoroshiro128PlusPlus&, const Xoroshiro128PlusPlus&) = default;

	private:

		state_type m_state;
	};

	// xoroshiro128**
	// 输出：64 位
	// 周期：2^128 - 1
	// 状态大小：16 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro128starstar.c
	// 版本：1.0
	class Xoroshiro128StarStar
	{
	public:

		using state_type	= std::array<std::uint64_t, 2>;
		using result_type	= std::uint64_t;

		[[nodiscard]]
		explicit constexpr Xoroshiro128StarStar(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro128StarStar>)
		[[nodiscard]]
		explicit constexpr Xoroshiro128StarStar(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoroshiro128StarStar(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^64 次 next()；
		// 可用于生成 2^64 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^96 次 next()；
		// 可用于生成 2^32 个起始点，每个起始点可通过 jump()
		// 生成 2^32 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoroshiro128StarStar&, const Xoroshiro128StarStar&) = default;

	private:

		state_type m_state;
	};

	// xoshiro128+
	// 输出：32 位
	// 周期：2^128 - 1
	// 状态大小：16 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro128plus.c
	// 版本：1.0
	class Xoshiro128Plus
	{
	public:

		using state_type	= std::array<std::uint32_t, 4>;
		using result_type	= std::uint32_t;

		[[nodiscard]]
		explicit constexpr Xoshiro128Plus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro128Plus>)
		[[nodiscard]]
		explicit constexpr Xoshiro128Plus(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoshiro128Plus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^64 次 next()；
		// 可用于生成 2^64 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^96 次 next()；
		// 可用于生成 2^32 个起始点，每个起始点可通过 jump()
		// 生成 2^32 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoshiro128Plus&, const Xoshiro128Plus&) = default;

	private:

		state_type m_state;
	};

	// xoshiro128++
	// 输出：32 位
	// 周期：2^128 - 1
	// 状态大小：16 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro128plusplus.c
	// 版本：1.0
	class Xoshiro128PlusPlus
	{
	public:

		using state_type	= std::array<std::uint32_t, 4>;
		using result_type	= std::uint32_t;

		[[nodiscard]]
		explicit constexpr Xoshiro128PlusPlus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro128PlusPlus>)
		[[nodiscard]]
		explicit constexpr Xoshiro128PlusPlus(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoshiro128PlusPlus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^64 次 next()；
		// 可用于生成 2^64 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^96 次 next()；
		// 可用于生成 2^32 个起始点，每个起始点可通过 jump()
		// 生成 2^32 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoshiro128PlusPlus&, const Xoshiro128PlusPlus&) = default;

	private:

		state_type m_state;
	};

	// xoshiro128**
	// 输出：32 位
	// 周期：2^128 - 1
	// 状态大小：16 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro128starstar.c
	// 版本：1.1
	class Xoshiro128StarStar
	{
	public:

		using state_type	= std::array<std::uint32_t, 4>;
		using result_type	= std::uint32_t;

		[[nodiscard]]
		explicit constexpr Xoshiro128StarStar(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro128StarStar>)
		[[nodiscard]]
		explicit constexpr Xoshiro128StarStar(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoshiro128StarStar(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 跳跃函数。等价于调用 2^64 次 next()；
		// 可用于生成 2^64 个不重叠的子序列，适用于并行计算。
		constexpr void jump() noexcept;

		// 长跳跃函数。等价于调用 2^96 次 next()；
		// 可用于生成 2^32 个起始点，每个起始点可通过 jump()
		// 生成 2^32 个不重叠的子序列，适用于并行分布式计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoshiro128StarStar&, const Xoshiro128StarStar&) = default;

	private:

		state_type m_state;
	};

	// xoroshiro64*
	// 输出：32 位
	// 周期：2^64 - 1
	// 状态大小：8 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro64star.c
	class Xoroshiro64Star
	{
	public:

		using state_type	= std::array<std::uint32_t, 2>;
		using result_type	= std::uint32_t;

		[[nodiscard]]
		explicit constexpr Xoroshiro64Star(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro64Star>)
		[[nodiscard]]
		explicit constexpr Xoroshiro64Star(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoroshiro64Star(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoroshiro64Star&, const Xoroshiro64Star&) = default;

	private:

		state_type m_state;
	};

	// xoroshiro64**
	// 输出：32 位
	// 周期：2^64 - 1
	// 状态大小：8 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro64starstar.c
	class Xoroshiro64StarStar
	{
	public:

		using state_type	= std::array<std::uint32_t, 2>;
		using result_type	= std::uint32_t;

		[[nodiscard]]
		explicit constexpr Xoroshiro64StarStar(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro64StarStar>)
		[[nodiscard]]
		explicit constexpr Xoroshiro64StarStar(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr Xoroshiro64StarStar(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const Xoroshiro64StarStar&, const Xoroshiro64StarStar&) = default;

	private:

		state_type m_state;
	};

	// SFC64 (Small Fast Counter)
	// 输出：64 位
	// 周期：>= 2^64（counter 保证）
	// 状态大小：32 字节
	// 作者：Chris Doty-Humphrey (PractRand)
	// 特点：通过 PractRand 全部测试，速度极快，无 jump 支持
	class SFC64
	{
	public:

		using state_type	= std::array<std::uint64_t, 4>;
		using result_type	= std::uint64_t;

		[[nodiscard]]
		explicit constexpr SFC64(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, SFC64>)
		[[nodiscard]]
		explicit constexpr SFC64(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr SFC64(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const SFC64&, const SFC64&) = default;

	private:

		std::uint64_t m_a;
		std::uint64_t m_b;
		std::uint64_t m_c;
		std::uint64_t m_counter;
	};

	// RomuDuoJr
	// 输出：64 位
	// 周期：估计 >= 2^51（非严格证明）
	// 状态大小：16 字节
	// 作者：Mark Overton
	// 特点：极简极快，适合非关键模拟，无 jump 支持，周期无严格保证
	class RomuDuoJr
	{
	public:

		using state_type	= std::array<std::uint64_t, 2>;
		using result_type	= std::uint64_t;

		[[nodiscard]]
		explicit constexpr RomuDuoJr(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, RomuDuoJr>)
		[[nodiscard]]
		explicit constexpr RomuDuoJr(SeedSeq& seq) noexcept;

		[[nodiscard]]
		explicit constexpr RomuDuoJr(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		friend auto operator <=>(const RomuDuoJr&, const RomuDuoJr&) = default;

	private:

		std::uint64_t m_x;
		std::uint64_t m_y;
	};
}

////////////////////////////////////////////////////////////////

namespace xoshiro
{
	template <std::same_as<std::uint32_t> Uint32>
	inline constexpr float FloatFromBits(const Uint32 i) noexcept
	{
		return (i >> 8) * 0x1.0p-24f;
	}

	template <std::same_as<std::uint64_t> Uint64>
	inline constexpr double DoubleFromBits(const Uint64 i) noexcept
	{
		return (i >> 11) * 0x1.0p-53;
	}

	namespace detail
	{
		[[nodiscard]]
		static constexpr std::uint64_t RotL(const std::uint64_t x, const int s) noexcept
		{
			return (x << s) | (x >> (64 - s));
		}

		[[nodiscard]]
		static constexpr std::uint32_t RotL(const std::uint32_t x, const int s) noexcept
		{
			return (x << s) | (x >> (32 - s));
		}

		// 检测状态数组是否全零（全零是 xoshiro/xoroshiro 的吸收态）
		template <std::size_t N>
		[[nodiscard]]
		static constexpr bool IsAllZero(const std::array<std::uint64_t, N>& state) noexcept
		{
			for (const auto& s : state) { if (s != 0) return false; }
			return true;
		}

		template <std::size_t N>
		[[nodiscard]]
		static constexpr bool IsAllZero(const std::array<std::uint32_t, N>& state) noexcept
		{
			for (const auto& s : state) { if (s != 0) return false; }
			return true;
		}

		// 尝试使用 RDRAND 获取 64 位硬件随机数
		[[nodiscard]]
		inline bool HardwareRand64(std::uint64_t& out) noexcept
		{
#if defined(__x86_64__) || defined(_M_X64)
	#if defined(__RDRND__)
			unsigned long long result;
			if (__builtin_ia32_rdrand64_step(&result))
			{
				out = result;
				return true;
			}
	#elif defined(_MSC_VER)
			unsigned long long result;
			if (_rdrand64_step(&result))
			{
				out = result;
				return true;
			}
	#endif
#endif
			(void)out;
			return false;
		}

	}

	////////////////////////////////////////////////////////////////
	//
	//	SplitMix64
	//
	inline constexpr SplitMix64::SplitMix64(const state_type state) noexcept
		: m_state(state) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, SplitMix64>)
	inline constexpr SplitMix64::SplitMix64(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 2> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
	}

	inline constexpr SplitMix64::result_type SplitMix64::operator()() noexcept
	{
		std::uint64_t z = (m_state += 0x9e3779b97f4a7c15);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
		z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
		return z ^ (z >> 31);
	}

	template <std::size_t N>
	inline constexpr std::array<std::uint64_t, N> SplitMix64::generateSeedSequence() noexcept
	{
		std::array<std::uint64_t, N> seeds = {};

		for (auto& seed : seeds)
		{
			seed = operator()();
		}

		return seeds;
	}

	inline constexpr SplitMix64::result_type SplitMix64::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr SplitMix64::result_type SplitMix64::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr SplitMix64::state_type SplitMix64::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void SplitMix64::deserialize(const state_type state) noexcept
	{
		m_state = state;
	}

	inline constexpr void SplitMix64::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro256+
	//
	inline constexpr Xoshiro256Plus::Xoshiro256Plus(const std::uint64_t seed) noexcept
		: m_state(SplitMix64{ seed }.generateSeedSequence<4>()) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro256Plus>)
	inline constexpr Xoshiro256Plus::Xoshiro256Plus(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 8> seeds;
		seq.generate(seeds.begin(), seeds.end());
		for (int i = 0; i < 4; ++i)
			m_state[i] = (static_cast<std::uint64_t>(seeds[2*i]) << 32) | seeds[2*i+1];
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoshiro256Plus::Xoshiro256Plus(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoshiro256Plus::result_type Xoshiro256Plus::operator()() noexcept
	{
		const std::uint64_t result = m_state[0] + m_state[3];
		const std::uint64_t t = m_state[1] << 17;
		m_state[2] ^= m_state[0];
		m_state[3] ^= m_state[1];
		m_state[1] ^= m_state[2];
		m_state[0] ^= m_state[3];
		m_state[2] ^= t;
		m_state[3] = detail::RotL(m_state[3], 45);
		return result;
	}

	inline constexpr void Xoshiro256Plus::jump() noexcept
	{
		constexpr std::uint64_t JUMP[] = { 0x180ec6d33cfd0aba, 0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;
		std::uint64_t s2 = 0;
		std::uint64_t s3 = 0;

		for (std::uint64_t jump : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr void Xoshiro256Plus::longJump() noexcept
	{
		constexpr std::uint64_t LONG_JUMP[] = { 0x76e15d3efefdcbbf, 0xc5004e441c522fb3, 0x77710069854ee241, 0x39109bb02acbe635 };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;
		std::uint64_t s2 = 0;
		std::uint64_t s3 = 0;

		for (std::uint64_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr Xoshiro256Plus::result_type Xoshiro256Plus::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoshiro256Plus::result_type Xoshiro256Plus::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoshiro256Plus::state_type Xoshiro256Plus::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoshiro256Plus::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoshiro256Plus::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro256++
	//
	inline constexpr Xoshiro256PlusPlus::Xoshiro256PlusPlus(const std::uint64_t seed) noexcept
		: m_state(SplitMix64{ seed }.generateSeedSequence<4>()) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro256PlusPlus>)
	inline constexpr Xoshiro256PlusPlus::Xoshiro256PlusPlus(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 8> seeds;
		seq.generate(seeds.begin(), seeds.end());
		for (int i = 0; i < 4; ++i)
			m_state[i] = (static_cast<std::uint64_t>(seeds[2*i]) << 32) | seeds[2*i+1];
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoshiro256PlusPlus::Xoshiro256PlusPlus(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoshiro256PlusPlus::result_type Xoshiro256PlusPlus::operator()() noexcept
	{
		const std::uint64_t result = detail::RotL(m_state[0] + m_state[3], 23) + m_state[0];
		const std::uint64_t t = m_state[1] << 17;
		m_state[2] ^= m_state[0];
		m_state[3] ^= m_state[1];
		m_state[1] ^= m_state[2];
		m_state[0] ^= m_state[3];
		m_state[2] ^= t;
		m_state[3] = detail::RotL(m_state[3], 45);
		return result;
	}

	inline constexpr void Xoshiro256PlusPlus::jump() noexcept
	{
		constexpr std::uint64_t JUMP[] = { 0x180ec6d33cfd0aba, 0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;
		std::uint64_t s2 = 0;
		std::uint64_t s3 = 0;

		for (std::uint64_t jump : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr void Xoshiro256PlusPlus::longJump() noexcept
	{
		constexpr std::uint64_t LONG_JUMP[] = { 0x76e15d3efefdcbbf, 0xc5004e441c522fb3, 0x77710069854ee241, 0x39109bb02acbe635 };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;
		std::uint64_t s2 = 0;
		std::uint64_t s3 = 0;

		for (std::uint64_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr Xoshiro256PlusPlus::result_type Xoshiro256PlusPlus::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoshiro256PlusPlus::result_type Xoshiro256PlusPlus::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoshiro256PlusPlus::state_type Xoshiro256PlusPlus::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoshiro256PlusPlus::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoshiro256PlusPlus::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro256**
	//
	inline constexpr Xoshiro256StarStar::Xoshiro256StarStar(const std::uint64_t seed) noexcept
		: m_state(SplitMix64{ seed }.generateSeedSequence<4>()) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro256StarStar>)
	inline constexpr Xoshiro256StarStar::Xoshiro256StarStar(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 8> seeds;
		seq.generate(seeds.begin(), seeds.end());
		for (int i = 0; i < 4; ++i)
			m_state[i] = (static_cast<std::uint64_t>(seeds[2*i]) << 32) | seeds[2*i+1];
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoshiro256StarStar::Xoshiro256StarStar(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoshiro256StarStar::result_type Xoshiro256StarStar::operator()() noexcept
	{
		const std::uint64_t result = detail::RotL(m_state[1] * 5, 7) * 9;
		const std::uint64_t t = m_state[1] << 17;
		m_state[2] ^= m_state[0];
		m_state[3] ^= m_state[1];
		m_state[1] ^= m_state[2];
		m_state[0] ^= m_state[3];
		m_state[2] ^= t;
		m_state[3] = detail::RotL(m_state[3], 45);
		return result;
	}

	inline constexpr void Xoshiro256StarStar::jump() noexcept
	{
		constexpr std::uint64_t JUMP[] = { 0x180ec6d33cfd0aba, 0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;
		std::uint64_t s2 = 0;
		std::uint64_t s3 = 0;

		for (std::uint64_t jump : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr void Xoshiro256StarStar::longJump() noexcept
	{
		constexpr std::uint64_t LONG_JUMP[] = { 0x76e15d3efefdcbbf, 0xc5004e441c522fb3, 0x77710069854ee241, 0x39109bb02acbe635 };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;
		std::uint64_t s2 = 0;
		std::uint64_t s3 = 0;

		for (std::uint64_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr Xoshiro256StarStar::result_type Xoshiro256StarStar::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoshiro256StarStar::result_type Xoshiro256StarStar::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoshiro256StarStar::state_type Xoshiro256StarStar::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoshiro256StarStar::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoshiro256StarStar::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro128+
	//
	inline constexpr Xoroshiro128Plus::Xoroshiro128Plus(const std::uint64_t seed) noexcept
		: m_state(SplitMix64{ seed }.generateSeedSequence<2>()) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro128Plus>)
	inline constexpr Xoroshiro128Plus::Xoroshiro128Plus(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 4> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state[0] = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		m_state[1] = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoroshiro128Plus::Xoroshiro128Plus(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoroshiro128Plus::result_type Xoroshiro128Plus::operator()() noexcept
	{
		const std::uint64_t s0 = m_state[0];
		std::uint64_t s1 = m_state[1];
		const std::uint64_t result = s0 + s1;
		s1 ^= s0;
		m_state[0] = detail::RotL(s0, 24) ^ s1 ^ (s1 << 16);
		m_state[1] = detail::RotL(s1, 37);
		return result;
	}

	inline constexpr void Xoroshiro128Plus::jump() noexcept
	{
		constexpr std::uint64_t JUMP[] = { 0xdf900294d8f554a5, 0x170865df4b3201fc };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;

		for (std::uint64_t jump : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
	}

	inline constexpr void Xoroshiro128Plus::longJump() noexcept
	{
		constexpr std::uint64_t LONG_JUMP[] = { 0xd2a98b26625eee7b, 0xdddf9b1090aa7ac1 };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;

		for (std::uint64_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
	}

	inline constexpr Xoroshiro128Plus::result_type Xoroshiro128Plus::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoroshiro128Plus::result_type Xoroshiro128Plus::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoroshiro128Plus::state_type Xoroshiro128Plus::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoroshiro128Plus::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoroshiro128Plus::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro128++
	//
	inline constexpr Xoroshiro128PlusPlus::Xoroshiro128PlusPlus(const std::uint64_t seed) noexcept
		: m_state(SplitMix64{ seed }.generateSeedSequence<2>()) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro128PlusPlus>)
	inline constexpr Xoroshiro128PlusPlus::Xoroshiro128PlusPlus(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 4> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state[0] = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		m_state[1] = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoroshiro128PlusPlus::Xoroshiro128PlusPlus(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoroshiro128PlusPlus::result_type Xoroshiro128PlusPlus::operator()() noexcept
	{
		const std::uint64_t s0 = m_state[0];
		std::uint64_t s1 = m_state[1];
		const std::uint64_t result = detail::RotL(s0 + s1, 17) + s0;
		s1 ^= s0;
		m_state[0] = detail::RotL(s0, 49) ^ s1 ^ (s1 << 21);
		m_state[1] = detail::RotL(s1, 28);
		return result;
	}

	inline constexpr void Xoroshiro128PlusPlus::jump() noexcept
	{
		constexpr std::uint64_t JUMP[] = { 0x2bd7a6a6e99c2ddc, 0x0992ccaf6a6fca05 };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;

		for (std::uint64_t jump : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
	}

	inline constexpr void Xoroshiro128PlusPlus::longJump() noexcept
	{
		constexpr std::uint64_t LONG_JUMP[] = { 0x360fd5f2cf8d5d99, 0x9c6e6877736c46e3 };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;

		for (std::uint64_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
	}

	inline constexpr Xoroshiro128PlusPlus::result_type Xoroshiro128PlusPlus::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoroshiro128PlusPlus::result_type Xoroshiro128PlusPlus::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoroshiro128PlusPlus::state_type Xoroshiro128PlusPlus::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoroshiro128PlusPlus::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoroshiro128PlusPlus::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro128**
	//
	inline constexpr Xoroshiro128StarStar::Xoroshiro128StarStar(const std::uint64_t seed) noexcept
		: m_state(SplitMix64{ seed }.generateSeedSequence<2>()) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro128StarStar>)
	inline constexpr Xoroshiro128StarStar::Xoroshiro128StarStar(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 4> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state[0] = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		m_state[1] = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoroshiro128StarStar::Xoroshiro128StarStar(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoroshiro128StarStar::result_type Xoroshiro128StarStar::operator()() noexcept
	{
		const std::uint64_t s0 = m_state[0];
		std::uint64_t s1 = m_state[1];
		const std::uint64_t result = detail::RotL(s0 * 5, 7) * 9;
		s1 ^= s0;
		m_state[0] = detail::RotL(s0, 24) ^ s1 ^ (s1 << 16);
		m_state[1] = detail::RotL(s1, 37);
		return result;
	}

	inline constexpr void Xoroshiro128StarStar::jump() noexcept
	{
		constexpr std::uint64_t JUMP[] = { 0xdf900294d8f554a5, 0x170865df4b3201fc };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;

		for (std::uint64_t jump : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
	}

	inline constexpr void Xoroshiro128StarStar::longJump() noexcept
	{
		constexpr std::uint64_t LONG_JUMP[] = { 0xd2a98b26625eee7b, 0xdddf9b1090aa7ac1 };

		std::uint64_t s0 = 0;
		std::uint64_t s1 = 0;

		for (std::uint64_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (jump & UINT64_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
	}

	inline constexpr Xoroshiro128StarStar::result_type Xoroshiro128StarStar::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoroshiro128StarStar::result_type Xoroshiro128StarStar::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoroshiro128StarStar::state_type Xoroshiro128StarStar::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoroshiro128StarStar::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoroshiro128StarStar::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro128+
	//
	inline constexpr Xoshiro128Plus::Xoshiro128Plus(const std::uint64_t seed) noexcept
		: m_state()
	{
		SplitMix64 splitmix{ seed };

		for (auto& state : m_state)
		{
			state = static_cast<std::uint32_t>(splitmix());
		}
	}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro128Plus>)
	inline constexpr Xoshiro128Plus::Xoshiro128Plus(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 4> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state = seeds;
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoshiro128Plus::Xoshiro128Plus(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoshiro128Plus::result_type Xoshiro128Plus::operator()() noexcept
	{
		const std::uint32_t result = m_state[0] + m_state[3];
		const std::uint32_t t = m_state[1] << 9;
		m_state[2] ^= m_state[0];
		m_state[3] ^= m_state[1];
		m_state[1] ^= m_state[2];
		m_state[0] ^= m_state[3];
		m_state[2] ^= t;
		m_state[3] = detail::RotL(m_state[3], 11);
		return result;
	}

	inline constexpr void Xoshiro128Plus::jump() noexcept
	{
		constexpr std::uint32_t JUMP[] = { 0x8764000b, 0xf542d2d3, 0x6fa035c3, 0x77f2db5b };

		std::uint32_t s0 = 0;
		std::uint32_t s1 = 0;
		std::uint32_t s2 = 0;
		std::uint32_t s3 = 0;

		for (std::uint32_t jump : JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (jump & UINT32_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr void Xoshiro128Plus::longJump() noexcept
	{
		constexpr std::uint32_t LONG_JUMP[] = { 0xb523952e, 0x0b6f099f, 0xccf5a0ef, 0x1c580662 };

		std::uint32_t s0 = 0;
		std::uint32_t s1 = 0;
		std::uint32_t s2 = 0;
		std::uint32_t s3 = 0;

		for (std::uint32_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (jump & UINT32_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr Xoshiro128Plus::result_type Xoshiro128Plus::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoshiro128Plus::result_type Xoshiro128Plus::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoshiro128Plus::state_type Xoshiro128Plus::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoshiro128Plus::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoshiro128Plus::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro128++
	//
	inline constexpr Xoshiro128PlusPlus::Xoshiro128PlusPlus(const std::uint64_t seed) noexcept
		: m_state()
	{
		SplitMix64 splitmix{ seed };

		for (auto& state : m_state)
		{
			state = static_cast<std::uint32_t>(splitmix());
		}
	}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro128PlusPlus>)
	inline constexpr Xoshiro128PlusPlus::Xoshiro128PlusPlus(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 4> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state = seeds;
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoshiro128PlusPlus::Xoshiro128PlusPlus(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoshiro128PlusPlus::result_type Xoshiro128PlusPlus::operator()() noexcept
	{
		const std::uint32_t result = detail::RotL(m_state[0] + m_state[3], 7) + m_state[0];
		const std::uint32_t t = m_state[1] << 9;
		m_state[2] ^= m_state[0];
		m_state[3] ^= m_state[1];
		m_state[1] ^= m_state[2];
		m_state[0] ^= m_state[3];
		m_state[2] ^= t;
		m_state[3] = detail::RotL(m_state[3], 11);
		return result;
	}

	inline constexpr void Xoshiro128PlusPlus::jump() noexcept
	{
		constexpr std::uint32_t JUMP[] = { 0x8764000b, 0xf542d2d3, 0x6fa035c3, 0x77f2db5b };

		std::uint32_t s0 = 0;
		std::uint32_t s1 = 0;
		std::uint32_t s2 = 0;
		std::uint32_t s3 = 0;

		for (std::uint32_t jump : JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (jump & UINT32_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr void Xoshiro128PlusPlus::longJump() noexcept
	{
		constexpr std::uint32_t LONG_JUMP[] = { 0xb523952e, 0x0b6f099f, 0xccf5a0ef, 0x1c580662 };

		std::uint32_t s0 = 0;
		std::uint32_t s1 = 0;
		std::uint32_t s2 = 0;
		std::uint32_t s3 = 0;

		for (std::uint32_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (jump & UINT32_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr Xoshiro128PlusPlus::result_type Xoshiro128PlusPlus::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoshiro128PlusPlus::result_type Xoshiro128PlusPlus::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoshiro128PlusPlus::state_type Xoshiro128PlusPlus::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoshiro128PlusPlus::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoshiro128PlusPlus::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro128**
	//
	inline constexpr Xoshiro128StarStar::Xoshiro128StarStar(const std::uint64_t seed) noexcept
		: m_state()
	{
		SplitMix64 splitmix{ seed };

		for (auto& state : m_state)
		{
			state = static_cast<std::uint32_t>(splitmix());
		}
	}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro128StarStar>)
	inline constexpr Xoshiro128StarStar::Xoshiro128StarStar(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 4> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state = seeds;
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoshiro128StarStar::Xoshiro128StarStar(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoshiro128StarStar::result_type Xoshiro128StarStar::operator()() noexcept
	{
		const std::uint32_t result = detail::RotL(m_state[1] * 5, 7) * 9;
		const std::uint32_t t = m_state[1] << 9;
		m_state[2] ^= m_state[0];
		m_state[3] ^= m_state[1];
		m_state[1] ^= m_state[2];
		m_state[0] ^= m_state[3];
		m_state[2] ^= t;
		m_state[3] = detail::RotL(m_state[3], 11);
		return result;
	}

	inline constexpr void Xoshiro128StarStar::jump() noexcept
	{
		constexpr std::uint32_t JUMP[] = { 0x8764000b, 0xf542d2d3, 0x6fa035c3, 0x77f2db5b };

		std::uint32_t s0 = 0;
		std::uint32_t s1 = 0;
		std::uint32_t s2 = 0;
		std::uint32_t s3 = 0;

		for (std::uint32_t jump : JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (jump & UINT32_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr void Xoshiro128StarStar::longJump() noexcept
	{
		constexpr std::uint32_t LONG_JUMP[] = { 0xb523952e, 0x0b6f099f, 0xccf5a0ef, 0x1c580662 };

		std::uint32_t s0 = 0;
		std::uint32_t s1 = 0;
		std::uint32_t s2 = 0;
		std::uint32_t s3 = 0;

		for (std::uint32_t jump : LONG_JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (jump & UINT32_C(1) << b)
				{
					s0 ^= m_state[0];
					s1 ^= m_state[1];
					s2 ^= m_state[2];
					s3 ^= m_state[3];
				}
				operator()();
			}
		}

		m_state[0] = s0;
		m_state[1] = s1;
		m_state[2] = s2;
		m_state[3] = s3;
	}

	inline constexpr Xoshiro128StarStar::result_type Xoshiro128StarStar::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoshiro128StarStar::result_type Xoshiro128StarStar::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoshiro128StarStar::state_type Xoshiro128StarStar::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoshiro128StarStar::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoshiro128StarStar::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro64*
	//
	inline constexpr Xoroshiro64Star::Xoroshiro64Star(const std::uint64_t seed) noexcept
		: m_state()
	{
		SplitMix64 splitmix{ seed };

		for (auto& state : m_state)
		{
			state = static_cast<std::uint32_t>(splitmix());
		}
	}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro64Star>)
	inline constexpr Xoroshiro64Star::Xoroshiro64Star(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 2> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state = seeds;
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoroshiro64Star::Xoroshiro64Star(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoroshiro64Star::result_type Xoroshiro64Star::operator()() noexcept
	{
		const std::uint32_t s0 = m_state[0];
		std::uint32_t s1 = m_state[1];

		const std::uint32_t result = s0 * 0x9E3779BB;

		s1 ^= s0;
		m_state[0] = detail::RotL(s0, 26) ^ s1 ^ (s1 << 9);
		m_state[1] = detail::RotL(s1, 13);

		return result;
	}

	inline constexpr Xoroshiro64Star::result_type Xoroshiro64Star::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoroshiro64Star::result_type Xoroshiro64Star::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoroshiro64Star::state_type Xoroshiro64Star::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoroshiro64Star::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoroshiro64Star::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro64**
	//
	inline constexpr Xoroshiro64StarStar::Xoroshiro64StarStar(const std::uint64_t seed) noexcept
		: m_state()
	{
		SplitMix64 splitmix{ seed };

		for (auto& state : m_state)
		{
			state = static_cast<std::uint32_t>(splitmix());
		}
	}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro64StarStar>)
	inline constexpr Xoroshiro64StarStar::Xoroshiro64StarStar(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 2> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_state = seeds;
		if (detail::IsAllZero(m_state)) m_state[0] = 1;
	}

	inline constexpr Xoroshiro64StarStar::Xoroshiro64StarStar(const state_type state) noexcept
		: m_state(state)
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr Xoroshiro64StarStar::result_type Xoroshiro64StarStar::operator()() noexcept
	{
		const std::uint32_t s0 = m_state[0];
		std::uint32_t s1 = m_state[1];

		const std::uint32_t result = detail::RotL(s0 * 0x9E3779BB, 5) * 5;

		s1 ^= s0;
		m_state[0] = detail::RotL(s0, 26) ^ s1 ^ (s1 << 9);
		m_state[1] = detail::RotL(s1, 13);

		return result;
	}

	inline constexpr Xoroshiro64StarStar::result_type Xoroshiro64StarStar::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr Xoroshiro64StarStar::result_type Xoroshiro64StarStar::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr Xoroshiro64StarStar::state_type Xoroshiro64StarStar::serialize() const noexcept
	{
		return m_state;
	}

	inline constexpr void Xoroshiro64StarStar::deserialize(const state_type state) noexcept
	{
		assert(!detail::IsAllZero(state) && "全零状态是吸收态，禁止使用");
		m_state = state;
	}

	inline constexpr void Xoroshiro64StarStar::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}


	////////////////////////////////////////////////////////////////
	//
	//	SFC64 (Small Fast Counter)
	//
	inline constexpr SFC64::SFC64(const std::uint64_t seed) noexcept
		: m_a(0), m_b(0), m_c(0), m_counter(1)
	{
		// 使用 SplitMix64 播种 + 12 轮预热
		SplitMix64 sm{ seed };
		m_a = sm();
		m_b = sm();
		m_c = sm();
		for (int i = 0; i < 12; ++i) { operator()(); }
	}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, SFC64>)
	inline constexpr SFC64::SFC64(SeedSeq& seq) noexcept
		: m_counter(1)
	{
		std::array<std::uint32_t, 8> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_a = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		m_b = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
		m_c = (static_cast<std::uint64_t>(seeds[4]) << 32) | seeds[5];
	}

	inline constexpr SFC64::SFC64(const state_type state) noexcept
		: m_a(state[0]), m_b(state[1]), m_c(state[2]), m_counter(state[3]) {}

	inline constexpr SFC64::result_type SFC64::operator()() noexcept
	{
		const std::uint64_t tmp = m_a + m_b + m_counter++;
		m_a = m_b ^ (m_b >> 11);
		m_b = m_c + (m_c << 3);
		m_c = detail::RotL(m_c, 24) + tmp;
		return tmp;
	}

	inline constexpr SFC64::result_type SFC64::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr SFC64::result_type SFC64::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr SFC64::state_type SFC64::serialize() const noexcept
	{
		return { m_a, m_b, m_c, m_counter };
	}

	inline constexpr void SFC64::deserialize(const state_type state) noexcept
	{
		m_a = state[0];
		m_b = state[1];
		m_c = state[2];
		m_counter = state[3];
	}

	inline constexpr void SFC64::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	RomuDuoJr
	//
	inline constexpr RomuDuoJr::RomuDuoJr(const std::uint64_t seed) noexcept
		: m_x(0), m_y(0)
	{
		SplitMix64 sm{ seed };
		m_x = sm();
		m_y = sm();
		// 确保不全零
		if (m_x == 0 && m_y == 0) { m_x = 1; }
	}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, RomuDuoJr>)
	inline constexpr RomuDuoJr::RomuDuoJr(SeedSeq& seq) noexcept
	{
		std::array<std::uint32_t, 4> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_x = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		m_y = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
		if (m_x == 0 && m_y == 0) m_x = 1;
	}

	inline constexpr RomuDuoJr::RomuDuoJr(const state_type state) noexcept
		: m_x(state[0]), m_y(state[1])
	{
		assert(!(m_x == 0 && m_y == 0) && "全零状态是吸收态，禁止使用");
	}

	inline constexpr RomuDuoJr::result_type RomuDuoJr::operator()() noexcept
	{
		const std::uint64_t xp = m_x;
		m_x = 15241094284759029579ULL * m_y;
		m_y = detail::RotL(m_y - xp, 27);
		return xp;
	}

	inline constexpr RomuDuoJr::result_type RomuDuoJr::min() noexcept
	{
		return std::numeric_limits<result_type>::lowest();
	}

	inline constexpr RomuDuoJr::result_type RomuDuoJr::max() noexcept
	{
		return std::numeric_limits<result_type>::max();
	}

	inline constexpr RomuDuoJr::state_type RomuDuoJr::serialize() const noexcept
	{
		return { m_x, m_y };
	}

	inline constexpr void RomuDuoJr::deserialize(const state_type state) noexcept
	{
		assert(!(state[0] == 0 && state[1] == 0) && "全零状态是吸收态，禁止使用");
		m_x = state[0];
		m_y = state[1];
	}

	inline constexpr void RomuDuoJr::discard(const unsigned long long n) noexcept
	{
		for (unsigned long long i = 0; i < n; ++i) { operator()(); }
	}

	////////////////////////////////////////////////////////////////
	//
	//	便捷工具函数
	//

	// 默认线程局部引擎，使用 std::random_device 播种
	[[nodiscard]]
	inline Xoshiro256StarStar& DefaultEngine()
	{
		thread_local Xoshiro256StarStar engine{ [] {
			std::random_device rd;
			return (static_cast<std::uint64_t>(rd()) << 32) | rd();
		}() };
		return engine;
	}

	// 生成非确定性的 64 位种子（优先硬件 RNG）
	[[nodiscard]]
	inline std::uint64_t RandomSeed()
	{
		std::uint64_t hw;
		if (detail::HardwareRand64(hw))
			return hw;
		std::random_device rd;
		return (static_cast<std::uint64_t>(rd()) << 32) | rd();
	}

	// 重置默认引擎的种子（用于测试复现）
	inline void Reseed(std::uint64_t seed)
	{
		DefaultEngine().deserialize(Xoshiro256StarStar{ seed }.serialize());
	}

	// 重置为真随机种子
	inline void ReseedRandom()
	{
		DefaultEngine().deserialize(Xoshiro256StarStar{ RandomSeed() }.serialize());
	}

	// 生成 [min, max] 范围内的随机整数
	template <std::integral T = int>
	[[nodiscard]]
	inline T RandInt(T min, T max)
	{
		std::uniform_int_distribution<T> dist(min, max);
		return dist(DefaultEngine());
	}

	// 生成 [0, max] 范围内的随机整数
	template <std::integral T = int>
	[[nodiscard]]
	inline T RandInt(T max)
	{
		return RandInt<T>(T{0}, max);
	}

	// 生成 [min, max) 范围内的随机浮点数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandReal(T min = T{0}, T max = T{1})
	{
		std::uniform_real_distribution<T> dist(min, max);
		return dist(DefaultEngine());
	}

	// 生成随机布尔值，为 true 的概率为 p
	[[nodiscard]]
	inline bool RandBool(double p = 0.5)
	{
		return RandReal<double>(0.0, 1.0) < p;
	}

	// 从容器中随机取一个元素
	template <class Container>
	[[nodiscard]]
	inline decltype(auto) RandElement(Container&& c)
	{
		using Size = typename std::remove_reference_t<Container>::size_type;
		return c[RandInt<Size>(static_cast<Size>(c.size() - 1))];
	}


	// 生成正态分布随机数（默认均值 0，标准差 1）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandNormal(T mean = T{0}, T stddev = T{1})
	{
		std::normal_distribution<T> dist(mean, stddev);
		return dist(DefaultEngine());
	}

	// 随机打乱容器
	template <class Container>
	inline void RandShuffle(Container&& c)
	{
		std::shuffle(c.begin(), c.end(), DefaultEngine());
	}

	// 按权重随机选取索引（权重容器元素为数值类型）
	template <class WeightContainer>
	[[nodiscard]]
	inline typename WeightContainer::size_type RandWeighted(const WeightContainer& weights)
	{
		using Size = typename WeightContainer::size_type;
		std::discrete_distribution<Size> dist(weights.begin(), weights.end());
		return dist(DefaultEngine());
	}

	// 指定引擎的重载版本
	template <std::integral T, class Engine>
	[[nodiscard]]
	inline T RandInt(Engine& engine, T min, T max)
	{
		std::uniform_int_distribution<T> dist(min, max);
		return dist(engine);
	}

	template <std::floating_point T, class Engine>
	[[nodiscard]]
	inline T RandReal(Engine& engine, T min = T{0}, T max = T{1})
	{
		std::uniform_real_distribution<T> dist(min, max);
		return dist(engine);
	}



	////////////////////////////////////////////////////////////////
	//
	//	多流接口（并行计算）
	//

	// 从同一种子创建第 streamId 个不重叠子序列的引擎
	// 每个流之间间隔 2^128 步（xoshiro256）或 2^64 步（xoroshiro128/xoshiro128）
	// 注意：Xoroshiro64 系列无 jump 函数，不支持多流
	template <class Engine>
	[[nodiscard]]
	inline constexpr Engine MakeStreamEngine(std::uint64_t streamId, std::uint64_t seed = DefaultSeed)
	{
		Engine rng{ seed };
		for (std::uint64_t i = 0; i < streamId; ++i)
			rng.jump();
		return rng;
	}

	////////////////////////////////////////////////////////////////
	//
	//	编译期随机（constexpr）
	//

	namespace detail
	{
#ifdef __SIZEOF_INT128__
		// Lemire 快速有界法：返回 [0, range) 内均匀分布的随机数，无模偏差
		// 使用 __uint128_t（GCC/Clang constexpr 友好）
		[[nodiscard]]
		inline constexpr std::uint64_t BoundedRand(Xoshiro256StarStar& rng, std::uint64_t range) noexcept
		{
			if (range == 0) return 0;
			__uint128_t product = static_cast<__uint128_t>(rng()) * range;
			std::uint64_t low = static_cast<std::uint64_t>(product);
			if (low < range)
			{
				const std::uint64_t threshold = (0ULL - range) % range;
				while (low < threshold)
				{
					product = static_cast<__uint128_t>(rng()) * range;
					low = static_cast<std::uint64_t>(product);
				}
			}
			return static_cast<std::uint64_t>(product >> 64);
		}
#else
		// 拒绝采样回退（MSVC 无 __uint128_t）
		[[nodiscard]]
		inline constexpr std::uint64_t BoundedRand(Xoshiro256StarStar& rng, std::uint64_t range) noexcept
		{
			if (range == 0) return 0;
			const std::uint64_t threshold = (0ULL - range) % range;
			std::uint64_t r;
			do { r = rng(); } while (r < threshold);
			return r % range;
		}
#endif
	}

	// 编译期生成 [min, max] 范围内的随机整数（使用固定种子，结果在编译期确定）
	// 使用 Lemire 有界法消除模偏差
	template <std::integral T = int, std::uint64_t Seed = DefaultSeed>
	[[nodiscard]]
	inline constexpr T RandIntCE(T min, T max) noexcept
	{
		Xoshiro256StarStar rng{ Seed };
		const auto range = static_cast<std::uint64_t>(max - min) + 1;
		return static_cast<T>(static_cast<std::uint64_t>(min) + detail::BoundedRand(rng, range));
	}

	// 编译期生成 [0, max] 范围内的随机整数
	template <std::integral T = int, std::uint64_t Seed = DefaultSeed>
	[[nodiscard]]
	inline constexpr T RandIntCE(T max) noexcept
	{
		return RandIntCE<T, Seed>(T{0}, max);
	}

	////////////////////////////////////////////////////////////////
	//
	//	编译期洗牌（constexpr）
	//

	// 编译期 Fisher-Yates 洗牌（std::shuffle 在 C++23 中仍非 constexpr）
	template <std::random_access_iterator It, std::uint64_t Seed = DefaultSeed>
	constexpr void ShuffleCE(It first, It last) noexcept
	{
		Xoshiro256StarStar rng{ Seed };
		const auto n = static_cast<std::uint64_t>(last - first);
		for (std::uint64_t i = n - 1; i > 0; --i)
		{
			const auto j = detail::BoundedRand(rng, i + 1);
			if (i != j)
			{
				auto tmp = std::move(first[i]);
				first[i] = std::move(first[j]);
				first[j] = std::move(tmp);
			}
		}
	}

	// 编译期洗牌数组版本（返回打乱后的副本）
	template <class T, std::size_t N, std::uint64_t Seed = DefaultSeed>
	[[nodiscard]]
	constexpr std::array<T, N> ShuffledArray(std::array<T, N> arr) noexcept
	{
		ShuffleCE<decltype(arr.begin()), Seed>(arr.begin(), arr.end());
		return arr;
	}

	////////////////////////////////////////////////////////////////
	//
	//	静态断言：确认引擎满足 uniform_random_bit_generator 概念
	//

	static_assert(std::uniform_random_bit_generator<SplitMix64>);
	static_assert(std::uniform_random_bit_generator<Xoshiro256Plus>);
	static_assert(std::uniform_random_bit_generator<Xoshiro256PlusPlus>);
	static_assert(std::uniform_random_bit_generator<Xoshiro256StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro128Plus>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro128PlusPlus>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro128StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoshiro128Plus>);
	static_assert(std::uniform_random_bit_generator<Xoshiro128PlusPlus>);
	static_assert(std::uniform_random_bit_generator<Xoshiro128StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro64Star>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro64StarStar>);
	static_assert(std::uniform_random_bit_generator<SFC64>);
	static_assert(std::uniform_random_bit_generator<RomuDuoJr>);

}
