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
//		Xoroshiro128StarStar   64-bit  2^128-1    16B    内存受限，统计更优
//		Xoshiro128StarStar     32-bit  2^128-1    16B    32 位平台，统计更优
//		Xoroshiro64StarStar    32-bit  2^64-1      8B    极端内存受限
//		SplitMix64             64-bit  2^64        8B    种子扩展 / 哈希，非通用 PRNG
//		SFC64                  64-bit  >= 2^64    32B    速度极快，通过 PractRand
//		RomuDuoJr              64-bit  >= 2^51    16B    极简极快，非关键模拟
//		ChaCha20               64-bit  无周期      48B+   密码学安全 CSPRNG（RFC 8439）
//
//	⚠️ 安全声明
//	本库的 xoshiro/xoroshiro/SFC64/RomuDuoJr 引擎均非 CSPRNG。
//	状态可从输出逆推，不可用于密码/密钥/会话 token 等安全场景。
//	此类场景请使用 ChaCha20 引擎或 SecureRandomBytes()。
//
//----------------------------------------------------------------------------------------

# pragma once
# include <cstdint>
# include <array>
# include <cmath>
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
# include <chrono>    // std::chrono（RandomSeed 时间戳兜底用）
# include <atomic>    // std::atomic（RandomSeed 兜底计数）
# include <functional>// std::hash（RandomSeed 线程 Hash）
# include <thread>    // std::this_thread（RandomSeed 线程 ID）
# include <ios>       // std::ios_base::failbit（operator>> 所需）
# include <istream>   // std::basic_istream（operator>> 所需完整类型）
# include <ostream>   // std::basic_ostream（operator<< 所需完整类型）
# if defined(_MSC_VER) && (defined(__x86_64__) || defined(_M_X64))
#	include <immintrin.h>
#	include <intrin.h>
# endif
// ── A3 跨平台 OS 熵源头文件（条件包含） ──
# if defined(_WIN32) && __has_include(<bcrypt.h>)
// bcrypt.h 依赖 <windows.h> 提供的 ULONG/NTSTATUS 等类型（MSVC 和 MinGW 均需）
// NOMINMAX 阻止 <windows.h> 定义 min/max 宏（与引擎的 min()/max() 方法冲突）
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#	include <bcrypt.h>
#	pragma comment(lib, "bcrypt.lib")  // 仅 MSVC 生效
// MinGW 不支持 #pragma comment(lib)，须手动添加 -lbcrypt 链接选项
#	if(defined(__MINGW32__) || defined(__MINGW64__)) && !defined(RANDX_SUPPRESS_LINK_HINT)
#		pragma message("RandX: MinGW 需手动链接 bcrypt（编译命令添加 -lbcrypt）")
#	endif
# elif defined(__linux__) && __has_include(<sys/random.h>)
#	include <sys/random.h>
#	include <cerrno>
# elif defined(__APPLE__)
#	include <TargetConditionals.h>
#	if TARGET_OS_IPHONE
#		if __has_include(<Security/SecRandom.h>)
#			include <Security/SecRandom.h>
#		endif
#	elif __has_include(<Security/Security.h>)
#		include <Security/Security.h>
#	endif
# endif
# include <chrono>   // std::chrono（RandomSeed 时间戳兜底用）
# include <cstring>  // std::memcpy（std::random_device 回退路径用）
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

	// ── 引擎基础设施（提前定义，供 EngineBase CRTP 基类使用） ──
	namespace detail
	{
		[[nodiscard]]
		inline constexpr std::uint64_t RotL(const std::uint64_t x, const int s) noexcept
		{
			const int count = s & 63;
			return count == 0 ? x : ((x << count) | (x >> (64 - count)));
		}

		[[nodiscard]]
		inline constexpr std::uint32_t RotL(const std::uint32_t x, const int s) noexcept
		{
			const int count = s & 31;
			return count == 0 ? x : ((x << count) | (x >> (32 - count)));
		}

		template <std::size_t N>
		[[nodiscard]]
		inline constexpr bool IsAllZero(const std::array<std::uint64_t, N>& state) noexcept
		{
			for (const auto& s : state) { if (s != 0) return false; }
			return true;
		}

		template <std::size_t N>
		[[nodiscard]]
		inline constexpr bool IsAllZero(const std::array<std::uint32_t, N>& state) noexcept
		{
			for (const auto& s : state) { if (s != 0) return false; }
			return true;
		}

		template <typename State>
		[[nodiscard]]
		inline constexpr bool IsValidState(const State& state) noexcept
		{
			return !IsAllZero(state);
		}
	}

	/// @defgroup engines 引擎
	/// @brief 8 个伪随机数生成引擎（7 非 CSPRNG + 1 ChaCha20 CSPRNG）

	/// @brief SplitMix64 伪随机数生成器，64 位输出，周期 2^64。
	///
	/// @details 状态 8 字节（1×uint64）。主要用于种子扩展与哈希，
	/// 可将单个 64 位种子展开为高质量的状态序列。
	/// 原始实现：http://prng.di.unimi.it/splitmix64.c
	///
	/// @note 非通用 PRNG，无 jump 方法。
	/// @sa Xoshiro256StarStar, Xoroshiro128StarStar
	class SplitMix64
	{
	public:

		using state_type	= std::uint64_t;	///< 状态类型（1×uint64）
		using result_type	= std::uint64_t;	///< 输出类型

		/// @brief 以指定状态构造引擎
		/// @param state 64 位初始状态值
		RANDX_NODISCARD_CXX20
		explicit constexpr SplitMix64(state_type state = DefaultSeed) noexcept;

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, SplitMix64>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr SplitMix64(SeedSeq& seq);

		/// @brief 生成下一个 64 位随机数
		/// @return [min(), max()] 区间内的伪随机数
		constexpr result_type operator()() noexcept;

		/// @brief 跳过 n 个输出
		/// @param n 跳过次数
		constexpr void discard(unsigned long long n) noexcept;

		/// @brief 生成 N 个高质量的 64 位种子序列
		/// @tparam N 生成的种子数量
		/// @return 包含 N 个 uint64 的数组，可用于播种其他引擎
		template <std::size_t N>
		[[nodiscard]]
		constexpr std::array<std::uint64_t, N> generateSeedSequence() noexcept;

		/// @brief 输出范围下界
		/// @return 0
		[[nodiscard]]
		static constexpr result_type min() noexcept;

		/// @brief 输出范围上界
		/// @return 2^64 - 1
		[[nodiscard]]
		static constexpr result_type max() noexcept;

		/// @brief 序列化引擎状态
		/// @return 当前状态值
		/// @sa deserialize()
		[[nodiscard]]
		constexpr state_type serialize() const noexcept;

		/// @brief 从状态恢复引擎
		/// @param state serialize() 返回的状态
		/// @sa serialize()
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

	// ── EngineBase CRTP 基类 ──
	// 为数组状态引擎提供公共接口：min/max/discard/serialize/比较/构造/jumpPoly
	// SplitMix64（标量状态）和 ChaCha20（CSPRNG）不继承此基类
	namespace detail
	{
		template <class Derived, class ResultType, std::size_t N>
		struct EngineBase
		{
			using result_type = ResultType;
			using state_type  = std::array<ResultType, N>;

			// --- 公共接口 ---

			[[nodiscard]]
			static constexpr result_type min() noexcept
			{
				return std::numeric_limits<result_type>::lowest();
			}

			[[nodiscard]]
			static constexpr result_type max() noexcept
			{
				return std::numeric_limits<result_type>::max();
			}

			constexpr void discard(unsigned long long z) noexcept
			{
				for (unsigned long long i = 0; i < z; ++i)
					static_cast<Derived*>(this)->operator()();
			}

			[[nodiscard]]
			constexpr state_type serialize() const noexcept
			{
				return s_;
			}

			constexpr void deserialize(const state_type& s) noexcept
			{
				s_ = s;
				if (IsAllZero(s_))
				{
					s_[0] = static_cast<ResultType>(1);
				}
				assert(!IsAllZero(s_) && "absorbing all-zero state");
			}

			// C++17: 手写比较运算符
			friend bool operator==(const EngineBase& lhs, const EngineBase& rhs) noexcept
			{
				return lhs.s_ == rhs.s_;
			}

			friend bool operator!=(const EngineBase& lhs, const EngineBase& rhs) noexcept
			{
				return lhs.s_ != rhs.s_;
			}

		protected:

			static constexpr int Bits = static_cast<int>(sizeof(ResultType) * 8);

			EngineBase() = default;

			// State 构造（用户直接传入，包含 Release/Debug 全零状态静默修正）
			explicit constexpr EngineBase(const state_type& state) noexcept
				: s_(state)
			{
				if (IsAllZero(s_))
				{
					s_[0] = static_cast<ResultType>(1);
				}
				assert(!IsAllZero(s_) && "absorbing all-zero state");
			}

			// SeedSeq 构造（SFINAE 排除 state_type 和 Derived）
			template <class SeedSeq,
				std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, state_type>
					&& !std::is_same_v<std::decay_t<SeedSeq>, Derived>>* = nullptr>
			explicit constexpr EngineBase(SeedSeq& seq)
			{
				if constexpr (sizeof(result_type) == 8)
				{
					std::array<std::uint32_t, N * 2> raw;
					seq.generate(raw.begin(), raw.end());
					for (std::size_t i = 0; i < N; ++i)
						s_[i] = (static_cast<result_type>(raw[2 * i]) << 32) | raw[2 * i + 1];
				}
				else
				{
					std::array<std::uint32_t, N> raw;
					seq.generate(raw.begin(), raw.end());
					for (std::size_t i = 0; i < N; ++i)
						s_[i] = static_cast<result_type>(raw[i]);
				}
				if (IsAllZero(s_)) s_[0] = 1;
			}

			// 单值播种（SplitMix64 扩展，等价于 generateSeedSequence<N>）
			explicit constexpr EngineBase(std::uint64_t seed) noexcept
			{
				SplitMix64 sm{ seed };
				for (std::size_t i = 0; i < N; ++i)
					s_[i] = static_cast<result_type>(sm());
				if (IsAllZero(s_)) s_[0] = 1;
			}

			// jump 多项式通用实现（constexpr，供 MakeStreamEngine 编译期调用）
			template <std::size_t K>
			constexpr void jumpPoly(const ResultType (&poly)[K]) noexcept
			{
				std::array<ResultType, N> acc{};
				for (std::size_t i = 0; i < K; ++i)
					for (int b = 0; b < Bits; ++b)
					{
						if (poly[i] & (ResultType{ 1 } << b))
							for (std::size_t j = 0; j < N; ++j)
								acc[j] ^= s_[j];
						static_cast<Derived*>(this)->operator()();
					}
				s_ = acc;
			}

			state_type s_{};
		};
	}

	/// @brief Xoshiro256** 伪随机数生成器，64 位输出，周期 2^256-1。
	///
	/// @details 通用首选引擎，统计质量最优。状态 32 字节（4×uint64）。
	/// 满足 `std::uniform_random_bit_generator` 概念。
	/// 原始实现：http://prng.di.unimi.it/xoshiro256starstar.c（版本 1.0）
	///
	/// @note 非 CSPRNG，不可用于密码学场景。安全场景请使用 ChaCha20。
	/// @sa Xoroshiro128StarStar, SFC64, ChaCha20
	class Xoshiro256StarStar
		: public detail::EngineBase<Xoshiro256StarStar, std::uint64_t, 4>
	{
		using Base = detail::EngineBase<Xoshiro256StarStar, std::uint64_t, 4>;
	public:

		using typename Base::result_type;	///< 输出类型
		using typename Base::state_type;	///< 状态类型（4×uint64）

		/// @brief 默认构造（使用 DefaultSeed）
		constexpr Xoshiro256StarStar() noexcept : Base(DefaultSeed) {}

		/// @brief 以指定种子构造引擎
		/// @param seed 64 位种子值
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256StarStar(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro256StarStar>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256StarStar(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro256StarStar(state_type state) noexcept
			: Base(state) {}

		/// @brief 生成下一个 64 位随机数
		/// @return [min(), max()] 区间内的伪随机数
		constexpr result_type operator()() noexcept;

		/// @brief 前进 2^128 步，用于创建并行子序列
		/// @note 等价于调用 2^128 次 operator()
		/// @sa longJump(), MakeStreamEngine()
		constexpr void jump() noexcept;

		/// @brief 前进 2^192 步，用于创建更稀疏的并行子序列
		/// @note 等价于调用 2^192 次 operator()
		/// @sa jump(), MakeStreamEngine()
		constexpr void longJump() noexcept;
	};

	/// @brief Xoroshiro128** 伪随机数生成器，64 位输出，周期 2^128-1。
	///
	/// @details 状态 16 字节（2×uint64），内存占用更小，统计质量更优，
	/// 适合内存受限场景。满足 `std::uniform_random_bit_generator` 概念。
	/// 原始实现：http://prng.di.unimi.it/xoroshiro128starstar.c（版本 1.0）
	///
	/// @note 非 CSPRNG，不可用于密码学场景。安全场景请使用 ChaCha20。
	/// @sa Xoshiro256StarStar, Xoroshiro64StarStar, ChaCha20
	class Xoroshiro128StarStar
		: public detail::EngineBase<Xoroshiro128StarStar, std::uint64_t, 2>
	{
		using Base = detail::EngineBase<Xoroshiro128StarStar, std::uint64_t, 2>;
	public:

		using typename Base::result_type;	///< 输出类型
		using typename Base::state_type;	///< 状态类型（2×uint64）

		/// @brief 默认构造（使用 DefaultSeed）
		constexpr Xoroshiro128StarStar() noexcept : Base(DefaultSeed) {}

		/// @brief 以指定种子构造引擎
		/// @param seed 64 位种子值
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128StarStar(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro128StarStar>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128StarStar(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro128StarStar(state_type state) noexcept
			: Base(state) {}

		/// @brief 生成下一个 64 位随机数
		/// @return [min(), max()] 区间内的伪随机数
		constexpr result_type operator()() noexcept;

		/// @brief 前进 2^64 步，用于创建并行子序列
		/// @note 等价于调用 2^64 次 operator()
		/// @sa longJump(), MakeStreamEngine()
		constexpr void jump() noexcept;

		/// @brief 前进 2^96 步，用于创建更稀疏的并行子序列
		/// @note 等价于调用 2^96 次 operator()
		/// @sa jump(), MakeStreamEngine()
		constexpr void longJump() noexcept;
	};

	/// @brief Xoshiro128** 伪随机数生成器，32 位输出，周期 2^128-1。
	///
	/// @details 状态 16 字节（4×uint32），32 位平台优先选择。
	/// 满足 `std::uniform_random_bit_generator` 概念。
	/// 原始实现：http://prng.di.unimi.it/xoshiro128starstar.c（版本 1.1）
	///
	/// @note 非 CSPRNG，不可用于密码学场景。安全场景请使用 ChaCha20。
	/// @sa Xoshiro256StarStar, Xoroshiro64StarStar, ChaCha20
	class Xoshiro128StarStar
		: public detail::EngineBase<Xoshiro128StarStar, std::uint32_t, 4>
	{
		using Base = detail::EngineBase<Xoshiro128StarStar, std::uint32_t, 4>;
	public:

		using typename Base::result_type;	///< 输出类型
		using typename Base::state_type;	///< 状态类型（4×uint32）

		/// @brief 默认构造（使用 DefaultSeed）
		constexpr Xoshiro128StarStar() noexcept : Base(DefaultSeed) {}

		/// @brief 以指定种子构造引擎
		/// @param seed 64 位种子值
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128StarStar(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoshiro128StarStar>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128StarStar(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoshiro128StarStar(state_type state) noexcept
			: Base(state) {}

		/// @brief 生成下一个 32 位随机数
		/// @return [min(), max()] 区间内的伪随机数
		constexpr result_type operator()() noexcept;

		/// @brief 前进 2^64 步，用于创建并行子序列
		/// @note 等价于调用 2^64 次 operator()
		/// @sa longJump(), MakeStreamEngine()
		constexpr void jump() noexcept;

		/// @brief 前进 2^96 步，用于创建更稀疏的并行子序列
		/// @note 等价于调用 2^96 次 operator()
		/// @sa jump(), MakeStreamEngine()
		constexpr void longJump() noexcept;
	};

	/// @brief Xoroshiro64** 伪随机数生成器，32 位输出，周期 2^64-1。
	///
	/// @details 状态 8 字节（2×uint32），适合极端内存受限场景。
	/// 满足 `std::uniform_random_bit_generator` 概念。
	/// 原始实现：http://prng.di.unimi.it/xoroshiro64starstar.c
	///
	/// @note 无 jump 方法。非 CSPRNG，不可用于密码学场景。安全场景请使用 ChaCha20。
	/// @sa Xoshiro128StarStar, Xoroshiro128StarStar, ChaCha20
	class Xoroshiro64StarStar
		: public detail::EngineBase<Xoroshiro64StarStar, std::uint32_t, 2>
	{
		using Base = detail::EngineBase<Xoroshiro64StarStar, std::uint32_t, 2>;
	public:

		using typename Base::result_type;	///< 输出类型
		using typename Base::state_type;	///< 状态类型（2×uint32）

		/// @brief 默认构造（使用 DefaultSeed）
		constexpr Xoroshiro64StarStar() noexcept : Base(DefaultSeed) {}

		/// @brief 以指定种子构造引擎
		/// @param seed 64 位种子值
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro64StarStar(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, Xoroshiro64StarStar>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro64StarStar(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		RANDX_NODISCARD_CXX20
		explicit constexpr Xoroshiro64StarStar(state_type state) noexcept
			: Base(state) {}

		/// @brief 生成下一个 32 位随机数
		/// @return [min(), max()] 区间内的伪随机数
		constexpr result_type operator()() noexcept;
	};
	/// @brief SFC64（Small Fast Counter）伪随机数生成器，64 位输出，周期 >= 2^64。
	///
	/// @details 状态 32 字节（4×uint64）。由 Chris Doty-Humphrey（PractRand）设计，
	/// 速度极快，通过 PractRand 全部统计测试。满足 `std::uniform_random_bit_generator` 概念。
	///
	/// @note 周期由 counter 保证 >= 2^64。无 jump 方法。
	/// 非 CSPRNG，不可用于密码学场景。安全场景请使用 ChaCha20。
	/// @sa Xoshiro256StarStar, RomuDuoJr, ChaCha20
	class SFC64
		: public detail::EngineBase<SFC64, std::uint64_t, 4>
	{
		using Base = detail::EngineBase<SFC64, std::uint64_t, 4>;
	public:

		using typename Base::result_type;	///< 输出类型
		using typename Base::state_type;	///< 状态类型（4×uint64）

		/// @brief 默认构造（使用 DefaultSeed）
		constexpr SFC64() noexcept : SFC64(DefaultSeed) {}

		/// @brief 以指定种子构造引擎（SplitMix64 填充 3 状态字 + counter=1 + 12 轮预热）
		/// @param seed 64 位种子值
		RANDX_NODISCARD_CXX20
		explicit constexpr SFC64(std::uint64_t seed) noexcept;

		/// @brief 从 std::seed_seq 播种（填充 3 状态字 + counter=1 + 12 轮预热）
		/// @param seq 种子序列对象
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, SFC64>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr SFC64(SeedSeq& seq);

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		RANDX_NODISCARD_CXX20
		explicit constexpr SFC64(state_type state) noexcept
			: Base(state) {}

		/// @brief 生成下一个 64 位随机数
		/// @return [min(), max()] 区间内的伪随机数
		constexpr result_type operator()() noexcept;
	};

	/// @brief RomuDuoJr 伪随机数生成器，64 位输出，周期估计 >= 2^51。
	///
	/// @details 状态 16 字节（2×uint64）。由 Mark Overton 设计，
	/// 极简极快，适合非关键模拟场景。满足 `std::uniform_random_bit_generator` 概念。
	///
	/// @note 周期无严格证明（估计 >= 2^51）。无 jump 方法。
	/// 非 CSPRNG，不可用于密码学场景。安全场景请使用 ChaCha20。
	/// @sa SFC64, Xoshiro256StarStar, ChaCha20
	class RomuDuoJr
		: public detail::EngineBase<RomuDuoJr, std::uint64_t, 2>
	{
		using Base = detail::EngineBase<RomuDuoJr, std::uint64_t, 2>;
	public:

		using typename Base::result_type;	///< 输出类型
		using typename Base::state_type;	///< 状态类型（2×uint64）

		/// @brief 默认构造（使用 DefaultSeed）
		constexpr RomuDuoJr() noexcept : Base(DefaultSeed) {}

		/// @brief 以指定种子构造引擎
		/// @param seed 64 位种子值
		RANDX_NODISCARD_CXX20
		explicit constexpr RomuDuoJr(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <class SeedSeq,
			std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, RomuDuoJr>>* = nullptr>
		RANDX_NODISCARD_CXX20
		explicit constexpr RomuDuoJr(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		RANDX_NODISCARD_CXX20
		explicit constexpr RomuDuoJr(state_type state) noexcept
			: Base(state) {}

		/// @brief 生成下一个 64 位随机数
		/// @return [min(), max()] 区间内的伪随机数
		constexpr result_type operator()() noexcept;
	};

	// ── 全 PRNG 引擎 TLS 可平凡析构（Trivially Destructible）编译期静态断言 ──
	static_assert(std::is_trivially_destructible_v<Xoshiro256StarStar>, "Xoshiro256StarStar must be trivially destructible for safe TLS.");
	static_assert(std::is_trivially_destructible_v<Xoroshiro128StarStar>, "Xoroshiro128StarStar must be trivially destructible for safe TLS.");
	static_assert(std::is_trivially_destructible_v<Xoshiro128StarStar>, "Xoshiro128StarStar must be trivially destructible for safe TLS.");
	static_assert(std::is_trivially_destructible_v<Xoroshiro64StarStar>, "Xoroshiro64StarStar must be trivially destructible for safe TLS.");
	static_assert(std::is_trivially_destructible_v<SplitMix64>, "SplitMix64 must be trivially destructible for safe TLS.");
	static_assert(std::is_trivially_destructible_v<SFC64>, "SFC64 must be trivially destructible for safe TLS.");
	static_assert(std::is_trivially_destructible_v<RomuDuoJr>, "RomuDuoJr must be trivially destructible for safe TLS.");

	/// @brief ChaCha20 密码学安全伪随机数生成器（CSPRNG），64 位输出，符合 RFC 8439。
	///
	/// @details 状态为 key(256-bit) + counter(32-bit) + nonce(96-bit)。
	/// 每次 operator() 返回 8 字节，一个 block 服务 8 次调用。
	/// 默认从 OS 熵自动播种，并在输出 2^20 字节后自动 reseed 以提供前向安全。
	/// 满足 `std::uniform_random_bit_generator` 概念。
	///
	/// @note CSPRNG 安全约束：不提供 serialize/deserialize、operator<</>>、
	/// jump/longJump（状态导出违背 CSPRNG 安全模型）。
	/// 非线程安全，每线程应持有独立实例。
	/// @sa SecureRandomBytes, SecureSeed, IsOsCryptoEntropyAvailable, Xoshiro256StarStar
	class ChaCha20
	{
	public:

		using result_type = std::uint64_t;	///< 输出类型

		ChaCha20(const ChaCha20&) = delete;
		ChaCha20& operator=(const ChaCha20&) = delete;
		ChaCha20(ChaCha20&& other) noexcept;
		ChaCha20& operator=(ChaCha20&& other) noexcept;
		~ChaCha20() noexcept;

		/// @brief 构造方式 1：从 OS 熵自动播种（密码学安全，默认）
		/// @note 推荐用于生产环境的密码学安全场景
		ChaCha20();

		/// @brief 构造方式 2：以显式 64 位种子构造
		/// @param seed 64 位种子值
		/// @note 仅用于测试/复现，非密码学安全（种子空间仅 64-bit）
		RANDX_NODISCARD_CXX20
		explicit ChaCha20(std::uint64_t seed);

		/// @brief 构造方式 3：直接指定 key + nonce + counter（高级用法/测试复现）
		/// @param key 密钥指针，须为 32 字节
		/// @param keyLen 密钥长度（字节），须为 32，否则抛出 std::invalid_argument
		/// @param nonce 随机数指针，须为 12 字节
		/// @param nonceLen 随机数长度（字节），须为 12，否则抛出 std::invalid_argument
		/// @param counter 32-bit block 计数器初值（默认 0；KAT 测试时显式传 1）
		/// @note 此构造路径不调用 SecureRandomBytes，调用方须自行保证 key/nonce 的熵源
		ChaCha20(const std::uint8_t* key, std::size_t keyLen,
		         const std::uint8_t* nonce, std::size_t nonceLen,
		         std::uint32_t counter = 0);

		/// @brief 生成下一个 64 位随机数
		/// @return [min(), max()] 区间内的密码学安全伪随机数
		result_type operator()();

		/// @brief 跳过 n 个输出
		/// @param n 跳过次数
		void discard(unsigned long long n);

		/// @brief 从 OS 熵重新播种
		/// @note 手动触发，重置 counter 与字节缓存
		void reseed();

		/// @brief 输出范围下界
		/// @return 0
		RANDX_NODISCARD_CXX20
		static constexpr result_type min() noexcept { return 0; }

		/// @brief 输出范围上界
		/// @return 2^64 - 1
		RANDX_NODISCARD_CXX20
		static constexpr result_type max() noexcept { return UINT64_MAX; }

		// 不提供：serialize/deserialize, operator<</>>, jump/longJump（CSPRNG 安全约束）

	private:

		std::array<std::uint32_t, 12> m_state;   // key(8) + counter(1) + nonce(3)，常数省略（generateBlock 时补齐）
		std::array<std::uint8_t, 64>  m_buffer;  // 当前 block 的字节缓存
		std::size_t                   m_bufferPos;       // 缓存消费位置 [0, 64)，==64 时触发新 block
		std::uint64_t                 m_bytesSinceReseed; // 自上次 reseed 以来输出的字节数
		bool                          m_autoReseed{ false }; // 是否在满 1MB 后自动从 OS 熵重新播种（仅默认无参构造函数启用）

		void generateBlock();        // 跑一次 ChaCha20 block 函数填充 m_buffer
		void reseedIfNecessary();    // m_bytesSinceReseed >= 阈值时自动 reseed
	};

	// ── sizeof 守卫：防止引擎 ABI 意外变化 ──
	static_assert(sizeof(Xoshiro256StarStar) == 32, "Xoshiro256StarStar size changed");
	static_assert(sizeof(Xoroshiro128StarStar) == 16, "Xoroshiro128StarStar size changed");
	static_assert(sizeof(Xoshiro128StarStar) == 16, "Xoshiro128StarStar size changed");
	static_assert(sizeof(Xoroshiro64StarStar) == 8, "Xoroshiro64StarStar size changed");
	static_assert(sizeof(SFC64) == 32, "SFC64 size changed");
	static_assert(sizeof(RomuDuoJr) == 16, "RomuDuoJr size changed");
	static_assert(sizeof(SplitMix64) == 8, "SplitMix64 size changed");
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
		// 安全擦除内存（volatile 防止编译器死存储消除）
		static void SecureWipe(void* ptr, std::size_t len) noexcept
		{
			volatile auto* p = static_cast<volatile std::uint8_t*>(ptr);
			while (len--) *p++ = 0;
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
			int cpuInfo[4] = {0};
			__cpuid(cpuInfo, 1);
			if ((cpuInfo[2] & (1 << 30)) != 0)
			{
				unsigned long long result = 0;
				if (_rdrand64_step(&result))
				{
					out = result;
					return true;
				}
			}
	#endif
#endif
			(void)out;
			return false;
		}

		// ── A3 跨平台 OS 密码学熵源 ──
		// 用 OS 密码学 API 填充 [buf, buf+n) 字节；成功返回 true。
		// 平台优先级：Windows BCryptGenRandom → Linux getrandom → macOS SecRandomCopyBytes → std::random_device 兜底
		// 注：getrandom 可能短读，内部循环直至填满；BCryptGenRandom/SecRandomCopyBytes 一次填满
		[[nodiscard]]
		inline bool GetOsEntropyBytes(void* buf, std::size_t n) noexcept
		{
			if (n == 0) return true;
			auto* p = static_cast<std::uint8_t*>(buf);

#	if defined(_WIN32) && __has_include(<bcrypt.h>)
			// Windows: BCryptGenRandom（分块处理 >4GB 时的 ULONG 截断）
			// NTSTATUS >= 0 即 NT_SUCCESS（使用 BCRYPT_SUCCESS 宏或强转 NTSTATUS 判定）
			std::size_t filled = 0;
			while (filled < n)
			{
				const ULONG chunkSize = static_cast<ULONG>((std::min)(n - filled, static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())));
				const auto status = ::BCryptGenRandom(nullptr, p + filled, chunkSize, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#	if defined(BCRYPT_SUCCESS)
				if (!BCRYPT_SUCCESS(status)) return false;
#	else
				if (static_cast<NTSTATUS>(status) < 0) return false;
#	endif
				filled += chunkSize;
			}
			return true;

#	elif defined(__linux__) && __has_include(<sys/random.h>)
			// Linux: getrandom（循环处理短读与 EINTR）
			std::size_t filled = 0;
			while (filled < n)
			{
				const ssize_t ret = ::getrandom(p + filled, n - filled, 0);
				if (ret < 0)
				{
					if (errno == EINTR) continue;  // 被信号打断，重试
					return false;                   // ENOSYS/EFAULT 等不可恢复错误
				}
				if (ret == 0) return false;
				filled += static_cast<std::size_t>(ret);
			}
			return true;

#	elif defined(__APPLE__) && __has_include(<Security/Security.h>)
			// macOS: SecRandomCopyBytes（一次调用填满）
			return (::SecRandomCopyBytes(kSecRandomDefault, n, p) == errSecSuccess);

#	else
			// 无可用 OS 密码学熵源 → 返回 false，SecureRandomBytes 将抛出异常
			// 非安全场景的播种请使用 RandomSeed()（含 random_device → 时间戳回退链）
			(void)p; (void)n;
			return false;
#	endif
		}

		// 返回 true 当且仅当编译期检测到 OS 密码学熵源 API（BCryptGenRandom/getrandom/SecRandomCopyBytes）
		// 返回 false 表示当前运行在 std::random_device 兜底路径，ChaCha20() 默认构造不保证密码学安全
		[[nodiscard]]
		inline bool HasCryptoGradeOsEntropy() noexcept
		{
#	if (defined(_WIN32) && __has_include(<bcrypt.h>)) || (defined(__linux__) && __has_include(<sys/random.h>)) || (defined(__APPLE__) && __has_include(<Security/Security.h>))
			return true;
#	else
			return false;
#	endif
		}

		// ── A4 ChaCha20 常数与辅助 ──
		// ChaCha20 常数 "expand 32-byte k"（RFC 8439 §2.3）
		inline constexpr std::uint32_t ChaCha20Constants[4] = {
			0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u
		};
		// 参考 NIST SP 800-90A reseed_interval 概念（SP 800-90A 涵盖 Hash/HMAC/CTR_DRBG，不含 ChaCha20；
		// 此处借用其"周期性强制 reseed 提供前向安全"思想，取保守阈值）
		inline constexpr std::uint64_t ChaCha20ReseedThreshold = 1ULL << 20;  // 1 MB

		// ChaCha20 quarter-round（仅 add/xor/rotl，常时间友好）
		static void ChaCha20QuarterRound(std::uint32_t& a, std::uint32_t& b,
		                                 std::uint32_t& c, std::uint32_t& d) noexcept
		{
			a += b; d ^= a; d = RotL(d, 16);
			c += d; b ^= c; b = RotL(b, 12);
			a += b; d ^= a; d = RotL(d, 8);
			c += d; b ^= c; b = RotL(b, 7);
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
	//	xoshiro256**
	//
	inline constexpr Xoshiro256StarStar::result_type Xoshiro256StarStar::operator()() noexcept
	{
		const std::uint64_t result = detail::RotL(s_[1] * 5, 7) * 9;
		const std::uint64_t t = s_[1] << 17;
		s_[2] ^= s_[0];
		s_[3] ^= s_[1];
		s_[1] ^= s_[2];
		s_[0] ^= s_[3];
		s_[2] ^= t;
		s_[3] = detail::RotL(s_[3], 45);
		return result;
	}

	inline constexpr void Xoshiro256StarStar::jump() noexcept
	{
		constexpr std::uint64_t p[] = {
			0x180ec6d33cfd0aba, 0xd5a61266f0c9392c,
			0xa9582618e03fc9aa, 0x39abdc4529b1661c };
		jumpPoly(p);
	}

	inline constexpr void Xoshiro256StarStar::longJump() noexcept
	{
		constexpr std::uint64_t p[] = {
			0x76e15d3efefdcbbf, 0xc5004e441c522fb3,
			0x77710069854ee241, 0x39109bb02acbe635 };
		jumpPoly(p);
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro128**
	//
	inline constexpr Xoroshiro128StarStar::result_type Xoroshiro128StarStar::operator()() noexcept
	{
		const std::uint64_t s0 = s_[0];
		std::uint64_t s1 = s_[1];
		const std::uint64_t result = detail::RotL(s0 * 5, 7) * 9;
		s1 ^= s0;
		s_[0] = detail::RotL(s0, 24) ^ s1 ^ (s1 << 16);
		s_[1] = detail::RotL(s1, 37);
		return result;
	}

	inline constexpr void Xoroshiro128StarStar::jump() noexcept
	{
		constexpr std::uint64_t p[] = { 0xdf900294d8f554a5, 0x170865df4b3201fc };
		jumpPoly(p);
	}

	inline constexpr void Xoroshiro128StarStar::longJump() noexcept
	{
		constexpr std::uint64_t p[] = { 0xd2a98b26625eee7b, 0xdddf9b1090aa7ac1 };
		jumpPoly(p);
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro128**
	//
	inline constexpr Xoshiro128StarStar::result_type Xoshiro128StarStar::operator()() noexcept
	{
		const std::uint32_t result = detail::RotL(s_[1] * 5, 7) * 9;
		const std::uint32_t t = s_[1] << 9;
		s_[2] ^= s_[0];
		s_[3] ^= s_[1];
		s_[1] ^= s_[2];
		s_[0] ^= s_[3];
		s_[2] ^= t;
		s_[3] = detail::RotL(s_[3], 11);
		return result;
	}

	inline constexpr void Xoshiro128StarStar::jump() noexcept
	{
		constexpr std::uint32_t p[] = { 0x8764000bu, 0xf542d2d3u, 0x6fa035c3u, 0x77f2db5bu };
		jumpPoly(p);
	}

	inline constexpr void Xoshiro128StarStar::longJump() noexcept
	{
		constexpr std::uint32_t p[] = { 0xb523952eu, 0x0b6f099fu, 0xccf5a0efu, 0x1c580662u };
		jumpPoly(p);
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro64**
	//
	inline constexpr Xoroshiro64StarStar::result_type Xoroshiro64StarStar::operator()() noexcept
	{
		const std::uint32_t s0 = s_[0];
		std::uint32_t s1 = s_[1];

		const std::uint32_t result = detail::RotL(s0 * 0x9E3779BB, 5) * 5;

		s1 ^= s0;
		s_[0] = detail::RotL(s0, 26) ^ s1 ^ (s1 << 9);
		s_[1] = detail::RotL(s1, 13);

		return result;
	}

	////////////////////////////////////////////////////////////////
	//
	//	SFC64 (Small Fast Counter)
	//
	inline constexpr SFC64::SFC64(const std::uint64_t seed) noexcept
		: Base()
	{
		// 使用 SplitMix64 播种 + 12 轮预热
		SplitMix64 sm{ seed };
		s_[0] = sm();
		s_[1] = sm();
		s_[2] = sm();
		s_[3] = 1;
		// 全零状态会导致输出可预测，强制修正
		if ((s_[0] | s_[1] | s_[2]) == 0) s_[0] = 0x9E3779B97F4A7C15ULL;
		for (int i = 0; i < 12; ++i) { operator()(); }
	}

	template <class SeedSeq, std::enable_if_t<!std::is_same_v<std::decay_t<SeedSeq>, SFC64>>*>
	inline constexpr SFC64::SFC64(SeedSeq& seq)
		: Base()
	{
		std::array<std::uint32_t, 8> seeds;
		seq.generate(seeds.begin(), seeds.end());
		s_[0] = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		s_[1] = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
		s_[2] = (static_cast<std::uint64_t>(seeds[4]) << 32) | seeds[5];
		s_[3] = 1;
		// 全零状态会导致输出可预测，强制修正
		if ((s_[0] | s_[1] | s_[2]) == 0) s_[0] = 0x9E3779B97F4A7C15ULL;
		// 与种子构造函数一致：12 轮预热
		for (int i = 0; i < 12; ++i) { operator()(); }
	}

	inline constexpr SFC64::result_type SFC64::operator()() noexcept
	{
		const std::uint64_t tmp = s_[0] + s_[1] + s_[3]++;
		s_[0] = s_[1] ^ (s_[1] >> 11);
		s_[1] = s_[2] + (s_[2] << 3);
		s_[2] = detail::RotL(s_[2], 24) + tmp;
		return tmp;
	}

	////////////////////////////////////////////////////////////////
	//
	//	RomuDuoJr
	//
	inline constexpr RomuDuoJr::result_type RomuDuoJr::operator()() noexcept
	{
		const std::uint64_t xp = s_[0];
		s_[0] = 15241094284759029579ULL * s_[1];
		s_[1] = detail::RotL(s_[1] - xp, 27);
		return xp;
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

		template <class Engine, class = void>
		struct HasLongJump : std::false_type {};
		template <class Engine>
		struct HasLongJump<Engine, std::void_t<decltype(std::declval<Engine&>().longJump())>> : std::true_type {};
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

		// 检测 C 是否为 random_access 容器
		template <class C, class = void>
		struct is_random_access_container : std::false_type {};

		template <class C>
		struct is_random_access_container<C, std::void_t<
			decltype(std::begin(std::declval<C&>())),
			decltype(std::end(std::declval<C&>()))>>
			: is_random_access_iterator<decltype(std::begin(std::declval<C&>()))> {};

		template <class C>
		inline constexpr bool is_random_access_container_v =
			is_random_access_container<C>::value;

		template <class Engine>
		[[nodiscard]]
		inline std::uint64_t Generate64Bits(Engine& engine)
		{
			if (sizeof(typename Engine::result_type) >= 8)
			{
				return static_cast<std::uint64_t>(engine());
			}
			const std::uint64_t lo = static_cast<std::uint64_t>(engine());
			const std::uint64_t hi = static_cast<std::uint64_t>(engine());
			return (hi << 32) | lo;
		}

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
		if constexpr (detail::HasLongJump<Engine>::value)
		{
			const std::uint64_t longJumps = streamId >> 32;
			const std::uint64_t shortJumps = streamId & 0xFFFFFFFFULL;
			for (std::uint64_t i = 0; i < longJumps; ++i)
				rng.longJump();
			for (std::uint64_t i = 0; i < shortJumps; ++i)
				rng.jump();
		}
		else
		{
			for (std::uint64_t i = 0; i < streamId; ++i)
				rng.jump();
		}
		return rng;
	}

	////////////////////////////////////////////////////////////////
	//
	//	便捷工具函数
	//

	// 生成非确定性的 64 位种子（优先硬件 RNG，用于统计 PRNG 播种）
	// 优先级链：RDRAND (x86_64) → detail::GetOsEntropyBytes → std::random_device → 时间戳回退
	[[nodiscard]]
	inline std::uint64_t RandomSeed()
	{
		std::uint64_t hw;
		if (detail::HardwareRand64(hw))
			return hw;
		if (detail::GetOsEntropyBytes(&hw, sizeof(hw)))
			return hw;
		std::random_device rd;
		try
		{
			return (static_cast<std::uint64_t>(rd()) << 32) | rd();
		}
		catch (...)
		{
			// 最终兜底：多维熵源（非密码学，仅保证 RandomSeed 永不抛异常，且防止 MSVC 15.6ms 时钟窗口下并发种子碰撞）
			const auto t1 = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			const auto t2 = std::chrono::steady_clock::now().time_since_epoch().count();
			const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
			static std::atomic<std::uint64_t> counter{0};
			std::uint64_t stackVar = 0;
			const std::uint64_t addr = reinterpret_cast<std::uint64_t>(&stackVar);

			const std::uint64_t rawSeed = static_cast<std::uint64_t>(t1) ^ static_cast<std::uint64_t>(t2)
			                              ^ threadId ^ addr ^ counter.fetch_add(1, std::memory_order_relaxed);
			SplitMix64 sm{ rawSeed };
			return sm();
		}
	}

	// 默认线程局部引擎，使用 RandomSeed() 播种（含 RDRAND → OS API → random_device → 时间戳回退链）
	[[nodiscard]]
	inline Xoshiro256StarStar& DefaultEngine()
	{
		thread_local Xoshiro256StarStar engine{ RandomSeed() };
		return engine;
	}

	/// @brief 重新播种当前线程的默认引擎（用于 POSIX fork() 产生子进程后重置引擎状态）
	inline void ResetThreadLocalEngine()
	{
		DefaultEngine() = Xoshiro256StarStar{ RandomSeed() };
	}

	/// @defgroup csprng 密码学安全
	/// @brief ChaCha20 CSPRNG 与 OS 熵源 API

	/// @brief 用 OS 密码学熵源填充 [buf, buf+n) 字节
	/// @param buf 目标缓冲区指针
	/// @param n 需要填充的字节数
	/// @throw std::runtime_error 当 OS 熵源不可用时抛出（CSPRNG 无熵不可用 = 致命错误）
	inline void SecureRandomBytes(void* buf, std::size_t n)
	{
		if (n == 0) return;
		if (!detail::GetOsEntropyBytes(buf, n))
			throw std::runtime_error("SecureRandomBytes: OS entropy source failed");
	}

	/// @brief 生成密码学安全的 64 位随机种子
	/// @return 复用 SecureRandomBytes 取前 8 字节的种子值
	[[nodiscard]]
	inline std::uint64_t SecureSeed()
	{
		std::uint64_t seed;
		SecureRandomBytes(&seed, sizeof(seed));
		return seed;
	}

	/// @brief 检测 OS 密码学熵源是否可用
	/// @return true 当且仅当 BCryptGenRandom/getrandom/SecRandomCopyBytes 可用；
	///         false 表示当前运行在 std::random_device 兜底路径，不保证密码学安全
	[[nodiscard]]
	inline bool IsOsCryptoEntropyAvailable() noexcept
	{
		return detail::HasCryptoGradeOsEntropy();
	}

	////////////////////////////////////////////////////////////////
	//
	//	ChaCha20 (RFC 8439) — CSPRNG 引擎实现
	//
	//  状态矩阵布局（16 × uint32，常数省略存于 m_state[0..11]）：
	//    0  1  2  3      "expa"  "nd 3"  "2-by"  "te k"   ← 常数（generateBlock 时补齐）
	//    4  5  6  7      key[0]  key[1]  key[2]  key[3]   ← m_state[0..3]
	//    8  9 10 11      key[4]  key[5]  key[6]  key[7]   ← m_state[4..7]
	//   12 13 14 15      ctr    nonce[0] nonce[1] nonce[2]← m_state[8..11]
	//
	//  生成流程：operator() → reseedIfNecessary → (缓存耗尽时)generateBlock → 取 8 字节
	//

	inline ChaCha20::ChaCha20(ChaCha20&& other) noexcept
		: m_state(other.m_state),
		  m_buffer(other.m_buffer),
		  m_bufferPos(other.m_bufferPos),
		  m_bytesSinceReseed(other.m_bytesSinceReseed),
		  m_autoReseed(other.m_autoReseed)
	{
		detail::SecureWipe(other.m_state.data(), sizeof(other.m_state));
		detail::SecureWipe(other.m_buffer.data(), sizeof(other.m_buffer));
		other.reseed();
	}

	inline ChaCha20& ChaCha20::operator=(ChaCha20&& other) noexcept
	{
		if (this != &other)
		{
			detail::SecureWipe(m_state.data(), sizeof(m_state));
			detail::SecureWipe(m_buffer.data(), sizeof(m_buffer));

			m_state = other.m_state;
			m_buffer = other.m_buffer;
			m_bufferPos = other.m_bufferPos;
			m_bytesSinceReseed = other.m_bytesSinceReseed;
			m_autoReseed = other.m_autoReseed;

			detail::SecureWipe(other.m_state.data(), sizeof(other.m_state));
			detail::SecureWipe(other.m_buffer.data(), sizeof(other.m_buffer));
			other.reseed();
		}
		return *this;
	}

	inline ChaCha20::~ChaCha20() noexcept
	{
		detail::SecureWipe(m_state.data(), sizeof(m_state));
		detail::SecureWipe(m_buffer.data(), sizeof(m_buffer));
	}

	// 构造方式 1：从 OS 熵自动播种（密码学安全，默认）
	inline ChaCha20::ChaCha20()
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(true)
	{
		reseed();  // 从 OS 熵获取 key + nonce，重置 counter
	}

	// 构造方式 2：显式种子（仅测试/复现，非密码学安全）
	// 用 SplitMix64 将 64-bit 种子扩展为 32 字节 key + 12 字节 nonce
	inline ChaCha20::ChaCha20(const std::uint64_t seed)
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(false)
	{
		SplitMix64 sm{ seed };
		// key: 前 4 次 SplitMix64 输出，每次 8 字节按小端序拆为 2 个 uint32
		for (int i = 0; i < 4; ++i)
		{
			const std::uint64_t v = sm();
			m_state[i * 2]     = static_cast<std::uint32_t>(v);
			m_state[i * 2 + 1] = static_cast<std::uint32_t>(v >> 32);
		}
		// nonce: 第 5 次输出（8 字节）+ 第 6 次输出低 4 字节（丢弃高 4 字节）
		{
			const std::uint64_t v5 = sm();
			m_state[9]  = static_cast<std::uint32_t>(v5);
			m_state[10] = static_cast<std::uint32_t>(v5 >> 32);
		}
		m_state[11] = static_cast<std::uint32_t>(sm());
		m_state[8]  = 0;  // counter 初值 = 0
	}

	// 构造方式 3：直接指定 key + nonce + counter
	inline ChaCha20::ChaCha20(const std::uint8_t* key, std::size_t keyLen,
	                          const std::uint8_t* nonce, std::size_t nonceLen,
	                          const std::uint32_t counter)
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(false)
	{
		if (keyLen != 32)
			throw std::invalid_argument("ChaCha20: key must be 32 bytes");
		if (nonceLen != 12)
			throw std::invalid_argument("ChaCha20: nonce must be 12 bytes");
		// key → m_state[0..7]（小端序）
		for (int i = 0; i < 8; ++i)
		{
			m_state[i] = static_cast<std::uint32_t>(key[i * 4])
			           | (static_cast<std::uint32_t>(key[i * 4 + 1]) << 8)
			           | (static_cast<std::uint32_t>(key[i * 4 + 2]) << 16)
			           | (static_cast<std::uint32_t>(key[i * 4 + 3]) << 24);
		}
		// nonce → m_state[9..11]（小端序）
		for (int i = 0; i < 3; ++i)
		{
			m_state[9 + i] = static_cast<std::uint32_t>(nonce[i * 4])
			               | (static_cast<std::uint32_t>(nonce[i * 4 + 1]) << 8)
			               | (static_cast<std::uint32_t>(nonce[i * 4 + 2]) << 16)
			               | (static_cast<std::uint32_t>(nonce[i * 4 + 3]) << 24);
		}
		m_state[8] = counter;  // counter
	}

	// 生成一个 ChaCha20 block（64 字节）填充 m_buffer
	inline void ChaCha20::generateBlock()
	{
		if (m_state[8] == 0xFFFFFFFFU)
		{
			throw std::overflow_error("ChaCha20: 32-bit block counter overflow");
		}

		// 构造完整 16-word 状态：常数 + key + counter + nonce
		std::array<std::uint32_t, 16> state{};
		state[0] = detail::ChaCha20Constants[0];
		state[1] = detail::ChaCha20Constants[1];
		state[2] = detail::ChaCha20Constants[2];
		state[3] = detail::ChaCha20Constants[3];
		for (int i = 0; i < 8; ++i) state[4 + i] = m_state[i];  // key
		state[12] = m_state[8];                                  // counter
		state[13] = m_state[9];                                  // nonce[0]
		state[14] = m_state[10];                                 // nonce[1]
		state[15] = m_state[11];                                 // nonce[2]

		std::array<std::uint32_t, 16> working = state;

		// 20 轮 = 10 次 double-round（列轮 + 对角轮）
		for (int i = 0; i < 10; ++i)
		{
			// 列轮 QR 顺序：(0,4,8,12) (1,5,9,13) (2,6,10,14) (3,7,11,15)
			detail::ChaCha20QuarterRound(working[0],  working[4],  working[8],  working[12]);
			detail::ChaCha20QuarterRound(working[1],  working[5],  working[9],  working[13]);
			detail::ChaCha20QuarterRound(working[2],  working[6],  working[10], working[14]);
			detail::ChaCha20QuarterRound(working[3],  working[7],  working[11], working[15]);
			// 对角轮 QR 顺序：(0,5,10,15) (1,6,11,12) (2,7,8,13) (3,4,9,14)
			detail::ChaCha20QuarterRound(working[0],  working[5],  working[10], working[15]);
			detail::ChaCha20QuarterRound(working[1],  working[6],  working[11], working[12]);
			detail::ChaCha20QuarterRound(working[2],  working[7],  working[8],  working[13]);
			detail::ChaCha20QuarterRound(working[3],  working[4],  working[9],  working[14]);
		}

		// 加初始状态后按小端序输出 64 字节到 m_buffer
		for (int i = 0; i < 16; ++i)
		{
			const std::uint32_t v = working[i] + state[i];
			m_buffer[i * 4 + 0] = static_cast<std::uint8_t>(v);
			m_buffer[i * 4 + 1] = static_cast<std::uint8_t>(v >> 8);
			m_buffer[i * 4 + 2] = static_cast<std::uint8_t>(v >> 16);
			m_buffer[i * 4 + 3] = static_cast<std::uint8_t>(v >> 24);
		}

		++m_state[8];   // 递增 counter（2^20 字节阈值远早于 2^32 回绕，自动 reseed 防止复用）
		m_bufferPos = 0;
	}

	// 自上次 reseed 以来输出字节数达到阈值时自动 reseed（前向安全）
	inline void ChaCha20::reseedIfNecessary()
	{
		if (m_autoReseed && m_bytesSinceReseed >= detail::ChaCha20ReseedThreshold)
			reseed();
	}

	// 从 OS 熵重新播种：32 字节新 key + 12 字节新 nonce，重置 counter=0、缓存标记耗尽
	inline void ChaCha20::reseed()
	{
		std::array<std::uint8_t, 44> seed;  // 32(key) + 12(nonce)
		SecureRandomBytes(seed.data(), seed.size());
		// key → m_state[0..7]（小端序）
		for (int i = 0; i < 8; ++i)
		{
			m_state[i] = static_cast<std::uint32_t>(seed[i * 4])
			           | (static_cast<std::uint32_t>(seed[i * 4 + 1]) << 8)
			           | (static_cast<std::uint32_t>(seed[i * 4 + 2]) << 16)
			           | (static_cast<std::uint32_t>(seed[i * 4 + 3]) << 24);
		}
		// nonce → m_state[9..11]（小端序）
		for (int i = 0; i < 3; ++i)
		{
			m_state[9 + i] = static_cast<std::uint32_t>(seed[32 + i * 4])
			               | (static_cast<std::uint32_t>(seed[32 + i * 4 + 1]) << 8)
			               | (static_cast<std::uint32_t>(seed[32 + i * 4 + 2]) << 16)
			               | (static_cast<std::uint32_t>(seed[32 + i * 4 + 3]) << 24);
		}
		m_state[8] = 0;            // counter 重置
		m_bufferPos = 64;          // 强制下次 operator() 触发新 block
		m_bytesSinceReseed = 0;
		detail::SecureWipe(seed.data(), seed.size());   // 擦除栈上密钥材料
		detail::SecureWipe(m_buffer.data(), m_buffer.size()); // 擦除旧 keystream
	}

	// 生成一个 64-bit 随机数（从缓存取 8 字节，缓存耗尽时生成新 block）
	inline ChaCha20::result_type ChaCha20::operator()()
	{
		reseedIfNecessary();
		if (m_bufferPos == 64)
			generateBlock();
		// 从缓存取 8 字节，小端序组装为 uint64_t
		std::uint64_t result = 0;
		for (int i = 0; i < 8; ++i)
			result |= static_cast<std::uint64_t>(m_buffer[m_bufferPos + i]) << (8 * i);
		m_bufferPos += 8;
		m_bytesSinceReseed += 8;
		return result;
	}

	inline void ChaCha20::discard(const unsigned long long n)
	{
		for (unsigned long long i = 0; i < n; ++i) operator()();
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

	/// @defgroup basic 基础生成
	/// @brief RandInt / RandReal / RandBool / RandChar / RandBits

	/// @brief 生成 [min, max] 范围内的随机整数
	/// @param min 下界（含）
	/// @param max 上界（含）
	/// @return 均匀分布于 [min, max] 的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(T min, T max)
	{
		return RandInt(DefaultEngine(), min, max);
	}

	/// @brief 生成 [0, max] 范围内的随机整数
	/// @param max 上界（含）
	/// @return 均匀分布于 [0, max] 的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(T max)
	{
		assert(max >= T{0});
		return RandInt<T>(T{0}, max);
	}

	/// @brief 生成 [min, max) 范围内的随机浮点数
	/// @param min 下界（含，默认 0）
	/// @param max 上界（不含，默认 1）
	/// @return 均匀分布于 [min, max) 的随机浮点数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandReal(T min = T{0}, T max = T{1})
	{
		return RandReal(DefaultEngine(), min, max);
	}

	/// @brief 采用无偏 Bit-Extraction 直通算法生成 [0, 1) 半开区间的随机浮点数（默认线程引擎）
	/// @tparam T 浮点数类型（float / double）
	/// @return [0, 1) 范围内的无偏伪随机浮点数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandCanonical() noexcept
	{
		return RandCanonical<T>(DefaultEngine());
	}

	/// @brief 生成 [0.0, 1.0) 半开区间的双精度浮点数（直通 Bit-Extraction 极速 API）
	[[nodiscard]]
	inline double RandCanonicalDouble() noexcept
	{
		return RandCanonical<double>();
	}

	/// @brief 生成 [0.0f, 1.0f) 半开区间的单精度浮点数（直通 Bit-Extraction 极速 API）
	[[nodiscard]]
	inline float RandCanonicalFloat() noexcept
	{
		return RandCanonical<float>();
	}

	/// @brief 生成随机布尔值
	/// @param p 为 true 的概率（默认 0.5）
	/// @return 以概率 p 返回 true
	[[nodiscard]]
	inline bool RandBool(double p = 0.5)
	{
		assert(std::isfinite(p) && p >= 0.0 && p <= 1.0);
		std::bernoulli_distribution dist(p);
		return dist(DefaultEngine());
	}

	/// @brief 生成随机布尔值（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 为 true 的概率（默认 0.5）
	/// @return 以概率 p 返回 true
	template <class Engine>
	[[nodiscard]]
	inline bool RandBool(Engine& engine, double p = 0.5)
	{
		assert(std::isfinite(p) && p >= 0.0 && p <= 1.0);
		std::bernoulli_distribution dist(p);
		return dist(engine);
	}

	/// @brief 伯努利分布（RandBool 的别名封装，对齐 \<random\> 命名）
	/// @param p 成功概率（默认 0.5）
	/// @return 以概率 p 返回 true
	[[nodiscard]]
	inline bool RandBernoulli(double p = 0.5)
	{
		assert(p >= 0.0 && p <= 1.0);
		return RandBool(p);
	}

	/// @brief 伯努利分布（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 成功概率（默认 0.5）
	/// @return 以概率 p 返回 true
	template <class Engine>
	[[nodiscard]]
	inline bool RandBernoulli(Engine& engine, double p = 0.5)
	{
		assert(p >= 0.0 && p <= 1.0);
		return RandBool(engine, p);
	}

	/// @brief 生成 [min, max] 范围内的随机字符
	/// @param min 下界字符（含）
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [min, max] 的随机字符
	/// @note 内部用 int64_t 避免 char32_t 范围（最大 0xFFFFFFFF）溢出 int32_t
	template <class CharT,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(CharT min, CharT max)
	{
		assert(min <= max);
		using IntT = std::int64_t;
		std::uniform_int_distribution<IntT> dist(
			static_cast<IntT>(min), static_cast<IntT>(max));
		return static_cast<CharT>(dist(DefaultEngine()));
	}

	/// @brief 生成 [CharT{}, max] 范围内的随机字符
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [CharT{}, max] 的随机字符
	template <class CharT,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(CharT max)
	{
		return RandChar<CharT>(CharT{}, max);
	}

	/// @brief 生成 [min, max] 范围内的随机字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界字符（含）
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [min, max] 的随机字符
	template <class CharT, class Engine,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT min, CharT max)
	{
		assert(min <= max);
		using IntT = std::int64_t;
		std::uniform_int_distribution<IntT> dist(
			static_cast<IntT>(min), static_cast<IntT>(max));
		return static_cast<CharT>(dist(engine));
	}

	/// @brief 生成 [CharT{}, max] 范围内的随机字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [CharT{}, max] 的随机字符
	template <class CharT, class Engine,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT max)
	{
		return RandChar<CharT>(engine, CharT{}, max);
	}

	////////////////////////////////////////////////////////////////
	//
	//	RandChar / RandString 预设字符集
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
		Hex,           // [0-9a-f]        16 个
		Printable,     // [!-~]           94 个可打印 ASCII
		Base64,        // [A-Za-z0-9+/]   64 个（RFC 4648 §4 标准变体）
		Base64UrlSafe, // [A-Za-z0-9-_]   64 个（RFC 4648 §5 URL-safe 变体）
	};

	namespace detail
	{
		// RandSample 分支选择阈值：n·K < size 时用 hash-set（实测交叉点 n≈N/127，K=64 留 2× 裕度）
		inline constexpr std::uint64_t HashSetThresholdK = 64;

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
				return "0123456789abcdef";
			case CharSet::Printable:
				return "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
			case CharSet::Base64:
				return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			case CharSet::Base64UrlSafe:
				return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
			}
			return "";
		}
	}

	/// @brief 从预设字符集随机取一个字符
	/// @param cs 预设字符集枚举
	/// @return 从字符集中均匀选取的 char（预设集均为 ASCII 范围）
	/// @throw std::invalid_argument 字符集为空时抛出
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

	/// @brief 从预设字符集随机取一个字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param cs 预设字符集枚举
	/// @return 从字符集中均匀选取的 char
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

	/// @defgroup containers 容器操作
	/// @brief RandElement / RandSample / RandShuffle / RandPermutation / RandFill / RandVector

	/// @brief 从容器中随机取一个元素（左值容器，返回引用）
	/// @param c 源容器（需支持 operator[] 和 size()）
	/// @return 容器中随机选取的一个元素的引用
	/// @throw std::invalid_argument 容器为空时抛出
	template <class Container,
		std::enable_if_t<detail::is_random_access_container_v<Container>>* = nullptr>
	[[nodiscard]]
	inline decltype(auto) RandElement(Container& c)
	{
		if (std::empty(c))
			throw std::invalid_argument("RandElement: empty container");
		return c[RandInt<std::size_t>(static_cast<std::size_t>(std::size(c) - 1))];
	}

	/// @brief 从容器中随机取一个元素（右值容器，按值返回以避免悬垂引用）
	/// @param c 源容器（需支持 operator[] 和 size()）
	/// @return 容器中随机选取的一个元素的副本
	/// @throw std::invalid_argument 容器为空时抛出
	template <class Container,
		std::enable_if_t<detail::is_random_access_container_v<std::decay_t<Container>>>* = nullptr>
	[[nodiscard]]
	inline typename std::iterator_traits<decltype(std::begin(std::declval<Container&>()))>::value_type RandElement(Container&& c)
	{
		if (std::empty(c))
			throw std::invalid_argument("RandElement: empty container");
		return c[RandInt<std::size_t>(static_cast<std::size_t>(std::size(c) - 1))];
	}

	/// @brief 从迭代器范围内随机取一个元素（随机访问迭代器：O(1) 直接定位）
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器
	/// @return 指向随机选取元素的迭代器
	/// @throw std::invalid_argument 范围为空时抛出
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

	/// @brief 从迭代器范围内随机取一个元素（输入迭代器：O(n) reservoir sampling）
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器
	/// @return 随机选取的元素值
	/// @throw std::invalid_argument 范围为空时抛出
	template <class It,
		std::enable_if_t<detail::is_input_iterator_v<It>
			&& !detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline typename std::iterator_traits<It>::value_type RandElement(It first, It last)
	{
		if (first == last)
			throw std::invalid_argument("RandElement: empty range");
		typename std::iterator_traits<It>::value_type selected = *first;
		++first;
		for (typename std::iterator_traits<It>::difference_type i = 1;
			first != last; ++first, ++i)
		{
			if (RandInt<typename std::iterator_traits<It>::difference_type>(0, i) == 0)
				selected = *first;
		}
		return selected;
	}

	/// @brief 从迭代器范围内随机取一个元素（指定引擎，随机访问迭代器）
	/// @param engine 自定义随机数引擎
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器
	/// @return 指向随机选取元素的迭代器
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

	/// @brief 从迭代器范围内随机取一个元素（指定引擎，输入迭代器）
	/// @param engine 自定义随机数引擎
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器
	/// @return 随机选取的元素值
	template <class It, class Engine,
		std::enable_if_t<detail::is_input_iterator_v<It>
			&& !detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline typename std::iterator_traits<It>::value_type RandElement(Engine& engine, It first, It last)
	{
		if (first == last)
			throw std::invalid_argument("RandElement: empty range");
		typename std::iterator_traits<It>::value_type selected = *first;
		++first;
		for (typename std::iterator_traits<It>::difference_type i = 1;
			first != last; ++first, ++i)
		{
			if (RandInt<typename std::iterator_traits<It>::difference_type>(
				engine, typename std::iterator_traits<It>::difference_type{0}, i) == 0)
				selected = *first;
		}
		return selected;
	}


	/// @defgroup distributions 统计分布
	/// @brief 16 种标准统计分布便捷函数

	/// @brief 生成正态分布随机数
	/// @param mean 均值（默认 0）
	/// @param stddev 标准差（默认 1）
	/// @return 服从 N(mean, stddev) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandNormal(T mean = T{0}, T stddev = T{1})
	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandNormal: invalid mean or stddev");
		std::normal_distribution<T> dist(mean, stddev);
		return dist(DefaultEngine());
	}

	/// @brief 生成正态分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 均值（默认 0）
	/// @param stddev 标准差（默认 1）
	/// @return 服从 N(mean, stddev) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandNormal: invalid mean or stddev");
		std::normal_distribution<T> dist(mean, stddev);
		return dist(engine);
	}

	/// @brief 随机打乱容器
	/// @param c 待打乱的容器
	template <class Container,
		std::enable_if_t<detail::is_random_access_container_v<Container>>* = nullptr>
	inline void RandShuffle(Container&& c)
	{
		std::shuffle(c.begin(), c.end(), DefaultEngine());
	}

	/// @brief 用 [min, max] 范围的随机整数填充迭代器区间
	/// @param first 起始迭代器
	/// @param last 结束迭代器
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @note T 从 min/max 推导，不从迭代器 value_type 推导。
	///       若容器元素类型与 min/max 字面量类型不一致，需显式指定 T 或用匹配类型的字面量。
	template <class It, class T,
		std::enable_if_t<detail::is_rand_fillable_v<It, T>>* = nullptr>
	inline void RandFill(It first, It last, T min, T max)
	{
		assert(min <= max);
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

	/// @brief 用 [min, max] 范围的随机整数填充迭代器区间（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param first 起始迭代器
	/// @param last 结束迭代器
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	template <class It, class T, class Engine,
		std::enable_if_t<detail::is_rand_fillable_v<It, T>>* = nullptr>
	inline void RandFill(Engine& engine, It first, It last, T min, T max)
	{
		assert(min <= max);
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

	/// @brief 生成含 n 个随机整数的 vector
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max] 的随机整数 vector
	template <class T,
		std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
	{
		assert(min <= max);
		std::vector<T> v;
		v.reserve(n);
		auto& rng = DefaultEngine();
		std::uniform_int_distribution<T> dist(min, max);
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(rng));
		return v;
	}

	/// @brief 生成含 n 个随机浮点数的 vector
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max) 的随机浮点数 vector
	template <class T,
		std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
	{
		assert(min <= max);
		std::vector<T> v;
		v.reserve(n);
		auto& rng = DefaultEngine();
		std::uniform_real_distribution<T> dist(min, max);
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(rng));
		return v;
	}

	/// @brief 生成含 n 个随机整数的 vector（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max] 的随机整数 vector
	template <class T, class Engine,
		std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n)
	{
		assert(min <= max);
		std::vector<T> v;
		v.reserve(n);
		std::uniform_int_distribution<T> dist(min, max);
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(engine));
		return v;
	}

	/// @brief 生成含 n 个随机浮点数的 vector（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max) 的随机浮点数 vector
	template <class T, class Engine,
		std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n)
	{
		assert(min <= max);
		std::vector<T> v;
		v.reserve(n);
		std::uniform_real_distribution<T> dist(min, max);
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(engine));
		return v;
	}

	/// @brief 按权重随机选取索引
	/// @param weights 权重容器（元素为数值类型）
	/// @return 按权重概率选中的索引值
	template <class WeightContainer>
	[[nodiscard]]
	inline typename WeightContainer::size_type RandWeighted(const WeightContainer& weights)
	{
		assert(!weights.empty() && std::all_of(weights.begin(), weights.end(), [](auto w) { return w >= 0; }) && std::any_of(weights.begin(), weights.end(), [](auto w) { return w > 0; }));
		using Size = typename WeightContainer::size_type;
		std::discrete_distribution<Size> dist(weights.begin(), weights.end());
		return dist(DefaultEngine());
	}

	/// @brief 按权重随机选取索引（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param weights 权重容器（元素为数值类型）
	/// @return 按权重概率选中的索引值
	template <class Engine, class WeightContainer>
	[[nodiscard]]
	inline typename WeightContainer::size_type RandWeighted(Engine& engine, const WeightContainer& weights)
	{
		assert(!weights.empty() && std::all_of(weights.begin(), weights.end(), [](auto w) { return w >= 0; }) && std::any_of(weights.begin(), weights.end(), [](auto w) { return w > 0; }));
		using Size = typename WeightContainer::size_type;
		std::discrete_distribution<Size> dist(weights.begin(), weights.end());
		return dist(engine);
	}

	/// @brief 按预构建权重分布随机选取索引（支持高频抽取复用，O(1) 复杂度）
	/// @param dist 预构建的 std::discrete_distribution 对象
	/// @return 按权重概率选中的索引值
	template <class IntType>
	[[nodiscard]]
	inline IntType RandWeighted(std::discrete_distribution<IntType>& dist)
	{
		return dist(DefaultEngine());
	}

	/// @brief 按预构建权重分布随机选取索引（指定引擎，支持高频抽取复用，O(1) 复杂度）
	/// @param engine 自定义随机数引擎
	/// @param dist 预构建的 std::discrete_distribution 对象
	/// @return 按权重概率选中的索引值
	template <class Engine, class IntType>
	[[nodiscard]]
	inline IntType RandWeighted(Engine& engine, std::discrete_distribution<IntType>& dist)
	{
		return dist(engine);
	}

	/// @brief 生成 [min, max] 范围内的随机整数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界（含）
	/// @param max 上界（含）
	/// @return 均匀分布于 [min, max] 的随机整数
	template <class T, class Engine, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(Engine& engine, T min, T max)
	{
		assert(min <= max);
		using DistType = std::conditional_t<(sizeof(T) < sizeof(short)),
			std::conditional_t<std::is_signed_v<T>, int, unsigned int>, T>;
		std::uniform_int_distribution<DistType> dist(static_cast<DistType>(min), static_cast<DistType>(max));
		return static_cast<T>(dist(engine));
	}

	/// @brief 采用无偏 Bit-Extraction 直通算法生成 [0, 1) 半开区间的随机浮点数（指定引擎重载）
	/// @tparam T 浮点数类型（float / double）
	/// @param engine 伪随机数生成引擎（自动兼容 32 位与 64 位输出引擎）
	/// @return [0, 1) 范围内的无偏伪随机浮点数
	template <typename T = double, class Engine,
	          typename std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
	[[nodiscard]] inline constexpr T RandCanonical(Engine& engine) noexcept
	{
		using ResultType = typename Engine::result_type;
		constexpr std::size_t Bits = sizeof(ResultType) * 8;

		if constexpr (std::is_same_v<T, double>)
		{
			if constexpr (Bits >= 64)
			{
				const std::uint64_t r = static_cast<std::uint64_t>(engine());
				return static_cast<double>(r >> 11) * 0x1.0p-53;
			}
			else
			{
				const std::uint64_t high = static_cast<std::uint64_t>(engine());
				const std::uint64_t low  = static_cast<std::uint64_t>(engine());
				const std::uint64_t r = (high << 32) | low;
				return static_cast<double>(r >> 11) * 0x1.0p-53;
			}
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			if constexpr (Bits >= 64)
			{
				const std::uint64_t r = static_cast<std::uint64_t>(engine());
				return static_cast<float>(r >> 40) * 0x1.0p-24f;
			}
			else
			{
				const std::uint32_t r = static_cast<std::uint32_t>(engine());
				return static_cast<float>(r >> 8) * 0x1.0p-24f;
			}
		}
		else
		{
			return std::generate_canonical<T, std::numeric_limits<T>::digits>(engine);
		}
	}

	/// @brief 生成 [min, max) 范围内的随机浮点数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界（含，默认 0）
	/// @param max 上界（不含，默认 1）
	/// @return 均匀分布于 [min, max) 的随机浮点数
	template <class T = double, class Engine, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandReal(Engine& engine, T min = T{0}, T max = T{1})
	{
		assert(std::isfinite(min) && std::isfinite(max) && min <= max);
		if (min == T{0} && max == T{1})
		{
			return RandCanonical<T>(engine);
		}
		std::uniform_real_distribution<T> dist(min, max);
		T val = dist(engine);
		if (val >= max) val = std::nextafter(max, min);
		return val;
	}

	////////////////////////////////////////////////////////////////
	//
	//	扩展便捷 API
	//

	/// @brief 无放回抽样：从容器中随机抽取 n 个元素（Fisher-Yates 前 n 步）
	/// @param c 源容器
	/// @param n 抽取数量（若 n >= 容器大小则返回全部元素的副本）
	/// @return 含 n 个随机选取元素的 vector
	template <class Container,
		std::enable_if_t<detail::is_random_access_container_v<Container>>* = nullptr>
	[[nodiscard]]
	inline auto RandSample(const Container& c, std::size_t n)
	{
		using T = typename std::iterator_traits<decltype(std::begin(c))>::value_type;
		using Size = std::size_t;
		std::vector<T> pool(std::begin(c), std::end(c));
		const Size size = pool.size();
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
	// RandSample 迭代器版
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
		if (n <= 0 || size <= 0)
			return {};
		if (n >= size)
			return std::vector<T>(first, last);

		auto& rng = DefaultEngine();

		// 分支选择：n·K < size 时 hash-set 内存优（O(n)）；否则索引数组常数优（O(N)）
		const auto sizeU = static_cast<std::uint64_t>(size);
		// 线性阈值：n·K < size 时用 hash-set（实测交叉点 n≈N/127，K=64 留 2× 裕度）
		if (static_cast<std::uint64_t>(n) * detail::HashSetThresholdK < sizeU)
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
		if (n <= 0 || size <= 0)
			return {};
		if (n >= size)
			return std::vector<T>(first, last);

		const auto sizeU = static_cast<std::uint64_t>(size);
		// 线性阈值：n·K < size 时用 hash-set（实测交叉点 n≈N/127，K=64 留 2× 裕度）
		if (static_cast<std::uint64_t>(n) * detail::HashSetThresholdK < sizeU)
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

	/// @brief 生成 [0, n) 的随机排列
	/// @param n 排列长度
	/// @return 含 [0, n) 随机排列的 vector
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

	/// @defgroup strings 字符串与 ID
	/// @brief RandString / RandUUID

	/// @brief 生成指定长度的随机字符串
	/// @param length 字符串长度
	/// @param charset 可用字符集（默认为字母+数字）
	/// @return 从 charset 中均匀选取字符组成的随机字符串
	/// @throw std::invalid_argument charset 为空时抛出
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

	/// @brief 从预设字符集生成随机字符串
	/// @param n 字符串长度
	/// @param cs 预设字符集枚举
	/// @return 从预设字符集中均匀选取字符组成的随机字符串
	[[nodiscard]]
	inline std::string RandString(std::size_t n, CharSet cs)
	{
		return RandString(n, detail::CharSetString(cs));
	}

	/// @brief 生成指定长度的随机字符串（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 字符串长度
	/// @param charset 可用字符集
	/// @return 从 charset 中均匀选取字符组成的随机字符串
	/// @throw std::invalid_argument charset 为空时抛出
	template <class Engine>
	[[nodiscard]]
	inline std::string RandString(Engine& engine, std::size_t n, std::string_view charset)
	{
		if (charset.empty())
			throw std::invalid_argument("RandString: charset is empty");
		std::string result(n, '\0');
		std::uniform_int_distribution<std::size_t> dist(0, charset.size() - 1);
		for (std::size_t i = 0; i < n; ++i)
			result[i] = charset[dist(engine)];
		return result;
	}

	/// @brief 从预设字符集生成随机字符串（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 字符串长度
	/// @param cs 预设字符集枚举
	/// @return 从预设字符集中均匀选取字符组成的随机字符串
	template <class Engine>
	[[nodiscard]]
	inline std::string RandString(Engine& engine, std::size_t n, CharSet cs)
	{
		return RandString(engine, n, detail::CharSetString(cs));
	}

	/// @brief 生成指数分布随机数
	/// @param lambda 速率参数（默认 1，均值 = 1/lambda）
	/// @return 服从 Exp(lambda) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExp(T lambda = T{1})
	{
		if (!std::isfinite(lambda) || lambda <= T{0})
			throw std::invalid_argument("RandExp: lambda must be positive");
		std::exponential_distribution<T> dist(lambda);
		return dist(DefaultEngine());
	}

	/// @brief 生成指数分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param lambda 速率参数（默认 1，均值 = 1/lambda）
	/// @return 服从 Exp(lambda) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExp(Engine& engine, T lambda = T{1})
	{
		if (!std::isfinite(lambda) || lambda <= T{0})
			throw std::invalid_argument("RandExp: lambda must be positive");
		std::exponential_distribution<T> dist(lambda);
		return dist(engine);
	}

	/// @brief 生成泊松分布随机数
	/// @param mean 均值参数（默认 1.0）
	/// @return 服从 Poisson(mean) 的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandPoisson(double mean = 1.0)
	{
		if (!std::isfinite(mean) || mean < 0.0)
			throw std::invalid_argument("RandPoisson: mean must be non-negative");
		if (mean == 0.0) return T{0};
		std::poisson_distribution<T> dist(mean);
		return dist(DefaultEngine());
	}

	/// @brief 生成泊松分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 均值参数（默认 1.0）
	/// @return 服从 Poisson(mean) 的随机整数
	template <class Engine, class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandPoisson(Engine& engine, double mean = 1.0)
	{
		if (!std::isfinite(mean) || mean < 0.0)
			throw std::invalid_argument("RandPoisson: mean must be non-negative");
		if (mean == 0.0) return T{0};
		std::poisson_distribution<T> dist(mean);
		return dist(engine);
	}

	/// @brief 生成伽马分布随机数
	/// @param alpha 形状参数（默认 1）
	/// @param beta 尺度参数（默认 1）
	/// @return 服从 Gamma(alpha, beta) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGamma(T alpha = T{1}, T beta = T{1})
	{
		if (!std::isfinite(alpha) || !std::isfinite(beta) || alpha <= T{0} || beta <= T{0})
			throw std::invalid_argument("RandGamma: alpha and beta must be positive");
		std::gamma_distribution<T> dist(alpha, beta);
		return dist(DefaultEngine());
	}

	/// @brief 生成伽马分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param alpha 形状参数（默认 1）
	/// @param beta 尺度参数（默认 1）
	/// @return 服从 Gamma(alpha, beta) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGamma(Engine& engine, T alpha = T{1}, T beta = T{1})
	{
		if (!std::isfinite(alpha) || !std::isfinite(beta) || alpha <= T{0} || beta <= T{0})
			throw std::invalid_argument("RandGamma: alpha and beta must be positive");
		std::gamma_distribution<T> dist(alpha, beta);
		return dist(engine);
	}

	/// @brief 生成二项分布随机数
	/// @param t 试验次数（默认 1）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 B(t, p) 的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBinomial(T t = 1, double p = 0.5)
	{
		if (t < 0 || !std::isfinite(p) || p < 0.0 || p > 1.0)
			throw std::invalid_argument("RandBinomial: invalid t or p");
		std::binomial_distribution<T> dist(t, p);
		return dist(DefaultEngine());
	}

	/// @brief 生成二项分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param t 试验次数（默认 1）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 B(t, p) 的随机整数
	template <class Engine, class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBinomial(Engine& engine, T t = 1, double p = 0.5)
	{
		if (t < 0 || !std::isfinite(p) || p < 0.0 || p > 1.0)
			throw std::invalid_argument("RandBinomial: invalid t or p");
		std::binomial_distribution<T> dist(t, p);
		return dist(engine);
	}

	/// @brief 生成对数正态分布随机数
	/// @param mean 对数均值（默认 0）
	/// @param stddev 对数标准差（默认 1）
	/// @return 服从 LogNormal(mean, stddev) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandLogNormal(T mean = T{0}, T stddev = T{1})
	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandLogNormal: invalid mean or stddev");
		std::lognormal_distribution<T> dist(mean, stddev);
		return dist(DefaultEngine());
	}

	/// @brief 生成对数正态分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 对数均值（默认 0）
	/// @param stddev 对数标准差（默认 1）
	/// @return 服从 LogNormal(mean, stddev) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandLogNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandLogNormal: invalid mean or stddev");
		std::lognormal_distribution<T> dist(mean, stddev);
		return dist(engine);
	}

	/// @brief 生成几何分布随机数（首次成功前的失败次数）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 Geometric(p) 的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGeometric(double p = 0.5)
	{
		if (!std::isfinite(p) || p <= 0.0 || p > 1.0)
			throw std::invalid_argument("RandGeometric: p must be in (0, 1]");
		std::geometric_distribution<T> dist(p);
		return dist(DefaultEngine());
	}

	/// @brief 生成几何分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 Geometric(p) 的随机整数
	template <class Engine, class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGeometric(Engine& engine, double p = 0.5)
	{
		if (!std::isfinite(p) || p <= 0.0 || p > 1.0)
			throw std::invalid_argument("RandGeometric: p must be in (0, 1]");
		std::geometric_distribution<T> dist(p);
		return dist(engine);
	}

	/// @brief 生成柯西分布随机数
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Cauchy(a, b) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandCauchy(T a = T{0}, T b = T{1})
	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandCauchy: invalid a or b");
		std::cauchy_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	/// @brief 生成柯西分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Cauchy(a, b) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandCauchy(Engine& engine, T a = T{0}, T b = T{1})
	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandCauchy: invalid a or b");
		std::cauchy_distribution<T> dist(a, b);
		return dist(engine);
	}

	/// @brief 生成韦布尔分布随机数
	/// @param a 形状参数（默认 1）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Weibull(a, b) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandWeibull(T a = T{1}, T b = T{1})
	{
		if (!std::isfinite(a) || !std::isfinite(b) || a <= T{0} || b <= T{0})
			throw std::invalid_argument("RandWeibull: invalid a or b");
		std::weibull_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	/// @brief 生成韦布尔分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 形状参数（默认 1）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Weibull(a, b) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandWeibull(Engine& engine, T a = T{1}, T b = T{1})
	{
		if (!std::isfinite(a) || !std::isfinite(b) || a <= T{0} || b <= T{0})
			throw std::invalid_argument("RandWeibull: invalid a or b");
		std::weibull_distribution<T> dist(a, b);
		return dist(engine);
	}

	/// @brief 生成极值分布（Gumbel）随机数
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 ExtremeValue(a, b) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExtremeValue(T a = T{0}, T b = T{1})
	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandExtremeValue: invalid a or b");
		std::extreme_value_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	/// @brief 生成极值分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 ExtremeValue(a, b) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExtremeValue(Engine& engine, T a = T{0}, T b = T{1})
	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandExtremeValue: invalid a or b");
		std::extreme_value_distribution<T> dist(a, b);
		return dist(engine);
	}

	/// @brief 生成卡方分布随机数
	/// @param n 自由度（默认 1）
	/// @return 服从 ChiSquared(n) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandChiSquared(T n = T{1})
	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandChiSquared: n must be positive");
		std::chi_squared_distribution<T> dist(n);
		return dist(DefaultEngine());
	}

	/// @brief 生成卡方分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 自由度（默认 1）
	/// @return 服从 ChiSquared(n) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandChiSquared(Engine& engine, T n = T{1})
	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandChiSquared: n must be positive");
		std::chi_squared_distribution<T> dist(n);
		return dist(engine);
	}

	/// @brief 生成学生 t 分布随机数
	/// @param n 自由度（默认 1）
	/// @return 服从 StudentT(n) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandStudentT(T n = T{1})
	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandStudentT: n must be positive");
		std::student_t_distribution<T> dist(n);
		return dist(DefaultEngine());
	}

	/// @brief 生成学生 t 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 自由度（默认 1）
	/// @return 服从 StudentT(n) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandStudentT(Engine& engine, T n = T{1})
	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandStudentT: n must be positive");
		std::student_t_distribution<T> dist(n);
		return dist(engine);
	}

	/// @brief 生成 Fisher F 分布随机数
	/// @param m 第一自由度（默认 1）
	/// @param n 第二自由度（默认 1）
	/// @return 服从 FisherF(m, n) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandFisherF(T m = T{1}, T n = T{1})
	{
		if (!std::isfinite(m) || !std::isfinite(n) || m <= T{0} || n <= T{0})
			throw std::invalid_argument("RandFisherF: invalid m or n");
		std::fisher_f_distribution<T> dist(m, n);
		return dist(DefaultEngine());
	}

	/// @brief 生成 Fisher F 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param m 第一自由度（默认 1）
	/// @param n 第二自由度（默认 1）
	/// @return 服从 FisherF(m, n) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandFisherF(Engine& engine, T m = T{1}, T n = T{1})
	{
		if (!std::isfinite(m) || !std::isfinite(n) || m <= T{0} || n <= T{0})
			throw std::invalid_argument("RandFisherF: invalid m or n");
		std::fisher_f_distribution<T> dist(m, n);
		return dist(engine);
	}

	/// @brief 生成 Beta 分布随机数
	/// @param a 形状参数（默认 1）
	/// @param b 形状参数（默认 1）
	/// @return 服从 Beta(a, b) 的随机数
	/// @note 无 STL 对应，自实现 Gamma(a)/(Gamma(a)+Gamma(b))
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBeta(T a = T{1}, T b = T{1})
	{
		return RandBeta(DefaultEngine(), a, b);
	}

	/// @brief 生成 Beta 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 形状参数（默认 1）
	/// @param b 形状参数（默认 1）
	/// @return 服从 Beta(a, b) 的随机数
	template <class Engine, class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBeta(Engine& engine, T a = T{1}, T b = T{1})
	{
		if (!std::isfinite(a) || !std::isfinite(b) || a <= T{0} || b <= T{0})
			throw std::invalid_argument("RandBeta: invalid a or b");
		std::gamma_distribution<T> distA(a, T{1});
		std::gamma_distribution<T> distB(b, T{1});
		const T x = distA(engine);
		const T y = distB(engine);
		const T sum = x + y;
		if (sum == T{0} || !std::isfinite(sum))
		{
			if (std::isinf(x) && !std::isinf(y)) return T{1};
			if (!std::isinf(x) && std::isinf(y)) return T{0};
			const double ratio = 1.0 / (1.0 + (static_cast<double>(b) / static_cast<double>(a)));
			return RandBool(engine, ratio) ? T{1} : T{0};
		}
		return x / sum;
	}

	/// @brief 生成 N 位随机整数
	/// @tparam N 位数（1-64，且不超过 T 的位宽）
	/// @return 均匀分布于 [0, 2^N) 的随机整数
	template <int N, class T = std::uint64_t, std::enable_if_t<std::is_integral_v<T> && (N > 0) && (N <= 64) && (N <= static_cast<int>(sizeof(T) * 8))>* = nullptr>
	[[nodiscard]]
	inline T RandBits() noexcept
	{
		return RandBits<N, T>(DefaultEngine());
	}

	/// @brief 生成 N 位随机整数（指定引擎重载）
	/// @tparam N 位数（1-64，且不超过 T 的位宽）
	/// @param engine 自定义随机数引擎
	/// @return 均匀分布于 [0, 2^N) 的随机整数
	template <int N, class T = std::uint64_t, class Engine, std::enable_if_t<std::is_integral_v<T> && (N > 0) && (N <= 64) && (N <= static_cast<int>(sizeof(T) * 8))>* = nullptr>
	[[nodiscard]]
	inline T RandBits(Engine& engine) noexcept
	{
		if constexpr (N == 64)
			return static_cast<T>(engine());
		else
			return static_cast<T>(engine() & ((N >= 64) ? ~std::uint64_t{0} : ((std::uint64_t{1} << (N & 63)) - 1)));
	}

	/// @brief 生成随机 UUID v4 字符串
	/// @return 格式为 xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx 的 UUID 字符串
	/// @warning 使用默认 PRNG（非 CSPRNG），不适用于安全敏感标识符；
	///          安全场景请改用 ChaCha20 引擎重载或 SecureRandomBytes
	template <class Engine>
	[[nodiscard]]
	inline std::string RandUUID(Engine& engine)
	{
		static constexpr char hex[] = "0123456789abcdef";
		std::string uuid(36, '-');
		const std::uint64_t u1 = detail::Generate64Bits(engine);
		const std::uint64_t u2 = detail::Generate64Bits(engine);

		for (int i = 0; i < 8; ++i)
			uuid[i] = hex[(u1 >> (i * 4)) & 0xFU];
		for (int i = 0; i < 4; ++i)
			uuid[9 + i] = hex[(u1 >> ((8 + i) * 4)) & 0xFU];
		uuid[14] = '4';
		for (int i = 1; i < 4; ++i)
			uuid[14 + i] = hex[(u1 >> ((12 + i) * 4)) & 0xFU];
		uuid[19] = hex[8 + ((u2 >> 0) & 0x3U)];
		for (int i = 1; i < 4; ++i)
			uuid[19 + i] = hex[(u2 >> (i * 4)) & 0xFU];
		for (int i = 0; i < 12; ++i)
			uuid[24 + i] = hex[(u2 >> ((4 + i) * 4)) & 0xFU];

		return uuid;
	}

	[[nodiscard]]
	inline std::string RandUUID()
	{
		return RandUUID(DefaultEngine());
	}

	////////////////////////////////////////////////////////////////
	//
	//	静态断言：确认引擎满足 UniformRandomBitGenerator 要求
	//

	static_assert(std::is_same_v<SplitMix64::result_type, std::uint64_t>);
	static_assert(std::is_same_v<Xoshiro256StarStar::result_type, std::uint64_t>);
	static_assert(std::is_same_v<Xoroshiro128StarStar::result_type, std::uint64_t>);
	static_assert(std::is_same_v<Xoshiro128StarStar::result_type, std::uint32_t>);
	static_assert(std::is_same_v<Xoroshiro64StarStar::result_type, std::uint32_t>);
	static_assert(std::is_same_v<SFC64::result_type, std::uint64_t>);
	static_assert(std::is_same_v<RomuDuoJr::result_type, std::uint64_t>);
	static_assert(std::is_same_v<ChaCha20::result_type, std::uint64_t>);
	static_assert(SplitMix64::min() < SplitMix64::max());
	static_assert(Xoshiro256StarStar::min() < Xoshiro256StarStar::max());
	static_assert(Xoroshiro128StarStar::min() < Xoroshiro128StarStar::max());
	static_assert(Xoshiro128StarStar::min() < Xoshiro128StarStar::max());
	static_assert(Xoroshiro64StarStar::min() < Xoroshiro64StarStar::max());
	static_assert(SFC64::min() < SFC64::max());
	static_assert(RomuDuoJr::min() < RomuDuoJr::max());
	static_assert(ChaCha20::min() < ChaCha20::max());

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
		typename std::basic_ostream<CharT, Traits>::sentry ok(os);
		if (!ok) return os;

		const auto flags = os.flags();
		os.setf(std::ios_base::dec, std::ios_base::basefield);

		auto state = engine.serialize();
		auto it = state.begin();
		if (it != state.end())
		{
			os << *it;
			for (++it; it != state.end(); ++it)
				os << os.widen(' ') << *it;
		}

		os.flags(flags);
		return os;
	}

	// 流式恢复引擎状态
	// 若解析失败（读取不足、流错误或状态非法/全零），setstate(failbit) 且引擎状态保持不变
	// （与 std::random_engine 一致：先读取到临时 state，全部成功才 deserialize）
	template <class CharT, class Traits, class Engine,
		std::enable_if_t<detail::is_serializable_engine_v<Engine>>* = nullptr>
	std::basic_istream<CharT, Traits>&
	operator>>(std::basic_istream<CharT, Traits>& is, Engine& engine)
	{
		typename std::basic_istream<CharT, Traits>::sentry ok(is);
		if (!ok) return is;

		const auto flags = is.flags();
		is.setf(std::ios_base::dec, std::ios_base::basefield);
		is.setf(std::ios_base::skipws);

		typename Engine::state_type state{};
		std::size_t i = 0;
		for (; i < state.size() && is; ++i)
			is >> state[i];

		if (i == state.size() && is && detail::IsValidState(state))
		{
			engine.deserialize(state);
		}
		else
		{
			is.setstate(std::ios_base::failbit);
		}

		is.flags(flags);
		return is;
	}

}

#undef RANDX_NODISCARD_CXX20
