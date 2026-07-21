//----------------------------------------------------------------------------------------
//
//	RandX_Cpp17.hpp — 基于 Xoshiro 的伪随机数生成器封装库（C++17 / C++23）
//
//	原始算法：David Blackman & Sebastiano Vigna (http://prng.di.unimi.it/)
//	原始 C++ 封装：Ryo Suzuki (https://github.com/Reputeless/Xoshiro-cpp)
//
//========================================================================================
//
//	快速上手
//
//		#include “RandX_Cpp17.hpp”
//
//		// 最简用法：直接调用便捷函数（内部使用线程局部 Xoshiro256StarStar）
//		int   dice  = RandX::RandInt(1, 6);     // [1, 6] 闭区间整数
//		double coin = RandX::RandReal();         // [0.0, 1.0) 浮点数
//		bool  flag  = RandX::RandBool(0.3);      // 30% 概率为 true
//
//		std::vector<int> v = {10, 20, 30, 40};
//		auto& elem = RandX::RandElement(v);      // 随机取一个元素
//
//	扩展 API
//
//		auto sample = RandX::RandSample(v, 2);   // 无放回抽样 2 个
//		auto perm   = RandX::RandPermutation(10);// [0,10) 随机排列
//		auto token  = RandX::RandString(16);     // 16 位随机字符串
//		auto uuid   = RandX::RandUUID();         // UUID v4
//		auto byte   = RandX::RandBits<8>();      // [0, 256) 随机整数
//		auto exp    = RandX::RandExp(2.0);       // 指数分布 λ=2
//		auto poi    = RandX::RandPoisson(5.0);   // 泊松分布 μ=5
//
//	手动管理引擎
//
//		RandX::Xoshiro256StarStar rng{ RandX::RandomSeed() };
//		int val = RandX::RandInt(rng, 0, 99);
//
//		// 配合标准库 distribution（满足 UniformRandomBitGenerator）
//		std::normal_distribution<double> norm(0.0, 1.0);
//		double sample = norm(rng);
//
//	多流并行
//
//		auto s0 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(0);
//		auto s1 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(1);
//
//	序列化 / 反序列化
//
//		auto state = rng.serialize();
//		rng.deserialize(state);
//
//	跳跃
//
//		rng.jump();      // 前进 2^128 步（xoshiro256 系列）
//		rng.longJump();  // 前进 2^192 步
//		rng.discard(1000);
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
//		SFC64                  64-bit  >= 2^64    32B    速度极快，通过 PractRand
//		RomuDuoJr              64-bit  >= 2^51    16B    极简极快，非关键模拟
//
//----------------------------------------------------------------------------------------

# pragma once
# include <cstdint>
# include <array>
# include <limits>
# include <type_traits>
# include <random>
# include <algorithm>
# include <cassert>
# include <string>
# include <string_view>
# include <unordered_set>
# include <vector>
# include <stdexcept>
# if defined(_MSC_VER) && (defined(__x86_64__) || defined(_M_X64))
#	include <immintrin.h>
# endif
# if __has_cpp_attribute(nodiscard) >= 201907L
#	define RANDX_NODISCARD_CXX20 [[nodiscard]]
# else
#	define RANDX_NODISCARD_CXX20
# endif

namespace RandX
{
	// 生成器的默认种子值
	inline constexpr std::uint64_t DefaultSeed = 1234567890ULL;

	// 将给定的 uint32 值 `i` 转换为 32 位浮点
	// 范围在 [0.0f, 1.0f) 的数值
	template <class Uint32, std::enable_if_t<std::is_same_v<Uint32, std::uint32_t>>* = nullptr>
	[[nodiscard]]
	inline constexpr float FloatFromBits(Uint32 i) noexcept;

	// 将给定的 uint64 值 `i` 转换为 64 位浮点
	// 范围在 [0.0, 1.0) 的数值
	template <class Uint64, std::enable_if_t<std::is_same_v<Uint64, std::uint64_t>>* = nullptr>
	[[nodiscard]]
	inline constexpr double DoubleFromBits(Uint64 i) noexcept;

	// SplitMix64
	// 输出：64 位
	// 周期：2^64
	// 占用：8 字节
	// 原始实现：http://prng.di.unimi.it/splitmix64.c
	class SplitMix64
	{
	public:

		using state_type	= std::uint64_t;	
		using result_type	= std::uint64_t;
		
		RANDX_NODISCARD_CXX20
		explicit constexpr SplitMix64(state_type state = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, SplitMix64>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr SplitMix64(SeedSeq& seq);

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

		[[nodiscard]]
		friend bool operator ==(const SplitMix64& lhs, const SplitMix64& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const SplitMix64& lhs, const SplitMix64& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}
	
	private:

		state_type m_state;
	};

	// xoshiro256+
	// 输出：64 位
	// 周期：2^256 - 1
	// 占用：32 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro256plus.c
	// 版本：1.0
	class Xoshiro256Plus
	{
	public:

		using state_type	= std::array<std::uint64_t, 4>;
		using result_type	= std::uint64_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256Plus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro256Plus>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256Plus(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256Plus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^128 次 operator()；可用于生成 2^128 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^192 次 next()；可用于生成 2^64 个起始点，
		// 每个起始点通过 jump() 可生成 2^64 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoshiro256Plus& lhs, const Xoshiro256Plus& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoshiro256Plus& lhs, const Xoshiro256Plus& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoshiro256++
	// 输出：64 位
	// 周期：2^256 - 1
	// 占用：32 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro256plusplus.c
	// 版本：1.0
	class Xoshiro256PlusPlus
	{
	public:

		using state_type	= std::array<std::uint64_t, 4>;
		using result_type	= std::uint64_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256PlusPlus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro256PlusPlus>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256PlusPlus(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256PlusPlus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^128 次 next()；可用于生成 2^128 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^192 次 next()；可用于生成 2^64 个起始点，
		// 每个起始点通过 jump() 可生成 2^64 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoshiro256PlusPlus& lhs, const Xoshiro256PlusPlus& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoshiro256PlusPlus& lhs, const Xoshiro256PlusPlus& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoshiro256**
	// 输出：64 位
	// 周期：2^256 - 1
	// 占用：32 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro256starstar.c
	// 版本：1.0
	class Xoshiro256StarStar
	{
	public:

		using state_type	= std::array<std::uint64_t, 4>;
		using result_type	= std::uint64_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256StarStar(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro256StarStar>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256StarStar(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256StarStar(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^128 次 next()；可用于生成 2^128 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^192 次 next()；可用于生成 2^64 个起始点，
		// 每个起始点通过 jump() 可生成 2^64 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoshiro256StarStar& lhs, const Xoshiro256StarStar& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoshiro256StarStar& lhs, const Xoshiro256StarStar& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoroshiro128+
	// 输出：64 位
	// 周期：2^128 - 1
	// 占用：16 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro128plus.c
	// 版本：1.0
	class Xoroshiro128Plus
	{
	public:

		using state_type	= std::array<std::uint64_t, 2>;
		using result_type	= std::uint64_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128Plus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro128Plus>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128Plus(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128Plus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^64 次 next()；可用于生成 2^64 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^96 次 next()；可用于生成 2^32 个起始点，
		// 每个起始点通过 jump() 可生成 2^32 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoroshiro128Plus& lhs, const Xoroshiro128Plus& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoroshiro128Plus& lhs, const Xoroshiro128Plus& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoroshiro128++
	// 输出：64 位
	// 周期：2^128 - 1
	// 占用：16 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro128plusplus.c
	// 版本：1.0
	class Xoroshiro128PlusPlus
	{
	public:

		using state_type	= std::array<std::uint64_t, 2>;
		using result_type	= std::uint64_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128PlusPlus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro128PlusPlus>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128PlusPlus(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128PlusPlus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^64 次 next()；可用于生成 2^64 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^96 次 next()；可用于生成 2^32 个起始点，
		// 每个起始点通过 jump() 可生成 2^32 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoroshiro128PlusPlus& lhs, const Xoroshiro128PlusPlus& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoroshiro128PlusPlus& lhs, const Xoroshiro128PlusPlus& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoroshiro128**
	// 输出：64 位
	// 周期：2^128 - 1
	// 占用：16 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro128starstar.c
	// 版本：1.0
	class Xoroshiro128StarStar
	{
	public:

		using state_type	= std::array<std::uint64_t, 2>;
		using result_type	= std::uint64_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128StarStar(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro128StarStar>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128StarStar(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128StarStar(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^64 次 next()；可用于生成 2^64 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^96 次 next()；可用于生成 2^32 个起始点，
		// 每个起始点通过 jump() 可生成 2^32 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoroshiro128StarStar& lhs, const Xoroshiro128StarStar& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoroshiro128StarStar& lhs, const Xoroshiro128StarStar& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoshiro128+
	// 输出：32 位
	// 周期：2^128 - 1
	// 占用：16 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro128plus.c
	// 版本：1.0
	class Xoshiro128Plus
	{
	public:

		using state_type	= std::array<std::uint32_t, 4>;
		using result_type	= std::uint32_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128Plus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro128Plus>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128Plus(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128Plus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^64 次 next()；可用于生成 2^64 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^96 次 next()；可用于生成 2^32 个起始点，
		// 每个起始点通过 jump() 可生成 2^32 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoshiro128Plus& lhs, const Xoshiro128Plus& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoshiro128Plus& lhs, const Xoshiro128Plus& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoshiro128++
	// 输出：32 位
	// 周期：2^128 - 1
	// 占用：16 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro128plusplus.c
	// 版本：1.0
	class Xoshiro128PlusPlus
	{
	public:

		using state_type	= std::array<std::uint32_t, 4>;
		using result_type	= std::uint32_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128PlusPlus(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro128PlusPlus>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128PlusPlus(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128PlusPlus(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^64 次 next()；可用于生成 2^64 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^96 次 next()；可用于生成 2^32 个起始点，
		// 每个起始点通过 jump() 可生成 2^32 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoshiro128PlusPlus& lhs, const Xoshiro128PlusPlus& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoshiro128PlusPlus& lhs, const Xoshiro128PlusPlus& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoshiro128**
	// 输出：32 位
	// 周期：2^128 - 1
	// 占用：16 字节
	// 原始实现：http://prng.di.unimi.it/xoshiro128starstar.c
	// 版本：1.1
	class Xoshiro128StarStar
	{
	public:

		using state_type	= std::array<std::uint32_t, 4>;
		using result_type	= std::uint32_t;

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128StarStar(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro128StarStar>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128StarStar(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128StarStar(state_type state) noexcept;

		constexpr result_type operator()() noexcept;

		constexpr void discard(unsigned long long n) noexcept;

		// 这是该生成器的 jump 函数。它等价于
		// 调用 2^64 次 next()；可用于生成 2^64 个
		// 互不重叠的子序列，用于并行计算。
		constexpr void jump() noexcept;

		// 这是该生成器的 long-jump 函数。它等价于
		// 调用 2^96 次 next()；可用于生成 2^32 个起始点，
		// 每个起始点通过 jump() 可生成 2^32 个互不重叠的
		// 子序列，用于分布式并行计算。
		constexpr void longJump() noexcept;

		[[nodiscard]]
		static constexpr result_type min() noexcept;

		[[nodiscard]]
		static constexpr result_type max() noexcept;

		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		constexpr void deserialize(state_type state) noexcept;

		[[nodiscard]]
		friend bool operator ==(const Xoshiro128StarStar& lhs, const Xoshiro128StarStar& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoshiro128StarStar& lhs, const Xoshiro128StarStar& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoroshiro64*
	// 输出：32 位
	// 周期：2^64 - 1
	// 占用：8 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro64star.c
	class Xoroshiro64Star
	{
	public:

		using state_type = std::array<std::uint32_t, 2>;
		using result_type = std::uint32_t;

		RANDX_NODISCARD_CXX20
			explicit constexpr Xoroshiro64Star(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro64Star>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro64Star(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
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

		[[nodiscard]]
		friend bool operator ==(const Xoroshiro64Star& lhs, const Xoroshiro64Star& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoroshiro64Star& lhs, const Xoroshiro64Star& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

	private:

		state_type m_state;
	};

	// xoroshiro64**
	// 输出：32 位
	// 周期：2^64 - 1
	// 占用：8 字节
	// 原始实现：http://prng.di.unimi.it/xoroshiro64starstar.c
	class Xoroshiro64StarStar
	{
	public:

		using state_type = std::array<std::uint32_t, 2>;
		using result_type = std::uint32_t;

		RANDX_NODISCARD_CXX20
			explicit constexpr Xoroshiro64StarStar(std::uint64_t seed = DefaultSeed) noexcept;

		// 从 std::seed_seq 播种
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro64StarStar>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro64StarStar(SeedSeq& seq);

		RANDX_NODISCARD_CXX20
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

		[[nodiscard]]
		friend bool operator ==(const Xoroshiro64StarStar& lhs, const Xoroshiro64StarStar& rhs) noexcept
		{
			return (lhs.m_state == rhs.m_state);
		}

		[[nodiscard]]
		friend bool operator !=(const Xoroshiro64StarStar& lhs, const Xoroshiro64StarStar& rhs) noexcept
		{
			return (lhs.m_state != rhs.m_state);
		}

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
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, SFC64>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr SFC64(SeedSeq& seq);

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

		[[nodiscard]]
		friend bool operator ==(const SFC64& lhs, const SFC64& rhs) noexcept
		{
			return (lhs.serialize() == rhs.serialize());
		}

		[[nodiscard]]
		friend bool operator !=(const SFC64& lhs, const SFC64& rhs) noexcept
		{
			return (lhs.serialize() != rhs.serialize());
		}

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
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, RomuDuoJr>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr RomuDuoJr(SeedSeq& seq);

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

		[[nodiscard]]
		friend bool operator ==(const RomuDuoJr& lhs, const RomuDuoJr& rhs) noexcept
		{
			return (lhs.serialize() == rhs.serialize());
		}

		[[nodiscard]]
		friend bool operator !=(const RomuDuoJr& lhs, const RomuDuoJr& rhs) noexcept
		{
			return (lhs.serialize() != rhs.serialize());
		}

	private:

		std::uint64_t m_x;
		std::uint64_t m_y;
	};
}

////////////////////////////////////////////////////////////////

namespace RandX
{
	template <class Uint32, std::enable_if_t<std::is_same_v<Uint32, std::uint32_t>>*>
	inline constexpr float FloatFromBits(const Uint32 i) noexcept
	{
		return (i >> 8) * 0x1.0p-24f;
	}

	template <class Uint64, std::enable_if_t<std::is_same_v<Uint64, std::uint64_t>>*>
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, SplitMix64>>*>
	inline constexpr SplitMix64::SplitMix64(SeedSeq& seq)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro256Plus>>*>
	inline constexpr Xoshiro256Plus::Xoshiro256Plus(SeedSeq& seq)
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

		for (std::uint64_t j : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

		for (std::uint64_t j : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro256PlusPlus>>*>
	inline constexpr Xoshiro256PlusPlus::Xoshiro256PlusPlus(SeedSeq& seq)
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

		for (std::uint64_t j : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

		for (std::uint64_t j : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro256StarStar>>*>
	inline constexpr Xoshiro256StarStar::Xoshiro256StarStar(SeedSeq& seq)
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

		for (std::uint64_t j : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

		for (std::uint64_t j : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro128Plus>>*>
	inline constexpr Xoroshiro128Plus::Xoroshiro128Plus(SeedSeq& seq)
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

		for (std::uint64_t j : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

		for (std::uint64_t j : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro128PlusPlus>>*>
	inline constexpr Xoroshiro128PlusPlus::Xoroshiro128PlusPlus(SeedSeq& seq)
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

		for (std::uint64_t j : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

		for (std::uint64_t j : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro128StarStar>>*>
	inline constexpr Xoroshiro128StarStar::Xoroshiro128StarStar(SeedSeq& seq)
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

		for (std::uint64_t j : JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

		for (std::uint64_t j : LONG_JUMP)
		{
			for (int b = 0; b < 64; ++b)
			{
				if (j & UINT64_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro128Plus>>*>
	inline constexpr Xoshiro128Plus::Xoshiro128Plus(SeedSeq& seq)
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

		for (std::uint32_t j : JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (j & UINT32_C(1) << b)
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

		for (std::uint32_t j : LONG_JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (j & UINT32_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro128PlusPlus>>*>
	inline constexpr Xoshiro128PlusPlus::Xoshiro128PlusPlus(SeedSeq& seq)
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

		for (std::uint32_t j : JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (j & UINT32_C(1) << b)
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

		for (std::uint32_t j : LONG_JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (j & UINT32_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro128StarStar>>*>
	inline constexpr Xoshiro128StarStar::Xoshiro128StarStar(SeedSeq& seq)
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

		for (std::uint32_t j : JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (j & UINT32_C(1) << b)
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

		for (std::uint32_t j : LONG_JUMP)
		{
			for (int b = 0; b < 32; ++b)
			{
				if (j & UINT32_C(1) << b)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro64Star>>*>
	inline constexpr Xoroshiro64Star::Xoroshiro64Star(SeedSeq& seq)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro64StarStar>>*>
	inline constexpr Xoroshiro64StarStar::Xoroshiro64StarStar(SeedSeq& seq)
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, SFC64>>*>
	inline constexpr SFC64::SFC64(SeedSeq& seq)
		: m_counter(1)
	{
		std::array<std::uint32_t, 8> seeds;
		seq.generate(seeds.begin(), seeds.end());
		m_a = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		m_b = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
		m_c = (static_cast<std::uint64_t>(seeds[4]) << 32) | seeds[5];
		// 全零状态会导致输出可预测，强制修正
		if ((m_a | m_b | m_c) == 0) m_a = 0x9E3779B97F4A7C15ULL;
		// 与种子构造函数一致：12 轮预热
		for (int i = 0; i < 12; ++i) { operator()(); }
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

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, RomuDuoJr>>*>
	inline constexpr RomuDuoJr::RomuDuoJr(SeedSeq& seq)
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
	//	多流接口（并行计算）
	//

	// 从同一种子创建第 streamId 个不重叠子序列的引擎
	// 每个流之间间隔 2^128 步（xoshiro256）或 2^64 步（xoroshiro128/xoshiro128）
	// 注意：Xoroshiro64 系列无 jump 函数，不支持多流
	namespace detail
	{
		template <class Engine, class = void>
		struct HasJump : std::false_type {};
		template <class Engine>
		struct HasJump<Engine, std::void_t<decltype(std::declval<Engine&>().jump())>> : std::true_type {};
	}

	namespace detail
	{
		// 字符类型检测（char/wchar_t/char16_t/char32_t，C++20+ 追加 char8_t）
		template <class T>
		struct is_character : std::bool_constant<
			std::is_same_v<T, char>
			|| std::is_same_v<T, wchar_t>
			|| std::is_same_v<T, char16_t>
			|| std::is_same_v<T, char32_t>
#	if defined(__cpp_char8_t) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
			|| std::is_same_v<T, char8_t>
#	endif
		> {};

		template <class T>
		inline constexpr bool is_character_v = is_character<T>::value;

		// 检测 It 是否为 random_access 迭代器（void_t 包装避免硬错误）
		template <class It, class = void>
		struct is_random_access_iterator : std::false_type {};

		template <class It>
		struct is_random_access_iterator<It, std::void_t<
			typename std::iterator_traits<It>::iterator_category>>
			: std::is_base_of<std::random_access_iterator_tag,
				typename std::iterator_traits<It>::iterator_category> {};

		// 检测 It 是否为 input_iterator（正向检测，自动排除 output_iterator_tag）
		template <class It, class = void>
		struct is_input_iterator : std::false_type {};

		template <class It>
		struct is_input_iterator<It, std::void_t<
			typename std::iterator_traits<It>::iterator_category>>
			: std::is_base_of<std::input_iterator_tag,
				typename std::iterator_traits<It>::iterator_category> {};

		template <class It>
		inline constexpr bool is_random_access_iterator_v =
			is_random_access_iterator<It>::value;

		template <class It>
		inline constexpr bool is_input_iterator_v =
			is_input_iterator<It>::value;

		// 检测 *first = T 合法性 + T 为数值类型（RandFill 用）
		template <class It, class T, class = void>
		struct is_rand_fillable : std::false_type {};

		template <class It, class T>
		struct is_rand_fillable<It, T, std::void_t<
			decltype(*std::declval<It&>() = std::declval<T>())
		>> : std::bool_constant<
			std::is_integral_v<T> || std::is_floating_point_v<T>
		> {};

		template <class It, class T>
		inline constexpr bool is_rand_fillable_v = is_rand_fillable<It, T>::value;

		// 检测 state_type 是否为可索引容器（排除标量如 SplitMix64 的 uint64_t）
		template <class S, class = void>
		struct is_indexable_state : std::false_type {};

		template <class S>
		struct is_indexable_state<S, std::void_t<
			decltype(std::declval<const S&>().size()),
			decltype(std::declval<S&>()[std::size_t{}]),
			typename S::value_type
		>> : std::is_same<
			decltype(std::declval<const S&>().size()),
			std::size_t> {};

		template <class S>
		inline constexpr bool is_indexable_state_v = is_indexable_state<S>::value;

		// 可序列化引擎检测（serialize/deserialize/state_type + state_type 为可索引容器）
		template <class E, class = void>
		struct is_serializable_engine : std::false_type {};

		template <class E>
		struct is_serializable_engine<E, std::void_t<
			decltype(std::declval<const E&>().serialize()),
			decltype(std::declval<E&>().deserialize(
				std::declval<typename E::state_type>())),
			typename E::state_type
		>> : std::bool_constant<
			std::is_same_v<
				decltype(std::declval<const E&>().serialize()),
				typename E::state_type>
			&& is_indexable_state_v<typename E::state_type>
		> {};

		template <class E>
		inline constexpr bool is_serializable_engine_v = is_serializable_engine<E>::value;
	}

	template <class Engine, std::enable_if_t<detail::HasJump<Engine>::value>* = nullptr>
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
		DefaultEngine() = Xoshiro256StarStar{ seed };
	}

	// 重置为真随机种子
	inline void ReseedRandom()
	{
		DefaultEngine() = Xoshiro256StarStar{ RandomSeed() };
	}

	// 生成 [min, max] 范围内的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(T min, T max)
	{
		std::uniform_int_distribution<T> dist(min, max);
		return dist(DefaultEngine());
	}

	// 生成 [0, max] 范围内的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(T max)
	{
		return RandInt<T>(T{0}, max);
	}

	// 生成 [min, max) 范围内的随机浮点数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
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

	// 指定引擎的重载：生成随机布尔值，为 true 的概率为 p
	template <class Engine>
	[[nodiscard]]
	inline bool RandBool(Engine& engine, double p = 0.5)
	{
		std::uniform_real_distribution<double> dist(0.0, 1.0);
		return dist(engine) < p;
	}

	// 伯努利分布（RandBool 的别名封装，对齐 <random> 命名）
	[[nodiscard]]
	inline bool RandBernoulli(double p = 0.5)
	{
		return RandBool(p);
	}

	// 伯努利分布引擎重载
	template <class Engine>
	[[nodiscard]]
	inline bool RandBernoulli(Engine& engine, double p = 0.5)
	{
		return RandBool(engine, p);
	}

	// 生成 [min, max] 范围内的随机字符（类型安全，避免 RandInt 窄化）
	// 内部用 int64_t 分布以支持 char32_t 全范围
	template <class CharT,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(CharT min, CharT max)
	{
		using IntT = std::int64_t;
		std::uniform_int_distribution<IntT> dist(
			static_cast<IntT>(min), static_cast<IntT>(max));
		return static_cast<CharT>(dist(DefaultEngine()));
	}

	// 生成 [CharT{}, max] 范围内的随机字符
	template <class CharT,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(CharT max)
	{
		return RandChar<CharT>(CharT{}, max);
	}

	// 指定引擎重载：生成 [min, max] 范围内的随机字符
	template <class CharT, class Engine,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT min, CharT max)
	{
		using IntT = std::int64_t;
		std::uniform_int_distribution<IntT> dist(
			static_cast<IntT>(min), static_cast<IntT>(max));
		return static_cast<CharT>(dist(engine));
	}

	// 指定引擎重载：生成 [CharT{}, max] 范围内的随机字符
	template <class CharT, class Engine,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT max)
	{
		return RandChar<CharT>(engine, CharT{}, max);
	}

	////////////////////////////////////////////////////////////////
	//
	//	RandChar / RandString 预设字符集（v1.2 新增）
	//
	//	提供常用字符集枚举，避免手写 ASCII 范围或字符串。
	//

	// 预设字符集枚举
	enum class CharSet
	{
		Alphanumeric,  // [A-Za-z0-9]    62 个
		Alpha,         // [A-Za-z]        52 个
		Lower,         // [a-z]           26 个
		Upper,         // [A-Z]           26 个
		Digit,         // [0-9]           10 个
		Hex,           // [0-9a-fA-F]     16 个
		Printable,     // [!-~]           94 个可打印 ASCII
		Base64,        // [A-Za-z0-9+/]   64 个（RFC 4648 标准变体）
	};

	namespace detail
	{
		// 返回预设字符集的字符串视图（零拷贝，指向静态存储）
		[[nodiscard]]
		inline std::string_view CharSetString(CharSet cs) noexcept
		{
			switch (cs)
			{
			case CharSet::Alphanumeric:
				return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
			case CharSet::Alpha:
				return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
			case CharSet::Lower:
				return "abcdefghijklmnopqrstuvwxyz";
			case CharSet::Upper:
				return "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
			case CharSet::Digit:
				return "0123456789";
			case CharSet::Hex:
				return "0123456789abcdefABCDEF";
			case CharSet::Printable:
				return "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
			case CharSet::Base64:
				return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			}
			return "";
		}
	}

	// 从预设字符集随机取一个字符（固定返回 char：预设集均为 ASCII 范围）
	[[nodiscard]]
	inline char RandChar(CharSet cs)
	{
		const auto charset = detail::CharSetString(cs);
		if (charset.empty())
			throw std::invalid_argument("RandChar: charset is empty");
		auto& rng = DefaultEngine();
		std::uniform_int_distribution<std::size_t> dist(0, charset.size() - 1);
		return charset[dist(rng)];
	}

	// 引擎重载
	template <class Engine>
	[[nodiscard]]
	inline char RandChar(Engine& engine, CharSet cs)
	{
		const auto charset = detail::CharSetString(cs);
		if (charset.empty())
			throw std::invalid_argument("RandChar: charset is empty");
		std::uniform_int_distribution<std::size_t> dist(0, charset.size() - 1);
		return charset[dist(engine)];
	}

	// 从容器中随机取一个元素
	template <class Container>
	[[nodiscard]]
	inline decltype(auto) RandElement(Container&& c)
	{
		using Size = typename std::remove_reference_t<Container>::size_type;
		if (c.empty())
			throw std::invalid_argument("RandElement: empty container");
		return c[RandInt<Size>(static_cast<Size>(c.size() - 1))];
	}

	// 迭代器版 RandElement：随机访问迭代器，O(1) 直接定位
	template <class It,
		std::enable_if_t<detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline It RandElement(It first, It last)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		const Diff n = std::distance(first, last);
		if (n <= 0)
			throw std::invalid_argument("RandElement: empty range");
		return std::next(first, RandInt<Diff>(Diff{0}, n - 1));
	}

	// 迭代器版 RandElement：输入迭代器（非 random_access），O(n) reservoir sampling
	// is_input_iterator_v 正向检测，自动排除 output_iterator_tag
	template <class It,
		std::enable_if_t<detail::is_input_iterator_v<It>
			&& !detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline It RandElement(It first, It last)
	{
		if (first == last)
			throw std::invalid_argument("RandElement: empty range");
		It selected = first;
		++first;
		for (typename std::iterator_traits<It>::difference_type i = 1;
			first != last; ++first, ++i)
		{
			if (RandInt<typename std::iterator_traits<It>::difference_type>(0, i) == 0)
				selected = first;
		}
		return selected;
	}

	// 指定引擎重载：随机访问迭代器版 RandElement
	template <class It, class Engine,
		std::enable_if_t<detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline It RandElement(Engine& engine, It first, It last)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		const Diff n = std::distance(first, last);
		if (n <= 0)
			throw std::invalid_argument("RandElement: empty range");
		return std::next(first, RandInt<Diff>(engine, Diff{0}, n - 1));
	}

	// 指定引擎重载：输入迭代器版 RandElement（reservoir sampling）
	template <class It, class Engine,
		std::enable_if_t<detail::is_input_iterator_v<It>
			&& !detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline It RandElement(Engine& engine, It first, It last)
	{
		if (first == last)
			throw std::invalid_argument("RandElement: empty range");
		It selected = first;
		++first;
		for (typename std::iterator_traits<It>::difference_type i = 1;
			first != last; ++first, ++i)
		{
			if (RandInt<typename std::iterator_traits<It>::difference_type>(
				engine, typename std::iterator_traits<It>::difference_type{0}, i) == 0)
				selected = first;
		}
		return selected;
	}


	// 生成正态分布随机数（默认均值 0，标准差 1）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
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

	// 用随机值填充 [first, last) 范围（STL 风格）
	// T 从 min/max 推导，须与容器 value_type 一致，否则精度丢失（见 README 警告）
	template <class It, class T,
		std::enable_if_t<detail::is_rand_fillable_v<It, T>>* = nullptr>
	inline void RandFill(It first, It last, T min, T max)
	{
		auto& rng = DefaultEngine();
		if constexpr (std::is_integral_v<T>)
		{
			std::uniform_int_distribution<T> dist(min, max);
			for (; first != last; ++first) *first = dist(rng);
		}
		else
		{
			std::uniform_real_distribution<T> dist(min, max);
			for (; first != last; ++first) *first = dist(rng);
		}
	}

	// 指定引擎重载：RandFill
	template <class It, class T, class Engine,
		std::enable_if_t<detail::is_rand_fillable_v<It, T>>* = nullptr>
	inline void RandFill(Engine& engine, It first, It last, T min, T max)
	{
		if constexpr (std::is_integral_v<T>)
		{
			std::uniform_int_distribution<T> dist(min, max);
			for (; first != last; ++first) *first = dist(engine);
		}
		else
		{
			std::uniform_real_distribution<T> dist(min, max);
			for (; first != last; ++first) *first = dist(engine);
		}
	}

	// 生成包含 n 个随机整数的 vector（便捷版）
	template <class T,
		std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
	{
		std::vector<T> v;
		v.reserve(n);
		auto& rng = DefaultEngine();
		std::uniform_int_distribution<T> dist(min, max);
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(rng));
		return v;
	}

	// 生成包含 n 个随机浮点数的 vector（便捷版）
	template <class T,
		std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
	{
		std::vector<T> v;
		v.reserve(n);
		auto& rng = DefaultEngine();
		std::uniform_real_distribution<T> dist(min, max);
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(rng));
		return v;
	}

	// 指定引擎重载：RandVector 整数版
	template <class T, class Engine,
		std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n)
	{
		std::vector<T> v;
		v.reserve(n);
		std::uniform_int_distribution<T> dist(min, max);
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(engine));
		return v;
	}

	// 指定引擎重载：RandVector 浮点版
	template <class T, class Engine,
		std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n)
	{
		std::vector<T> v;
		v.reserve(n);
		std::uniform_real_distribution<T> dist(min, max);
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(engine));
		return v;
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
	template <class T, class Engine, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(Engine& engine, T min, T max)
	{
		std::uniform_int_distribution<T> dist(min, max);
		return dist(engine);
	}

	template <class T, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandReal(Engine& engine, T min = T{0}, T max = T{1})
	{
		std::uniform_real_distribution<T> dist(min, max);
		return dist(engine);
	}

	////////////////////////////////////////////////////////////////
	//
	//	扩展便捷 API
	//

	// 无放回抽样：从容器中随机抽取 n 个元素（Fisher-Yates 前 n 步）
	template <class Container>
	[[nodiscard]]
	inline auto RandSample(const Container& c, typename Container::size_type n)
	{
		using T = typename Container::value_type;
		using Size = typename Container::size_type;
		std::vector<T> pool(c.begin(), c.end());
		const Size size = static_cast<Size>(pool.size());
		if (n >= size) return pool;
		auto& rng = DefaultEngine();
		for (Size i = 0; i < n; ++i)
		{
			std::uniform_int_distribution<Size> dist(i, size - 1);
			const Size j = dist(rng);
			auto tmp = std::move(pool[i]);
			pool[i] = std::move(pool[j]);
			pool[j] = std::move(tmp);
		}
		pool.resize(n);
		return pool;
	}

	// ============================================================
	// RandSample 迭代器版（v1.2 新增）
	// 路径 1：随机访问迭代器 —— hash-set / 索引数组双分支
	// 路径 2：输入迭代器 —— reservoir sampling (Algorithm R, i+1 修复)
	// ============================================================

	// 路径 1：随机访问迭代器（hash-set / 索引数组双分支）
	template <class It,
		std::enable_if_t<detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline std::vector<typename std::iterator_traits<It>::value_type>
	RandSample(It first, It last, typename std::iterator_traits<It>::difference_type n)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		using T = typename std::iterator_traits<It>::value_type;
		const Diff size = std::distance(first, last);
		if (n <= 0 || size == 0)
			return {};
		if (n >= size)
			return std::vector<T>(first, last);

		auto& rng = DefaultEngine();

		// 分支选择：n² < size 时 hash-set 内存优（O(n)）；否则索引数组常数优（O(N)）
		const auto nSq = static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(n);
		const auto sizeU = static_cast<std::uint64_t>(size);
		if (nSq < sizeU)
		{
			// hash-set 分支：O(n) 内存，O(n) 期望时间
			std::unordered_set<Diff> selected;
			selected.reserve(static_cast<std::size_t>(n));
			std::vector<T> result;
			result.reserve(static_cast<std::size_t>(n));
			while (result.size() < static_cast<std::size_t>(n))
			{
				std::uniform_int_distribution<Diff> dist(Diff{0}, static_cast<Diff>(sizeU - 1));
				const Diff idx = dist(rng);
				if (selected.insert(idx).second)
					result.push_back(first[idx]);
			}
			return result;
		}

		// 索引数组分支：O(N) 内存，O(N) 时间，无碰撞
		std::vector<Diff> indices(static_cast<std::size_t>(size));
		for (Diff i = 0; i < size; ++i)
			indices[static_cast<std::size_t>(i)] = i;

		// Fisher-Yates 前 n 步：j ∈ [i, size-1]
		for (Diff i = 0; i < n; ++i)
		{
			std::uniform_int_distribution<Diff> dist(i, static_cast<Diff>(size - 1));
			const Diff j = dist(rng);
			std::swap(indices[static_cast<std::size_t>(i)],
			          indices[static_cast<std::size_t>(j)]);
		}

		std::vector<T> result;
		result.reserve(static_cast<std::size_t>(n));
		for (Diff i = 0; i < n; ++i)
			result.push_back(first[indices[static_cast<std::size_t>(i)]]);
		return result;
	}

	// 路径 2：输入迭代器（reservoir sampling, Algorithm R, i+1 修复）
	template <class It,
		std::enable_if_t<detail::is_input_iterator_v<It>
			&& !detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline std::vector<typename std::iterator_traits<It>::value_type>
	RandSample(It first, It last, typename std::iterator_traits<It>::difference_type n)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		using T = typename std::iterator_traits<It>::value_type;
		if (n <= 0)
			return {};

		std::vector<T> reservoir;
		reservoir.reserve(static_cast<std::size_t>(n));

		// 填满蓄水池
		Diff i = 0;
		for (; i < n && first != last; ++i, ++first)
			reservoir.push_back(*first);

		if (first == last)
			return reservoir;  // 元素不足 n，返回全部

		// Algorithm R：第 i 个元素（i >= n，0-indexed）以 n/(i+1) 概率替换蓄水池随机位置
		// 关键：j ∈ [0, i]（闭区间），uniform_int_distribution(0, i) 正好是 [0, i] 闭区间
		auto& rng = DefaultEngine();
		for (; first != last; ++i, ++first)
		{
			std::uniform_int_distribution<Diff> dist(Diff{0}, i);
			const Diff j = dist(rng);
			if (j < n)
				reservoir[static_cast<std::size_t>(j)] = *first;
		}
		return reservoir;
	}

	// 引擎重载 —— 随机访问迭代器
	template <class It, class Engine,
		std::enable_if_t<detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline std::vector<typename std::iterator_traits<It>::value_type>
	RandSample(Engine& engine, It first, It last, typename std::iterator_traits<It>::difference_type n)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		using T = typename std::iterator_traits<It>::value_type;
		const Diff size = std::distance(first, last);
		if (n <= 0 || size == 0)
			return {};
		if (n >= size)
			return std::vector<T>(first, last);

		const auto nSq = static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(n);
		const auto sizeU = static_cast<std::uint64_t>(size);
		if (nSq < sizeU)
		{
			std::unordered_set<Diff> selected;
			selected.reserve(static_cast<std::size_t>(n));
			std::vector<T> result;
			result.reserve(static_cast<std::size_t>(n));
			while (result.size() < static_cast<std::size_t>(n))
			{
				std::uniform_int_distribution<Diff> dist(Diff{0}, static_cast<Diff>(sizeU - 1));
				const Diff idx = dist(engine);
				if (selected.insert(idx).second)
					result.push_back(first[idx]);
			}
			return result;
		}

		std::vector<Diff> indices(static_cast<std::size_t>(size));
		for (Diff i = 0; i < size; ++i)
			indices[static_cast<std::size_t>(i)] = i;

		for (Diff i = 0; i < n; ++i)
		{
			std::uniform_int_distribution<Diff> dist(i, static_cast<Diff>(size - 1));
			const Diff j = dist(engine);
			std::swap(indices[static_cast<std::size_t>(i)],
			          indices[static_cast<std::size_t>(j)]);
		}

		std::vector<T> result;
		result.reserve(static_cast<std::size_t>(n));
		for (Diff i = 0; i < n; ++i)
			result.push_back(first[indices[static_cast<std::size_t>(i)]]);
		return result;
	}

	// 引擎重载 —— 输入迭代器（reservoir）
	template <class It, class Engine,
		std::enable_if_t<detail::is_input_iterator_v<It>
			&& !detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline std::vector<typename std::iterator_traits<It>::value_type>
	RandSample(Engine& engine, It first, It last, typename std::iterator_traits<It>::difference_type n)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		using T = typename std::iterator_traits<It>::value_type;
		if (n <= 0)
			return {};

		std::vector<T> reservoir;
		reservoir.reserve(static_cast<std::size_t>(n));

		Diff i = 0;
		for (; i < n && first != last; ++i, ++first)
			reservoir.push_back(*first);

		if (first == last)
			return reservoir;

		// Algorithm R：j ∈ [0, i] 闭区间
		for (; first != last; ++i, ++first)
		{
			std::uniform_int_distribution<Diff> dist(Diff{0}, i);
			const Diff j = dist(engine);
			if (j < n)
				reservoir[static_cast<std::size_t>(j)] = *first;
		}
		return reservoir;
	}

	// 生成 [0, n) 的随机排列
	[[nodiscard]]
	inline std::vector<std::size_t> RandPermutation(std::size_t n)
	{
		std::vector<std::size_t> perm(n);
		for (std::size_t i = 0; i < n; ++i) perm[i] = i;
		if (n < 2) return perm;
		auto& rng = DefaultEngine();
		for (std::size_t i = n - 1; i > 0; --i)
		{
			std::uniform_int_distribution<std::size_t> dist(0, i);
			const std::size_t j = dist(rng);
			auto tmp = perm[i];
			perm[i] = perm[j];
			perm[j] = tmp;
		}
		return perm;
	}

	// 生成指定长度的随机字符串
	[[nodiscard]]
	inline std::string RandString(std::size_t length, std::string_view charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
	{
		if (charset.empty())
			throw std::invalid_argument("RandString: charset is empty");
		std::string result(length, '\0');
		auto& rng = DefaultEngine();
		std::uniform_int_distribution<std::size_t> dist(0, charset.size() - 1);
		for (std::size_t i = 0; i < length; ++i)
			result[i] = charset[dist(rng)];
		return result;
	}

	// 从预设字符集生成随机字符串（委托 string_view 版，零重复代码）
	[[nodiscard]]
	inline std::string RandString(std::size_t n, CharSet cs)
	{
		return RandString(n, detail::CharSetString(cs));
	}

	// 引擎重载：从预设字符集生成随机字符串
	template <class Engine>
	[[nodiscard]]
	inline std::string RandString(Engine& engine, std::size_t n, CharSet cs)
	{
		const auto charset = detail::CharSetString(cs);
		if (charset.empty())
			throw std::invalid_argument("RandString: charset is empty");
		std::string result(n, '\0');
		std::uniform_int_distribution<std::size_t> dist(0, charset.size() - 1);
		for (std::size_t i = 0; i < n; ++i)
			result[i] = charset[dist(engine)];
		return result;
	}

	// 指数分布随机数（参数 lambda，均值 = 1/lambda）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExp(T lambda = T{1})
	{
		std::exponential_distribution<T> dist(lambda);
		return dist(DefaultEngine());
	}

	// 泊松分布随机数（参数 mean）
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandPoisson(double mean = 1.0)
	{
		std::poisson_distribution<T> dist(mean);
		return dist(DefaultEngine());
	}

	// 伽马分布随机数（参数 alpha, beta）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGamma(T alpha = T{1}, T beta = T{1})
	{
		std::gamma_distribution<T> dist(alpha, beta);
		return dist(DefaultEngine());
	}

	// 二项分布随机数（t 次试验，每次成功概率 p）
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBinomial(T t = 1, double p = 0.5)
	{
		std::binomial_distribution<T> dist(t, p);
		return dist(DefaultEngine());
	}

	// 二项分布引擎重载
	template <class T = int, class Engine, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBinomial(Engine& engine, T t = 1, double p = 0.5)
	{
		std::binomial_distribution<T> dist(t, p);
		return dist(engine);
	}

	// 对数正态分布随机数（参数 mean, stddev）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandLogNormal(T mean = T{0}, T stddev = T{1})
	{
		std::lognormal_distribution<T> dist(mean, stddev);
		return dist(DefaultEngine());
	}

	// 对数正态分布引擎重载
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandLogNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
	{
		std::lognormal_distribution<T> dist(mean, stddev);
		return dist(engine);
	}

	// 几何分布随机数（首次成功前的失败次数，参数 p）
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGeometric(double p = 0.5)
	{
		std::geometric_distribution<T> dist(p);
		return dist(DefaultEngine());
	}

	// 几何分布引擎重载
	template <class T = int, class Engine, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGeometric(Engine& engine, double p = 0.5)
	{
		std::geometric_distribution<T> dist(p);
		return dist(engine);
	}

	// 柯西分布随机数（位置参数 a，尺度参数 b）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandCauchy(T a = T{0}, T b = T{1})
	{
		std::cauchy_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	// 柯西分布引擎重载
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandCauchy(Engine& engine, T a = T{0}, T b = T{1})
	{
		std::cauchy_distribution<T> dist(a, b);
		return dist(engine);
	}

	// 韦布尔分布随机数（形状参数 a，尺度参数 b）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandWeibull(T a = T{1}, T b = T{1})
	{
		std::weibull_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	// 韦布尔分布引擎重载
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandWeibull(Engine& engine, T a = T{1}, T b = T{1})
	{
		std::weibull_distribution<T> dist(a, b);
		return dist(engine);
	}

	// 极值分布（Gumbel）随机数（位置参数 a，尺度参数 b）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExtremeValue(T a = T{0}, T b = T{1})
	{
		std::extreme_value_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	// 极值分布引擎重载
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExtremeValue(Engine& engine, T a = T{0}, T b = T{1})
	{
		std::extreme_value_distribution<T> dist(a, b);
		return dist(engine);
	}

	// 卡方分布随机数（自由度 n）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandChiSquared(T n = T{1})
	{
		std::chi_squared_distribution<T> dist(n);
		return dist(DefaultEngine());
	}

	// 卡方分布引擎重载
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandChiSquared(Engine& engine, T n = T{1})
	{
		std::chi_squared_distribution<T> dist(n);
		return dist(engine);
	}

	// 学生 t 分布随机数（自由度 n）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandStudentT(T n = T{1})
	{
		std::student_t_distribution<T> dist(n);
		return dist(DefaultEngine());
	}

	// 学生 t 分布引擎重载
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandStudentT(Engine& engine, T n = T{1})
	{
		std::student_t_distribution<T> dist(n);
		return dist(engine);
	}

	// Fisher F 分布随机数（自由度 m, n）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandFisherF(T m = T{1}, T n = T{1})
	{
		std::fisher_f_distribution<T> dist(m, n);
		return dist(DefaultEngine());
	}

	// Fisher F 分布引擎重载
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandFisherF(Engine& engine, T m = T{1}, T n = T{1})
	{
		std::fisher_f_distribution<T> dist(m, n);
		return dist(engine);
	}

	// Beta 分布随机数（形状参数 a, b；无 STL，自实现 Gamma(a)/(Gamma(a)+Gamma(b))）
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBeta(T a = T{1}, T b = T{1})
	{
		std::gamma_distribution<T> distA(a, T{1});
		std::gamma_distribution<T> distB(b, T{1});
		auto& rng = DefaultEngine();
		const T x = distA(rng);
		const T y = distB(rng);
		const T sum = x + y;
		// 捕获精确 0 与非正规数，避免除零/除以极小值产生 inf
		if (sum < std::numeric_limits<T>::min())
			return T{0};
		return x / sum;
	}

	// Beta 分布引擎重载
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBeta(Engine& engine, T a = T{1}, T b = T{1})
	{
		std::gamma_distribution<T> distA(a, T{1});
		std::gamma_distribution<T> distB(b, T{1});
		const T x = distA(engine);
		const T y = distB(engine);
		const T sum = x + y;
		if (sum < std::numeric_limits<T>::min())
			return T{0};
		return x / sum;
	}

	// 生成 N 位随机整数（结果范围 [0, 2^N)）
	template <int N, class T = std::uint64_t, std::enable_if_t<std::is_integral_v<T> && (N > 0) && (N <= 64)>* = nullptr>
	[[nodiscard]]
	inline T RandBits() noexcept
	{
		auto& rng = DefaultEngine();
		if constexpr (N == 64)
			return static_cast<T>(rng());
		else
			return static_cast<T>(rng() & ((std::uint64_t{1} << N) - 1));
	}

	// 生成随机 UUID v4 字符串（xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx）
	[[nodiscard]]
	inline std::string RandUUID()
	{
		auto& rng = DefaultEngine();
		static constexpr char hex[] = "0123456789abcdef";
		std::string uuid(36, '-');
		for (int i = 0; i < 36; ++i)
		{
			if (i == 8 || i == 13 || i == 18 || i == 23) continue;
			if (i == 14) { uuid[i] = '4'; continue; }
			if (i == 19) { uuid[i] = hex[8 + (rng() & 3)]; continue; }
			uuid[i] = hex[rng() & 15];
		}
		return uuid;
	}

	////////////////////////////////////////////////////////////////
	//
	//	静态断言：确认引擎满足 UniformRandomBitGenerator 要求
	//

	static_assert(std::is_same_v<SplitMix64::result_type, std::uint64_t>);
	static_assert(std::is_same_v<Xoshiro256StarStar::result_type, std::uint64_t>);
	static_assert(std::is_same_v<Xoshiro128StarStar::result_type, std::uint32_t>);
	static_assert(std::is_same_v<Xoroshiro64StarStar::result_type, std::uint32_t>);
	static_assert(std::is_same_v<SFC64::result_type, std::uint64_t>);
	static_assert(std::is_same_v<RomuDuoJr::result_type, std::uint64_t>);
	static_assert(SplitMix64::min() < SplitMix64::max());
	static_assert(Xoshiro256StarStar::min() < Xoshiro256StarStar::max());
	static_assert(Xoshiro128StarStar::min() < Xoshiro128StarStar::max());
	static_assert(Xoroshiro64StarStar::min() < Xoroshiro64StarStar::max());
	static_assert(SFC64::min() < SFC64::max());
	static_assert(RomuDuoJr::min() < RomuDuoJr::max());

	// ========================================================================
	// 流式运算符 operator<< / operator>>
	// 仅对 state_type 为可索引容器类的引擎生效（is_serializable_engine_v）
	// SplitMix64（state_type = uint64_t 标量）不支持，由 is_indexable_state_v 排除
	// 格式兼容 std::random_engine：空格分隔的十进制数序列
	// ========================================================================

	// 流式输出引擎状态
	template <class CharT, class Traits, class Engine,
		std::enable_if_t<detail::is_serializable_engine_v<Engine>>* = nullptr>
	std::basic_ostream<CharT, Traits>&
	operator<<(std::basic_ostream<CharT, Traits>& os, const Engine& engine)
	{
		auto state = engine.serialize();
		auto it = state.begin();
		if (it != state.end())
		{
			os << *it;
			for (++it; it != state.end(); ++it)
				os << os.widen(' ') << *it;
		}
		return os;
	}

	// 流式恢复引擎状态
	// 若解析失败（读取不足或流错误），setstate(failbit) 且引擎状态保持不变
	// （与 std::random_engine 一致：先读取到临时 state，全部成功才 deserialize）
	template <class CharT, class Traits, class Engine,
		std::enable_if_t<detail::is_serializable_engine_v<Engine>>* = nullptr>
	std::basic_istream<CharT, Traits>&
	operator>>(std::basic_istream<CharT, Traits>& is, Engine& engine)
	{
		typename Engine::state_type state{};
		std::size_t i = 0;
		for (; i < state.size() && is; ++i)
			is >> state[i];

		if (i == state.size() && is)
		{
			engine.deserialize(state);
		}
		else
		{
			is.setstate(std::ios_base::failbit);
		}
		return is;
	}

}
