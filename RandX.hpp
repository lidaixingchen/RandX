//----------------------------------------------------------------------------------------
//
//	RandX.hpp — 基于 Xoshiro 的伪随机数生成器封装库（C++23）
//
//	原始算法：David Blackman & Sebastiano Vigna (http://prng.di.unimi.it/)
//	原始 C++ 封装：Ryo Suzuki (https://github.com/Reputeless/Xoshiro-cpp)
//
//========================================================================================
//
//	快速上手
//
//		#include "RandX.hpp"
//
//		// 最简用法：直接调用便捷函数（内部使用线程局部 Xoshiro256StarStar）
//		int   dice  = RandX::RandInt(1, 6);        // [1, 6] 闭区间整数
//		double coin = RandX::RandReal();            // [0.0, 1.0) 浮点数
//		bool  flag  = RandX::RandBool(0.3);         // 30% 概率为 true
//
//		std::vector<int> v = {10, 20, 30, 40};
//		auto& elem = RandX::RandElement(v);         // 随机取一个元素
//
//	扩展 API
//
//		auto sample = RandX::RandSample(v, 2);      // 无放回抽样 2 个
//		auto perm   = RandX::RandPermutation(10);   // [0,10) 随机排列
//		auto token  = RandX::RandString(16);        // 16 位随机字符串
//		auto uuid   = RandX::RandUUID();            // UUID v4
//		auto byte   = RandX::RandBits<8>();         // [0, 256) 随机整数
//		auto exp    = RandX::RandExp(2.0);          // 指数分布 λ=2
//		auto poi    = RandX::RandPoisson(5.0);      // 泊松分布 μ=5
//
//	手动管理引擎
//
//		// 用真随机种子创建引擎
//		RandX::Xoshiro256StarStar rng{ RandX::RandomSeed() };
//
//		// 指定引擎的便捷函数重载
//		int val = RandX::RandInt(rng, 0, 99);
//
//		// 配合标准库 distribution 使用（满足 UniformRandomBitGenerator）
//		std::normal_distribution<double> norm(0.0, 1.0);
//		double sample = norm(rng);
//
//	多流并行
//
//		auto s0 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(0);
//		auto s1 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(1);
//		// 各流间隔 2^128 步，互不重叠
//
//	编译期随机（constexpr）
//
//		constexpr int v = RandX::RandIntCE(0, 100);
//		constexpr auto shuffled = RandX::ShuffledArray<int, 5>({1,2,3,4,5});
//
//	序列化 / 反序列化（保存和恢复状态）
//
//		auto state = rng.serialize();
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
# include <cmath>
# include <cstdint>
# include <array>
# include <limits>
# include <concepts>
# include <random>
# include <algorithm>
# include <bit>
# include <cassert>
# include <type_traits>
# include <ranges>
# include <string>
# include <string_view>
# include <unordered_set>
# include <vector>
# include <iterator>
# include <cstddef>
# include <stdexcept>
# include <chrono>    // std::chrono（RandomSeed 时间戳兜底用）
# include <atomic>    // std::atomic（RandomSeed 兜底计数）
# include <functional>// std::hash（RandomSeed 线程 Hash）
# include <thread>    // std::this_thread（RandomSeed 线程 ID）
# include <ios>       // std::ios_base::failbit（流状态标志完整定义）
# include <istream>   // std::basic_istream（operator>> 所需完整类型）
# include <ostream>   // std::basic_ostream（operator<< 所需完整类型）
# include <stdint.h>
# include <string.h>
# if defined(_MSC_VER)
#	include <intrin.h>
# endif
# if defined(_MSC_VER) && (defined(__x86_64__) || defined(_M_X64))
#	include <immintrin.h>
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
#	if defined(min)
#		undef min
#	endif
#	if defined(max)
#		undef max
#	endif
#	include <bcrypt.h>
#	define RANDX_DETAIL_WINDOWS_SDK_HEADERS_AVAILABLE 1
#	if defined(_MSC_VER)
#		pragma comment(lib, "bcrypt.lib")  // 仅 MSVC 生效
#	endif
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
# include <cstring>   // std::memcpy（std::random_device 回退路径用）

namespace RandX
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

	class SFC64;

	// ── 引擎基础设施（提前定义，供 EngineBase CRTP 基类使用） ──
	namespace detail
	{
		[[nodiscard]]
		inline constexpr std::uint64_t RotL(const std::uint64_t x, const int s) noexcept
		{
			return std::rotl(x, s);
		}

		[[nodiscard]]
		inline constexpr std::uint32_t RotL(const std::uint32_t x, const int s) noexcept
		{
			return std::rotl(x, s);
		}

		// 检测状态数组是否全零（全零是 xoshiro/xoroshiro 的吸收态）
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

		template <class Engine>
		struct EngineStatePolicy
		{
			static constexpr bool AllowsZeroState = false;
		};

		template <>
		struct EngineStatePolicy<RandX::SFC64>
		{
			static constexpr bool AllowsZeroState = true;
		};

		template <class Engine, class State>
		[[nodiscard]]
		inline constexpr bool IsValidSnapshot(const State& state) noexcept
		{
			if constexpr (EngineStatePolicy<Engine>::AllowsZeroState)
			{
				return true;
			}
			else
			{
				return !IsAllZero(state);
			}
		}

		template <class S>
		concept SeedSequence =
			!std::is_integral_v<std::remove_cvref_t<S>> &&
			!std::is_enum_v<std::remove_cvref_t<S>> &&
			requires(S& s, std::uint32_t* p) {
				s.generate(p, p);
			};
	}

	/// @defgroup engines 引擎
	/// @brief 8 个伪随机数生成引擎（7 非 CSPRNG + 1 ChaCha20 CSPRNG）
	/// @{

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
		[[nodiscard]]
		explicit constexpr SplitMix64(state_type state = DefaultSeed) noexcept;

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <detail::SeedSequence SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
				&& !std::same_as<std::remove_cvref_t<SeedSeq>, SplitMix64>)
		[[nodiscard]]
		explicit constexpr SplitMix64(SeedSeq& seq)
		{
			std::array<std::uint32_t, 2> seeds;
			seq.generate(seeds.data(), seeds.data() + seeds.size());
			m_state = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		}

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

		friend auto operator <=>(const SplitMix64&, const SplitMix64&) = default;

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
				if constexpr (!EngineStatePolicy<Derived>::AllowsZeroState)
				{
					if (IsAllZero(s_))
					{
						s_[0] = static_cast<ResultType>(1);
					}
					assert(!IsAllZero(s_) && "absorbing all-zero state");
				}
			}

			// C++23: defaulted 三路比较（保留 ==, !=, <, >, <=, >= 全套）
			friend auto operator<=>(const EngineBase&, const EngineBase&) = default;

		protected:

			static constexpr int Bits = static_cast<int>(sizeof(ResultType) * 8);

			EngineBase() = default;

			// State 构造（用户直接传入，包含 Release/Debug 全零状态静默修正）
			explicit constexpr EngineBase(const state_type& state) noexcept
				: s_(state)
			{
				if constexpr (!EngineStatePolicy<Derived>::AllowsZeroState)
				{
					if (IsAllZero(s_))
					{
						s_[0] = static_cast<ResultType>(1);
					}
					assert(!IsAllZero(s_) && "absorbing all-zero state");
				}
			}

			// SeedSeq 构造（零状态修正，Release 安全）
			template <detail::SeedSequence SeedSeq>
				requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
					&& !std::same_as<std::remove_cvref_t<SeedSeq>, Derived>)
			explicit constexpr EngineBase(SeedSeq& seq)
			{
				if constexpr (sizeof(result_type) == 8)
				{
					std::array<std::uint32_t, N * 2> raw;
					seq.generate(raw.data(), raw.data() + raw.size());
					for (std::size_t i = 0; i < N; ++i)
						s_[i] = (static_cast<result_type>(raw[2 * i]) << 32) | raw[2 * i + 1];
				}
				else
				{
					std::array<std::uint32_t, N> raw;
					seq.generate(raw.data(), raw.data() + raw.size());
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
		[[nodiscard]]
		explicit constexpr Xoshiro256StarStar(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <detail::SeedSequence SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
				&& !std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro256StarStar>)
		[[nodiscard]]
		explicit constexpr Xoshiro256StarStar(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		[[nodiscard]]
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
		[[nodiscard]]
		explicit constexpr Xoroshiro128StarStar(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <detail::SeedSequence SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
				&& !std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro128StarStar>)
		[[nodiscard]]
		explicit constexpr Xoroshiro128StarStar(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		[[nodiscard]]
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
		[[nodiscard]]
		explicit constexpr Xoshiro128StarStar(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <detail::SeedSequence SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
				&& !std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro128StarStar>)
		[[nodiscard]]
		explicit constexpr Xoshiro128StarStar(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		[[nodiscard]]
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
		[[nodiscard]]
		explicit constexpr Xoroshiro64StarStar(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <detail::SeedSequence SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
				&& !std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro64StarStar>)
		[[nodiscard]]
		explicit constexpr Xoroshiro64StarStar(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		[[nodiscard]]
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
		[[nodiscard]]
		explicit constexpr SFC64(std::uint64_t seed) noexcept;

		/// @brief 从 std::seed_seq 播种（填充 3 状态字 + counter=1 + 12 轮预热）
		/// @param seq 种子序列对象
		template <detail::SeedSequence SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
				&& !std::same_as<std::remove_cvref_t<SeedSeq>, SFC64>)
		[[nodiscard]]
		explicit constexpr SFC64(SeedSeq& seq)
			: Base()
		{
			std::array<std::uint32_t, 8> seeds;
			seq.generate(seeds.data(), seeds.data() + seeds.size());
			s_[0] = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
			s_[1] = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
			s_[2] = (static_cast<std::uint64_t>(seeds[4]) << 32) | seeds[5];
			s_[3] = 1;
			// 全零状态会导致输出可预测，强制修正
			if ((s_[0] | s_[1] | s_[2]) == 0) s_[0] = 0x9E3779B97F4A7C15ULL;
			// 与种子构造函数一致：12 轮预热
			for (int i = 0; i < 12; ++i) { operator()(); }
		}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		[[nodiscard]]
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
		[[nodiscard]]
		explicit constexpr RomuDuoJr(std::uint64_t seed) noexcept
			: Base(seed) {}

		/// @brief 从 std::seed_seq 播种
		/// @param seq 种子序列对象
		template <detail::SeedSequence SeedSeq>
			requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
				&& !std::same_as<std::remove_cvref_t<SeedSeq>, RomuDuoJr>)
		[[nodiscard]]
		explicit constexpr RomuDuoJr(SeedSeq& seq)
			: Base(seq) {}

		/// @brief 从状态数组直接构造
		/// @param state serialize() 返回的状态
		[[nodiscard]]
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
		[[nodiscard]]
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
		[[nodiscard]]
		static constexpr result_type min() noexcept { return 0; }

		/// @brief 输出范围上界
		/// @return 2^64 - 1
		[[nodiscard]]
		static constexpr result_type max() noexcept { return UINT64_MAX; }

		// 不提供：serialize/deserialize, operator<</>>, jump/longJump（CSPRNG 安全约束）

	private:

		std::array<std::uint32_t, 12> m_state;   // key(8) + counter(1) + nonce(3)，常数省略（generateBlock 时补齐）
		std::array<std::uint8_t, 64>  m_buffer;  // 当前 block 的字节缓存
		std::size_t                   m_bufferPos{ 64 };       // 缓存消费位置 [0, 64)，==64 时触发新 block
		std::uint64_t                 m_bytesSinceReseed{ 0 }; // 自上次 reseed 以来输出的字节数
		bool                          m_autoReseed{ false }; // 是否在满 1MB 后自动从 OS 熵重新播种（仅默认无参构造函数启用）
		bool                          m_counterExhausted{ false }; // 32-bit block 计数器是否已耗尽（0xFFFFFFFF block 已生成）
		bool                          m_movedFrom{ false }; // 是否处于移出状态

		void generateBlock();        // 跑一次 ChaCha20 block 函数填充 m_buffer
		void reseedIfNecessary();    // m_bytesSinceReseed >= 阈值时自动 reseed
	};

	// ── sizeof 守卫：防止引擎 ABI 意外变化 ──
	static_assert(sizeof(Xoshiro256StarStar) == 32);
	static_assert(sizeof(Xoroshiro128StarStar) == 16);
	static_assert(sizeof(Xoshiro128StarStar) == 16);
	static_assert(sizeof(Xoroshiro64StarStar) == 8);
	static_assert(sizeof(SFC64) == 32);
	static_assert(sizeof(RomuDuoJr) == 16);
	static_assert(sizeof(SplitMix64) == 8);
}

////////////////////////////////////////////////////////////////

namespace RandX
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
		// 安全擦除后端及兼容实现
		inline void SecureWipeCompilerBarrier() noexcept
		{
#	if defined(_MSC_VER)
			_ReadWriteBarrier();
#	elif defined(__GNUC__) || defined(__clang__)
			__asm__ __volatile__("" ::: "memory");
#	endif
		}

		inline void SecureWipePortable(void* ptr, std::size_t len) noexcept
		{
			if (len == 0) return;

			volatile auto* destination = static_cast<volatile std::uint8_t*>(ptr);
			for (std::size_t index = 0; index < len; ++index)
				destination[index] = 0;
			SecureWipeCompilerBarrier();
		}
		enum class SecureWipeBackend
		{
			Portable,
			Windows,
			Glibc,
			Apple
		};

#	define RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MAJOR 2
#	define RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MINOR 25
#	define RANDX_DETAIL_MEMSET_S_MACOS_MINIMUM_VERSION 1090
#	define RANDX_DETAIL_MEMSET_S_IOS_MINIMUM_VERSION 70000

#	if defined(RANDX_USE_PORTABLE_SECURE_WIPE) && RANDX_USE_PORTABLE_SECURE_WIPE
#		define RANDX_DETAIL_SECURE_WIPE_BACKEND_PORTABLE
#	elif defined(RANDX_USE_APPLE_MEMSET_S) && RANDX_USE_APPLE_MEMSET_S && !defined(__APPLE__)
#		error RANDX_USE_APPLE_MEMSET_S requires an Apple target
#	elif defined(RANDX_DETAIL_WINDOWS_SDK_HEADERS_AVAILABLE)
#		define RANDX_DETAIL_SECURE_WIPE_BACKEND_WINDOWS
#	elif defined(__APPLE__) && defined(RANDX_USE_APPLE_MEMSET_S) && RANDX_USE_APPLE_MEMSET_S
#		if !defined(__STDC_WANT_LIB_EXT1__) || __STDC_WANT_LIB_EXT1__ != 1
#			error RANDX_USE_APPLE_MEMSET_S requires __STDC_WANT_LIB_EXT1__=1 before system headers
#		endif
#		if !defined(RSIZE_MAX)
#			error RANDX_USE_APPLE_MEMSET_S requires RSIZE_MAX from the target SDK
#		endif
#		if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#			if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < RANDX_DETAIL_MEMSET_S_MACOS_MINIMUM_VERSION
#				error memset_s is unavailable below the macOS 10.9 deployment target
#			endif
#		elif defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)
#			if __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < RANDX_DETAIL_MEMSET_S_IOS_MINIMUM_VERSION
#				error memset_s is unavailable below the iOS 7.0 deployment target
#			endif
#		else
#			error RANDX_USE_APPLE_MEMSET_S requires a validated macOS or iOS deployment target
#		endif
#		define RANDX_DETAIL_SECURE_WIPE_BACKEND_APPLE
#	elif defined(__linux__) && defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#		if __GLIBC_PREREQ(RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MAJOR, RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MINOR) && defined(__USE_MISC)
#			define RANDX_DETAIL_SECURE_WIPE_BACKEND_GLIBC
#		else
#			define RANDX_DETAIL_SECURE_WIPE_BACKEND_PORTABLE
#		endif
#	else
#		define RANDX_DETAIL_SECURE_WIPE_BACKEND_PORTABLE
#	endif

#	if defined(RANDX_DETAIL_SECURE_WIPE_BACKEND_WINDOWS)
		inline constexpr SecureWipeBackend kSecureWipeBackend = SecureWipeBackend::Windows;
		inline void SecureWipeWithSelectedBackend(void* ptr, std::size_t len) noexcept
		{
			::SecureZeroMemory(ptr, len);
		}
#	elif defined(RANDX_DETAIL_SECURE_WIPE_BACKEND_GLIBC)
		inline constexpr SecureWipeBackend kSecureWipeBackend = SecureWipeBackend::Glibc;
		inline void SecureWipeWithSelectedBackend(void* ptr, std::size_t len) noexcept
		{
			::explicit_bzero(ptr, len);
		}
#	elif defined(RANDX_DETAIL_SECURE_WIPE_BACKEND_APPLE)
		inline constexpr SecureWipeBackend kSecureWipeBackend = SecureWipeBackend::Apple;
		inline void SecureWipeWithSelectedBackend(void* ptr, std::size_t len) noexcept
		{
			static_assert(RSIZE_MAX > 0, "RSIZE_MAX must be positive");
			auto* destination = static_cast<unsigned char*>(ptr);
			const std::size_t maximumChunk = static_cast<std::size_t>(RSIZE_MAX);
			while (len > 0)
			{
				const std::size_t chunk = len < maximumChunk ? len : maximumChunk;
				const auto status = ::memset_s(destination,
				                               static_cast<rsize_t>(chunk),
				                               0,
				                               static_cast<rsize_t>(chunk));
				if (status != 0)
					SecureWipePortable(destination, chunk);
				destination += chunk;
				len -= chunk;
			}
		}
#	else
		inline constexpr SecureWipeBackend kSecureWipeBackend = SecureWipeBackend::Portable;
		inline void SecureWipeWithSelectedBackend(void* ptr, std::size_t len) noexcept
		{
			SecureWipePortable(ptr, len);
		}
#	endif

		inline constexpr const char* SecureWipeBackendName() noexcept
		{
			switch (kSecureWipeBackend)
			{
			case SecureWipeBackend::Windows: return "windows";
			case SecureWipeBackend::Glibc: return "glibc";
			case SecureWipeBackend::Apple: return "apple";
			case SecureWipeBackend::Portable: return "portable";
			}
			return "portable";
		}

		inline void SecureWipe(void* ptr, std::size_t len) noexcept
		{
			if (len == 0) return;
			SecureWipeWithSelectedBackend(ptr, len);
		}

#	undef RANDX_DETAIL_SECURE_WIPE_BACKEND_PORTABLE
#	undef RANDX_DETAIL_SECURE_WIPE_BACKEND_WINDOWS
#	undef RANDX_DETAIL_SECURE_WIPE_BACKEND_GLIBC
#	undef RANDX_DETAIL_SECURE_WIPE_BACKEND_APPLE
#	undef RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MAJOR
#	undef RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MINOR
#	undef RANDX_DETAIL_MEMSET_S_MACOS_MINIMUM_VERSION
#	undef RANDX_DETAIL_MEMSET_S_IOS_MINIMUM_VERSION
#	undef RANDX_DETAIL_WINDOWS_SDK_HEADERS_AVAILABLE

		// RAII 敏感内存擦除守卫（覆盖异常展开路径）
		struct ScopedWiper
		{
			void* ptr;
			std::size_t len;
			explicit ScopedWiper(void* p, std::size_t l) noexcept : ptr(p), len(l) {}
			~ScopedWiper() noexcept { SecureWipe(ptr, len); }
			ScopedWiper(const ScopedWiper&) = delete;
			ScopedWiper& operator=(const ScopedWiper&) = delete;
		};

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

		// OS 熵读取的原生状态与平台适配
		enum class EntropyReadStatus
		{
			Progress,
			Interrupted,
			Failure
		};

		struct EntropyReadResult
		{
			EntropyReadStatus status;
			std::size_t bytes;
		};

#	if defined(_WIN32) && __has_include(<bcrypt.h>)
		[[nodiscard]]
		inline EntropyReadResult ConvertWindowsEntropyResult(NTSTATUS status, std::size_t requestedBytes) noexcept
		{
#		if defined(BCRYPT_SUCCESS)
			return BCRYPT_SUCCESS(status)
				? EntropyReadResult{EntropyReadStatus::Progress, requestedBytes}
				: EntropyReadResult{EntropyReadStatus::Failure, 0};
#		else
			return static_cast<NTSTATUS>(status) >= 0
				? EntropyReadResult{EntropyReadStatus::Progress, requestedBytes}
				: EntropyReadResult{EntropyReadStatus::Failure, 0};
#		endif
		}
#	elif defined(__linux__) && __has_include(<sys/random.h>)
		[[nodiscard]]
		inline EntropyReadResult ConvertLinuxEntropyResult(ssize_t result, int error) noexcept
		{
			if (result > 0)
				return {EntropyReadStatus::Progress, static_cast<std::size_t>(result)};
			if (result < 0 && error == EINTR)
				return {EntropyReadStatus::Interrupted, 0};
			return {EntropyReadStatus::Failure, 0};
		}
#	elif defined(__APPLE__) && __has_include(<Security/Security.h>)
		[[nodiscard]]
		inline EntropyReadResult ConvertAppleEntropyResult(int status, std::size_t requestedBytes) noexcept
		{
			return status == errSecSuccess
				? EntropyReadResult{EntropyReadStatus::Progress, requestedBytes}
				: EntropyReadResult{EntropyReadStatus::Failure, 0};
		}
#	endif

		struct NativeOsEntropyReader
		{
			[[nodiscard]]
			std::size_t maxRequestSize() const noexcept
			{
#	if defined(_WIN32) && __has_include(<bcrypt.h>)
				return static_cast<std::size_t>((std::numeric_limits<ULONG>::max)());
#	elif defined(__linux__) && __has_include(<sys/random.h>)
				return static_cast<std::size_t>((std::numeric_limits<ssize_t>::max)());
#	else
				return (std::numeric_limits<std::size_t>::max)();
#	endif
			}

			[[nodiscard]]
			EntropyReadResult read(std::uint8_t* buffer, std::size_t length) noexcept
			{
#	if defined(_WIN32) && __has_include(<bcrypt.h>)
				const ULONG requestSize = static_cast<ULONG>(length);
				const NTSTATUS status = ::BCryptGenRandom(
					nullptr, buffer, requestSize, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
				return ConvertWindowsEntropyResult(status, length);
#	elif defined(__linux__) && __has_include(<sys/random.h>)
				const ssize_t result = ::getrandom(buffer, length, 0);
				const int error = result < 0 ? errno : 0;
				return ConvertLinuxEntropyResult(result, error);
#	elif defined(__APPLE__) && __has_include(<Security/Security.h>)
				const int status = ::SecRandomCopyBytes(kSecRandomDefault, length, buffer);
				return ConvertAppleEntropyResult(status, length);
#	else
				(void)buffer;
				(void)length;
				return {EntropyReadStatus::Failure, 0};
#	endif
			}
		};

#	if defined(RANDX_ENABLE_ENTROPY_TEST_HOOKS)
		struct EntropyTestHook
		{
			bool (*fill)(void* context, void* buffer, std::size_t length) noexcept;
			void* context;
		};

		inline thread_local EntropyTestHook entropyTestHook{};
#	endif

		template <class Reader>
		[[nodiscard]]
		inline bool FillOsEntropy(Reader& reader, void* buffer, std::size_t length) noexcept
		{
			if (length == 0) return true;

			auto* destination = static_cast<std::uint8_t*>(buffer);
			std::size_t filled = 0;
			while (filled < length)
			{
				const std::size_t requestSize = (std::min)(length - filled, reader.maxRequestSize());
				const EntropyReadResult result = reader.read(destination + filled, requestSize);
				if (result.status == EntropyReadStatus::Failure) return false;
				if (result.status == EntropyReadStatus::Interrupted) continue;
				filled += result.bytes;
			}
			return true;
		}

		// 使用原生 OS reader 完整填充请求缓冲区
		[[nodiscard]]
		inline bool GetOsEntropyBytes(void* buf, std::size_t n) noexcept
		{
			if (n == 0) return true;
#	if defined(RANDX_ENABLE_ENTROPY_TEST_HOOKS)
			if (entropyTestHook.fill != nullptr)
				return entropyTestHook.fill(entropyTestHook.context, buf, n);
#	endif
			NativeOsEntropyReader reader;
			return FillOsEntropy(reader, buf, n);
		}

		// 编译期特性检测：检测目标平台与编译器环境是否支持 OS 密码学熵源 API
		// （Windows BCryptGenRandom / Linux getrandom / macOS SecRandomCopyBytes）。
		// 返回 true 表示目标平台支持真 OS 密码学 API；返回 false 表示当前平台缺少 OS 密码学支持。
		// 密码学安全组件（ChaCha20 / SecureRandomBytes）在熵源不可用或获取失败时直接抛异常，绝不降级。
		[[nodiscard]]
		inline constexpr bool HasCryptoGradeOsEntropy() noexcept
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

		template <class E>
		concept RandomEngine =
			std::uniform_random_bit_generator<std::remove_cvref_t<E>> &&
			requires { typename std::remove_cvref_t<E>::result_type; } &&
			std::same_as<typename std::remove_cvref_t<E>::result_type, std::invoke_result_t<std::remove_cvref_t<E>&>> &&
			(!std::same_as<typename std::remove_cvref_t<E>::result_type, bool>);

		template <class E>
		inline constexpr bool is_random_engine_v = RandomEngine<E>;

		template <class Engine>
		inline constexpr bool IsFull64BitEngine = false;

		template <RandomEngine Engine>
		inline constexpr bool IsFull64BitEngine<Engine> =
			(sizeof(typename std::remove_cvref_t<Engine>::result_type) >= sizeof(std::uint64_t) &&
			 static_cast<std::uint64_t>(std::remove_cvref_t<Engine>::min()) == 0ULL &&
			 static_cast<std::uint64_t>(std::remove_cvref_t<Engine>::max()) == (std::numeric_limits<std::uint64_t>::max)());

		template <class Engine>
		inline constexpr bool IsFull32BitEngine = false;

		template <RandomEngine Engine>
		inline constexpr bool IsFull32BitEngine<Engine> =
			(static_cast<std::uint64_t>(std::remove_cvref_t<Engine>::min()) == 0ULL &&
			 static_cast<std::uint64_t>(std::remove_cvref_t<Engine>::max()) == 0xFFFFFFFFULL);

		template <class T>
		inline T NormalizeBetaSample(T x, T y)
		{
			if (!std::isfinite(x) || !std::isfinite(y) || x < T{0} || y < T{0})
				throw std::domain_error("RandBeta: Gamma sample is not finite or negative");
			if (x == T{0} && y == T{0})
				throw std::domain_error("RandBeta: both Gamma samples are zero");
			if (x == T{0}) return T{0};
			if (y == T{0}) return T{1};
			const T sum = x + y;
			if (std::isfinite(sum))
			{
				return x / sum;
			}
			if (x >= y && x > T{0})
			{
				return T{1} / (T{1} + (y / x));
			}
			else if (x < y && y > T{0})
			{
				const T r = x / y;
				return r / (T{1} + r);
			}
			throw std::domain_error("RandBeta: unable to normalize Gamma samples");
		}

		template <RandomEngine Engine>
		[[nodiscard]]
		inline std::uint64_t Generate64Bits(Engine& engine)
		{
			if constexpr (IsFull64BitEngine<Engine>)
			{
				return static_cast<std::uint64_t>(engine());
			}
			else if constexpr (IsFull32BitEngine<Engine>)
			{
				const std::uint64_t lo = static_cast<std::uint64_t>(engine());
				const std::uint64_t hi = static_cast<std::uint64_t>(engine());
				return (hi << 32) | lo;
			}
			else
			{
				std::uniform_int_distribution<std::uint64_t> dist(0, (std::numeric_limits<std::uint64_t>::max)());
				return dist(engine);
			}
		}

	// 字符类型 concept（char/wchar_t/char8_t/char16_t/char32_t）
	// char8_t 仅在 C++20+ 编译器下为基本类型，用特性检测宏条件启用
	template <class T>
	concept Character =
		std::same_as<T, char> ||
		std::same_as<T, wchar_t> ||
		std::same_as<T, char16_t> ||
		std::same_as<T, char32_t>
#if defined(__cpp_char8_t) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
		|| std::same_as<T, char8_t>
#endif
		;

	// 检测 state_type 是否为可索引容器（排除标量如 SplitMix64 的 uint64_t）
	template <class S>
	concept IndexableState = requires(const S& cs, S& s) {
		{ cs.size() } -> std::same_as<std::size_t>;
		{ s[std::size_t{}] } -> std::same_as<typename S::value_type&>;
	};

	// 可序列化引擎 concept（仅对 state_type 为容器类的引擎生效）
	template <class E>
	concept SerializableEngine = requires(const E& ce, E& e) {
		{ ce.serialize() } -> std::same_as<typename E::state_type>;
		{ e.deserialize(std::declval<typename E::state_type>()) } -> std::same_as<void>;
		typename E::state_type;
		requires IndexableState<typename E::state_type>;
	};

	/// @brief 可跳跃引擎概念：支持 jump() 前进 2^N 步
	///
	/// @details 满足此概念的引擎可通过 jump() 创建并行不重叠子序列。
	/// 当前满足：Xoshiro256StarStar, Xoroshiro128StarStar, Xoshiro128StarStar。
	/// 不满足：SplitMix64, SFC64, RomuDuoJr, Xoroshiro64StarStar, ChaCha20。
	///
	/// @note ChaCha20 不提供 jump（CSPRNG 安全约束，状态导出违背前向安全模型）。
	/// @sa StreamEngine, MakeStreamEngine
	template <class E>
	concept JumpableEngine = requires(E& e) {
		{ e.jump() } -> std::same_as<void>;
	};

	/// @brief 流式引擎概念：可通过 MakeStreamEngine 创建互不重叠的子序列流
	///
	/// @details 当前跳跃流需支持 jump()，并可由 uint64_t 种子直接列表构造。
	/// JumpableEngine 描述跳跃能力，StreamEngine 描述 MakeStreamEngine 所需的完整能力。
	/// 未来 counter-based 引擎（如 Philox）可通过 counter 偏移创建流，届时可扩展流引擎约束。
	///
	/// @sa MakeStreamEngine, JumpableEngine
	template <class E>
	concept StreamEngine = JumpableEngine<E> && requires(std::uint64_t seed) {
		E{ seed };
	} && (std::is_constructible_v<E, E&&> || std::is_constructible_v<E, E&>);

	// 迭代器可填充约束（RandFill 用）
	template <class It, class T>
	concept RandFillable = std::output_iterator<It, T>
		&& (std::integral<T> || std::floating_point<T>);

		template <class CharT, class Traits>
		class StreamFormatGuard
		{
		public:
			using StreamType = std::basic_ios<CharT, Traits>;

			explicit StreamFormatGuard(StreamType& stream)
				: m_stream(stream),
				  m_flags(stream.flags()),
				  m_fill(stream.fill()),
				  m_width(stream.width())
			{
			}

			~StreamFormatGuard()
			{
				m_stream.flags(m_flags);
				m_stream.fill(m_fill);
				m_stream.width(m_width);
			}

			StreamFormatGuard(const StreamFormatGuard&) = delete;
			StreamFormatGuard& operator=(const StreamFormatGuard&) = delete;

		private:
			StreamType& m_stream;
			typename StreamType::fmtflags m_flags;
			typename StreamType::char_type m_fill;
			std::streamsize m_width;
		};
	}

	using detail::RandomEngine;
	using detail::is_random_engine_v;

	// ========================================================================
	// 流式运算符 operator<< / operator>>
	// 仅对 state_type 为可索引容器类的引擎生效（SerializableEngine concept）
	// SplitMix64（state_type = uint64_t 标量）不支持，由 IndexableState 排除
	// 格式兼容 std::random_engine：空格分隔的十进制数序列
	// ========================================================================

	// 流式输出引擎状态
	template <class CharT, class Traits, detail::SerializableEngine Engine>
	std::basic_ostream<CharT, Traits>&
	operator<<(std::basic_ostream<CharT, Traits>& os, const Engine& engine)
	{
		typename std::basic_ostream<CharT, Traits>::sentry ok(os);
		if (!ok) return os;

		detail::StreamFormatGuard<CharT, Traits> guard(os);

		os.setf(std::ios_base::dec, std::ios_base::basefield);
		os.setf(std::ios_base::left, std::ios_base::adjustfield);
		os.fill(os.widen(' '));
		os.width(0);

		const auto space = os.widen(' ');
		const auto state = engine.serialize();
		for (std::size_t i = 0; i < state.size(); ++i)
		{
			if (i != 0) os << space;
			os << state[i];
		}

		return os;
	}

	// 流式恢复引擎状态
	// 若解析失败（读取不足、流错误或状态非法/全零），setstate(failbit) 且引擎状态保持不变
	// （与 std::random_engine 一致：先读取到临时 state，全部成功才 deserialize）
	template <class CharT, class Traits, detail::SerializableEngine Engine>
	std::basic_istream<CharT, Traits>&
	operator>>(std::basic_istream<CharT, Traits>& is, Engine& engine)
	{
		typename std::basic_istream<CharT, Traits>::sentry ok(is);
		if (!ok) return is;

		detail::StreamFormatGuard<CharT, Traits> guard(is);

		is.setf(std::ios_base::dec, std::ios_base::basefield);
		is.setf(std::ios_base::skipws);
		is.width(0);

		typename Engine::state_type state{};
		std::size_t i = 0;
		for (; i < state.size() && (is >> state[i]); ++i)
		{
		}

		if (i == state.size() && detail::IsValidSnapshot<Engine>(state))
		{
			engine.deserialize(state);
		}
		else
		{
			is.setstate(std::ios_base::failbit);
		}

		return is;
	}

	////////////////////////////////////////////////////////////////
	//
	//	SplitMix64
	//
	inline constexpr SplitMix64::SplitMix64(const state_type state) noexcept
		: m_state(state) {}

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
		m_state += static_cast<std::uint64_t>(n) * 0x9e3779b97f4a7c15ULL;
	}

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro256**
	//
	inline constexpr Xoshiro256StarStar::result_type Xoshiro256StarStar::operator()() noexcept
	{
		auto* state = s_.data();
		const std::uint64_t result = detail::RotL(state[1] * 5, 7) * 9;
		const std::uint64_t t = state[1] << 17;
		state[2] ^= state[0];
		state[3] ^= state[1];
		state[1] ^= state[2];
		state[0] ^= state[3];
		state[2] ^= t;
		state[3] = detail::RotL(state[3], 45);
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
		auto* state = s_.data();
		const std::uint64_t s0 = state[0];
		std::uint64_t s1 = state[1];
		const std::uint64_t result = detail::RotL(s0 * 5, 7) * 9;
		s1 ^= s0;
		state[0] = detail::RotL(s0, 24) ^ s1 ^ (s1 << 16);
		state[1] = detail::RotL(s1, 37);
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
		auto* state = s_.data();
		const std::uint32_t result = detail::RotL(state[1] * 5, 7) * 9;
		const std::uint32_t t = state[1] << 9;
		state[2] ^= state[0];
		state[3] ^= state[1];
		state[1] ^= state[2];
		state[0] ^= state[3];
		state[2] ^= t;
		state[3] = detail::RotL(state[3], 11);
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
	//	便捷工具函数
	//

	namespace detail
	{
		struct NativeRandomSeedSources
		{
			[[nodiscard]]
			bool TryHardware(std::uint64_t& out) noexcept
			{
				return HardwareRand64(out);
			}

			[[nodiscard]]
			bool TryOs(std::uint64_t& out) noexcept
			{
				return GetOsEntropyBytes(&out, sizeof(out));
			}

			[[nodiscard]]
			std::uint64_t ReadRandomDevice()
			{
				std::random_device rd;
				constexpr int wordBits = std::numeric_limits<std::uint32_t>::digits;
				return (static_cast<std::uint64_t>(rd()) << wordBits) | rd();
			}

			[[nodiscard]]
			std::uint64_t Fallback() noexcept
			{
				const auto t1 = std::chrono::high_resolution_clock::now().time_since_epoch().count();
				const auto t2 = std::chrono::steady_clock::now().time_since_epoch().count();
				const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
				static std::atomic<std::uint64_t> counter{0};
				std::uint64_t stackVar = 0;
				const std::uint64_t addr = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&stackVar));

				const std::uint64_t rawSeed = static_cast<std::uint64_t>(t1) ^ static_cast<std::uint64_t>(t2)
				                              ^ threadId ^ addr ^ counter.fetch_add(1, std::memory_order_relaxed);
				SplitMix64 sm{ rawSeed };
				return sm();
			}
		};

		template <class Sources>
		[[nodiscard]]
		inline std::uint64_t RandomSeedWithSources(Sources& sources)
		{
			std::uint64_t seed;
			if (sources.TryHardware(seed))
				return seed;
			if (sources.TryOs(seed))
				return seed;
			try
			{
				return sources.ReadRandomDevice();
			}
			catch (...)
			{
				return sources.Fallback();
			}
		}
	}

	/// @brief 生成非确定性的 64 位种子
	/// @return 优先硬件 RNG 的种子值
	/// @note 优先级链：RDRAND (x86_64) → detail::GetOsEntropyBytes → std::random_device → 时间戳回退
	[[nodiscard]]
	// 平台调用与异常回退独立编译，限制线程局部初始化对常用随机接口的展开规模。
#if defined(_MSC_VER)
	__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
	__attribute__((noinline))
#endif
	inline std::uint64_t RandomSeed()
	{
		detail::NativeRandomSeedSources sources;
		return detail::RandomSeedWithSources(sources);
	}

	namespace detail
	{
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		inline Xoshiro256StarStar CreateDefaultEngine()
		{
			return Xoshiro256StarStar{ RandomSeed() };
		}
	}

	/// @brief 获取当前线程专属的默认伪随机数生成引擎
	/// @return 线程局部 Xoshiro256StarStar 引擎的左值引用
	/// @warning 返回引用的生命周期严格绑定于当前线程的线程局部存储（TLS），
	///          严禁跨线程转移引用、捕获引用传递给异地异步任务（如 std::async、线程池）。
	[[nodiscard]]
	inline Xoshiro256StarStar& DefaultEngine()
	{
		thread_local Xoshiro256StarStar engine = detail::CreateDefaultEngine();
		return engine;
	}

	/// @brief 重新播种当前线程的默认引擎（用于 POSIX fork() 产生子进程后重置引擎状态）
	inline void ResetThreadLocalEngine()
	{
		DefaultEngine() = Xoshiro256StarStar{ RandomSeed() };
	}

	/// @}

	/// @defgroup csprng 密码学安全
	/// @brief ChaCha20 CSPRNG 与 OS 熵源 API
	/// @{

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

	/// @brief 编译期特性检测：检测当前平台是否支持 OS 密码学熵源 API
	/// @return true 表示支持真 OS 密码学 API（BCryptGenRandom/getrandom/SecRandomCopyBytes）；
	///         false 表示缺少 OS 密码学支持。密码学安全组件在熵源失败时直接抛异常，绝不隐式降级。
	[[nodiscard]]
	inline constexpr bool IsOsCryptoEntropyAvailable() noexcept
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
		  m_autoReseed(other.m_autoReseed),
		  m_counterExhausted(other.m_counterExhausted),
		  m_movedFrom(other.m_movedFrom)
	{
		detail::SecureWipe(other.m_state.data(), sizeof(other.m_state));
		detail::SecureWipe(other.m_buffer.data(), sizeof(other.m_buffer));
		other.m_movedFrom = true;
		other.m_bufferPos = 64;
		other.m_bytesSinceReseed = 0;
		other.m_counterExhausted = false;
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
			m_counterExhausted = other.m_counterExhausted;
			m_movedFrom = other.m_movedFrom;

			detail::SecureWipe(other.m_state.data(), sizeof(other.m_state));
			detail::SecureWipe(other.m_buffer.data(), sizeof(other.m_buffer));
			other.m_movedFrom = true;
			other.m_bufferPos = 64;
			other.m_bytesSinceReseed = 0;
			other.m_counterExhausted = false;
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
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(true),
		  m_counterExhausted(false), m_movedFrom(false)
	{
		reseed();  // 从 OS 熵获取 key + nonce，重置 counter
	}

	// 构造方式 2：显式种子（仅测试/复现，非密码学安全）
	// 用 SplitMix64 将 64-bit 种子扩展为 32 字节 key + 12 字节 nonce
	inline ChaCha20::ChaCha20(const std::uint64_t seed)
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(false),
		  m_counterExhausted(false), m_movedFrom(false)
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
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(false),
		  m_counterExhausted(false), m_movedFrom(false)
	{
		if (!key || keyLen != 32 || !nonce || nonceLen != 12)
			throw std::invalid_argument("ChaCha20: invalid key or nonce");
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
		if (m_counterExhausted)
		{
			throw std::overflow_error("ChaCha20: 32-bit block counter overflow");
		}

		// 构造完整 16-word 状态：常数 + key + counter + nonce
		std::array<std::uint32_t, 16> state{};
		detail::ScopedWiper stateWiper(state.data(), sizeof(state));
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
		detail::ScopedWiper workingWiper(working.data(), sizeof(working));

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

		if (m_state[8] == 0xFFFFFFFFU)
		{
			m_counterExhausted = true;
		}
		else
		{
			++m_state[8];
		}
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
		std::array<std::uint8_t, 44> seed{};  // 32(key) + 12(nonce)
		detail::ScopedWiper wiper(seed.data(), seed.size());
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
		m_counterExhausted = false;
		m_movedFrom = false;
		detail::SecureWipe(m_buffer.data(), m_buffer.size()); // 擦除旧 keystream
	}

	// 生成一个 64-bit 随机数（从缓存取 8 字节，缓存耗尽时生成新 block）
	inline ChaCha20::result_type ChaCha20::operator()()
	{
		if (m_movedFrom)
			throw std::logic_error("ChaCha20: generator is in moved-from state");
		reseedIfNecessary();
		if (m_bufferPos == 64)
		{
			if (m_counterExhausted)
				throw std::overflow_error("ChaCha20: 32-bit block counter overflow");
			generateBlock();
		}
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
		if (m_movedFrom)
			throw std::logic_error("ChaCha20: generator is in moved-from state");
		for (unsigned long long i = 0; i < n; ++i) operator()();
	}

	/// @brief 重置默认引擎的种子（用于测试复现）
	/// @param seed 新的种子值
	inline void Reseed(std::uint64_t seed)
	{
		DefaultEngine() = Xoshiro256StarStar{ seed };
	}

	/// @brief 重置默认引擎为真随机种子
	inline void ReseedRandom()
	{
		DefaultEngine() = Xoshiro256StarStar{ RandomSeed() };
	}

	/// @}

	/// @defgroup basic 基础生成
	/// @brief RandInt / RandReal / RandBool / RandChar / RandBits
	/// @{

	/// @brief 生成 [min, max] 范围内的随机整数
	/// @param min 下界（含）
	/// @param max 上界（含）
	/// @return 均匀分布于 [min, max] 的随机整数
	template <std::integral T = int>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	[[nodiscard]]
	inline T RandInt(T min, T max)
	{
		return RandInt(DefaultEngine(), min, max);
	}

	/// @brief 生成 [0, max] 范围内的随机整数
	/// @param max 上界（含）
	/// @return 均匀分布于 [0, max] 的随机整数
	template <std::integral T = int>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	[[nodiscard]]
	inline T RandInt(T max)
	{
		return RandInt<T>(T{0}, max);
	}

	/// @brief 生成 [min, max) 范围内的随机浮点数
	/// @param min 下界（含，默认 0）
	/// @param max 上界（不含，默认 1）
	/// @return 均匀分布于 [min, max) 的随机浮点数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandReal(T min = T{0}, T max = T{1})
	{
		return RandReal(DefaultEngine(), min, max);
	}

	/// @brief 采用无偏 Bit-Extraction 直通算法生成 [0, 1) 半开区间的随机浮点数（默认线程引擎）
	/// @tparam T 浮点数类型（float / double）
	/// @return [0, 1) 范围内的无偏伪随机浮点数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandCanonical()
	{
		return RandCanonical<T>(DefaultEngine());
	}

	/// @brief 生成 [0.0, 1.0) 半开区间的双精度浮点数（直通 Bit-Extraction 极速 API）
	[[nodiscard]]
	inline double RandCanonicalDouble()
	{
		return RandCanonical<double>();
	}

	/// @brief 生成 [0.0f, 1.0f) 半开区间的单精度浮点数（直通 Bit-Extraction 极速 API）
	[[nodiscard]]
	inline float RandCanonicalFloat()
	{
		return RandCanonical<float>();
	}

	/// @brief 生成随机布尔值
	/// @param p 为 true 的概率（默认 0.5）
	/// @return 以概率 p 返回 true
	[[nodiscard]]
	inline bool RandBool(double p = 0.5)
	{
		if (!std::isfinite(p) || p < 0.0 || p > 1.0)
			throw std::invalid_argument("RandBool: invalid probability p");
		std::bernoulli_distribution dist(p);
		return dist(DefaultEngine());
	}

	/// @brief 生成随机布尔值（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 为 true 的概率（默认 0.5）
	/// @return 以概率 p 返回 true
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline bool RandBool(Engine& engine, double p = 0.5)
	{
		if (!std::isfinite(p) || p < 0.0 || p > 1.0)
			throw std::invalid_argument("RandBool: invalid probability p");
		std::bernoulli_distribution dist(p);
		return dist(engine);
	}

	/// @brief 伯努利分布（RandBool 的别名封装，对齐 \<random\> 命名）
	/// @param p 成功概率（默认 0.5）
	/// @return 以概率 p 返回 true
	[[nodiscard]]
	inline bool RandBernoulli(double p = 0.5)
	{
		return RandBool(p);
	}

	/// @brief 伯努利分布（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 成功概率（默认 0.5）
	/// @return 以概率 p 返回 true
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline bool RandBernoulli(Engine& engine, double p = 0.5)
	{
		return RandBool(engine, p);
	}

	/// @brief 生成 [min, max] 范围内的随机字符
	/// @param min 下界字符（含）
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [min, max] 的随机字符
	/// @note 内部用 int64_t 避免 char32_t 范围（最大 0xFFFFFFFF）溢出 int32_t
	template <detail::Character CharT>
	[[nodiscard]]
	inline CharT RandChar(CharT min, CharT max)
	{
		if (min > max)
			throw std::invalid_argument("RandChar: min > max");
		using IntT = std::int64_t;
		std::uniform_int_distribution<IntT> dist(
			static_cast<IntT>(min), static_cast<IntT>(max));
		return static_cast<CharT>(dist(DefaultEngine()));
	}

	/// @brief 生成 [CharT{}, max] 范围内的随机字符
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [CharT{}, max] 的随机字符
	template <detail::Character CharT = char>
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
	template <detail::Character CharT, detail::RandomEngine Engine>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT min, CharT max)
	{
		if (min > max)
			throw std::invalid_argument("RandChar: min > max");
		using IntT = std::int64_t;
		std::uniform_int_distribution<IntT> dist(
			static_cast<IntT>(min), static_cast<IntT>(max));
		return static_cast<CharT>(dist(engine));
	}

	/// @brief 生成 [CharT{}, max] 范围内的随机字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [CharT{}, max] 的随机字符
	template <detail::Character CharT = char, detail::RandomEngine Engine>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT max)
	{
		return RandChar<CharT>(engine, CharT{}, max);
	}

	/// @}

	namespace detail
	{
		template <class T, class Diff, class It, class Sentinel, class GetEngine>
		inline T
		RandElementInput(It& first, Sentinel& last, GetEngine&& getEngine)
		{
			using Count = std::uint64_t;
			constexpr Count maxCount = (std::numeric_limits<Count>::max)();

			if (first == last)
				throw std::invalid_argument("RandElement: empty range");
			T selected = *first;
			++first;
			Count count = 1;
			while (first != last)
			{
				if (count == maxCount)
					throw std::length_error("RandElement: range exceeds count capacity");

				auto& engine = getEngine();
				Count index;
				if constexpr (std::numeric_limits<Diff>::digits <= std::numeric_limits<Count>::digits)
				{
					if (count <= static_cast<Count>((std::numeric_limits<Diff>::max)()))
					{
						const Diff upper = static_cast<Diff>(count);
						index = static_cast<Count>(RandInt<Diff>(engine, Diff{0}, upper));
					}
					else
					{
						index = RandInt<Count>(engine, Count{0}, count);
					}
				}
				else
				{
					const Diff upper = static_cast<Diff>(count);
					index = static_cast<Count>(RandInt<Diff>(engine, Diff{0}, upper));
				}
				if (index == 0)
					selected = *first;
				++first;
				++count;
			}
			if constexpr (std::is_constructible_v<T, T&&>)
				return selected;
			else
				return static_cast<const T&>(selected);
		}
	}
	/// @defgroup containers 容器操作
	/// @brief RandElement / RandSample / RandShuffle / RandPermutation / RandFill / RandVector
	/// @{

	/// @brief 从容器中随机取一个元素（左值容器，返回引用）
	/// @param c 源容器（需支持 operator[] 和 size()）
	/// @return 容器中随机选取的一个元素的引用
	/// @throw std::invalid_argument 容器为空时抛出
	template <class Container>
		requires std::ranges::random_access_range<Container>
	[[nodiscard]]
	inline decltype(auto) RandElement(Container& c)
	{
		if (std::size(c) == 0)
			throw std::invalid_argument("RandElement: empty container");
		return c[RandInt<std::size_t>(static_cast<std::size_t>(std::size(c) - 1))];
	}

	/// @brief 从容器中随机取一个元素（右值容器，按值返回以避免悬垂引用）
	/// @param c 源容器（需支持 operator[] 和 size()）
	/// @return 容器中随机选取的一个元素的副本
	/// @throw std::invalid_argument 容器为空时抛出
	template <class Container>
		requires std::ranges::random_access_range<Container>
	[[nodiscard]]
	inline std::ranges::range_value_t<Container> RandElement(Container&& c)
	{
		if (std::size(c) == 0)
			throw std::invalid_argument("RandElement: empty container");
		return c[RandInt<std::size_t>(static_cast<std::size_t>(std::size(c) - 1))];
	}

	/// @brief 从迭代器范围内随机取一个元素（随机访问迭代器：O(1) 直接定位）
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器
	/// @return 指向随机选取元素的迭代器
	/// @throw std::invalid_argument 范围为空时抛出
	template <std::random_access_iterator It, std::sized_sentinel_for<It> Sentinel>
	[[nodiscard]]
	inline It RandElement(It first, Sentinel last)
	{
		using Diff = std::iter_difference_t<It>;
		const Diff n = static_cast<Diff>(last - first);
		if (n <= 0)
			throw std::invalid_argument("RandElement: empty range");
		return std::next(first, RandInt<Diff>(Diff{0}, n - 1));
	}

	/// @brief 从迭代器范围内随机取一个元素（输入迭代器：O(n) reservoir sampling）
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @return 随机选取的元素值
	/// @throw std::invalid_argument 范围为空时抛出
	template <std::input_iterator It, std::sentinel_for<It> Sentinel>
		requires (!std::random_access_iterator<It> || !std::sized_sentinel_for<Sentinel, It>)
			&& std::is_copy_constructible_v<std::iter_value_t<It>>
			&& std::is_copy_assignable_v<std::iter_value_t<It>>
	[[nodiscard]]
	inline std::iter_value_t<It> RandElement(It first, Sentinel last)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		return detail::RandElementInput<T, Diff>(first, last,
			[]() -> Xoshiro256StarStar& { return DefaultEngine(); });
	}

	/// @brief 从迭代器范围内随机取一个元素（指定引擎，随机访问迭代器）
	/// @param engine 自定义随机数引擎
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @return 指向随机选取元素的迭代器
	template <std::random_access_iterator It, std::sized_sentinel_for<It> Sentinel, detail::RandomEngine Engine>
	[[nodiscard]]
	inline It RandElement(Engine& engine, It first, Sentinel last)
	{
		using Diff = std::iter_difference_t<It>;
		const Diff n = static_cast<Diff>(last - first);
		if (n <= 0)
			throw std::invalid_argument("RandElement: empty range");
		return std::next(first, RandInt<Diff>(engine, Diff{0}, n - 1));
	}

	/// @brief 从迭代器范围内随机取一个元素（指定引擎，输入迭代器）
	/// @param engine 自定义随机数引擎
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @return 随机选取的元素值
	template <std::input_iterator It, std::sentinel_for<It> Sentinel, detail::RandomEngine Engine>
		requires (!std::random_access_iterator<It> || !std::sized_sentinel_for<Sentinel, It>)
			&& std::is_copy_constructible_v<std::iter_value_t<It>>
			&& std::is_copy_assignable_v<std::iter_value_t<It>>
	[[nodiscard]]
	inline std::iter_value_t<It> RandElement(Engine& engine, It first, Sentinel last)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		return detail::RandElementInput<T, Diff>(first, last,
			[&engine]() -> Engine& { return engine; });
	}


	/// @}

	/// @defgroup distributions 统计分布
	/// @brief 16 种标准统计分布便捷函数
	/// @{

	/// @brief 生成正态分布随机数
	/// @param mean 均值（默认 0）
	/// @param stddev 标准差（默认 1）
	/// @return 服从 N(mean, stddev) 的随机数
	template <std::floating_point T = double>
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
	template <detail::RandomEngine Engine, std::floating_point T = double>
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
	template <std::ranges::random_access_range Container>
		requires std::permutable<std::ranges::iterator_t<Container>>
	inline void RandShuffle(Container&& c)
	{
		std::ranges::shuffle(c, DefaultEngine());
	}

	/// @brief 用 [min, max] 范围的随机整数填充迭代器区间
	/// @param first 起始迭代器
	/// @param last 结束迭代器
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @note T 从 min/max 推导，不从迭代器 value_type 推导。
	///       若容器元素类型与 min/max 字面量类型不一致，需显式指定 T 或用匹配类型的字面量。
	template <class It, class Sentinel, class T>
		requires detail::RandFillable<It, T> && std::integral<T> && (!std::same_as<std::remove_cv_t<T>, bool>) && std::sentinel_for<Sentinel, It>
	inline void RandFill(It first, Sentinel last, T min, T max)
	{
		if (min > max)
			throw std::invalid_argument("RandFill: min > max");
		using DistType = std::conditional_t<(sizeof(T) < sizeof(short)),
			std::conditional_t<std::is_signed_v<T>, int, unsigned int>, T>;
		std::uniform_int_distribution<DistType> dist(static_cast<DistType>(min), static_cast<DistType>(max));
		for (; first != last; ++first)
			*first = static_cast<T>(dist(DefaultEngine()));
	}

	namespace detail
	{
		template <class T>
		class RealIntervalKernel
		{
		public:
			RealIntervalKernel(T min, T max, const char* funcName = "RandReal")
				: m_min(min), m_max(max), m_isDegenerate(min == max)
			{
				if (!std::isfinite(min) || !std::isfinite(max) || min > max)
				{
					throw std::invalid_argument(std::string(funcName) + ": invalid min or max");
				}
				if (m_isDegenerate)
				{
					return;
				}
				if (min < 0 && max > 0 && (max > std::numeric_limits<T>::max() + min))
				{
					throw std::invalid_argument(std::string(funcName) + ": range width overflow");
				}
				const T width = max - min;
				if (!std::isfinite(width))
				{
					throw std::invalid_argument(std::string(funcName) + ": range width overflow");
				}
				m_upperCorrected = std::nextafter(max, min);
				m_dist.param(typename std::uniform_real_distribution<T>::param_type(min, max));
			}

			[[nodiscard]]
			bool IsDegenerate() const noexcept { return m_isDegenerate; }

			[[nodiscard]]
			T DegenerateValue() const noexcept { return m_min; }

			template <class Engine>
			[[nodiscard]]
			T Generate(Engine& engine)
			{
				if (m_isDegenerate)
				{
					return m_min;
				}
				T val = m_dist(engine);
				if (!std::isfinite(val) || val < m_min || val > m_max)
				{
					throw std::runtime_error("RealIntervalKernel: numerical failure in distribution generation");
				}
				if (val >= m_max)
				{
					val = m_upperCorrected;
				}
				return val;
			}

		private:
			T m_min;
			T m_max;
			T m_upperCorrected{0};
			bool m_isDegenerate;
			std::uniform_real_distribution<T> m_dist;
		};
		template <class T>
		using StableDistributionWorkType = std::conditional_t<std::is_same_v<T, float>, double, T>;

		template <class WorkT>
		struct NormalizedGammaLogSample
		{
			WorkT base_log{0};
			WorkT exponential{0};
			WorkT degrees{1};
		};

		template <class WorkT>
		struct DecomposedGammaLog;

		template <class WorkT, class Engine>
		inline void SampleGammaLogScale(Engine& engine, WorkT shape, DecomposedGammaLog<WorkT>& out);

		template <class WorkT, class Engine>
		inline NormalizedGammaLogSample<WorkT> SampleNormalizedGammaLog(Engine& engine, WorkT degrees)
		{
			const WorkT shape = degrees / WorkT{2};
			const WorkT log_shape = shape > WorkT{0} ? std::log(shape) : std::log(degrees) - std::log(WorkT{2});
			DecomposedGammaLog<WorkT> gamma_sample;
			if (shape > WorkT{0})
			{
				SampleGammaLogScale(engine, shape, gamma_sample);
			}
			else
			{
				// 最小次正规自由度的一半不可表示；提升后的 Gamma 形状舍入为 1，原形状保留在对数域。
				std::exponential_distribution<WorkT> exponential(WorkT{1});
				const WorkT correction = exponential(engine);
				SampleGammaLogScale(engine, WorkT{1}, gamma_sample);
				gamma_sample.exp_val = correction;
				gamma_sample.has_extra = true;
			}

			NormalizedGammaLogSample<WorkT> sample;
			sample.base_log = gamma_sample.base_log - log_shape;
			sample.exponential = gamma_sample.has_extra ? gamma_sample.exp_val : WorkT{0};
			sample.degrees = degrees;
			return sample;
		}

		template <class WorkT>
		inline WorkT ComputeNormalizedGammaLogRatio(
			const NormalizedGammaLogSample<WorkT>& numerator,
			const NormalizedGammaLogSample<WorkT>& denominator)
		{
			const WorkT base_diff = numerator.base_log - denominator.base_log;
			// 先在共同尺度上相减，再除以较小自由度，保留相近补偿项的差并避免无穷相减。
			WorkT correction;
			if (numerator.degrees <= denominator.degrees)
			{
				const WorkT ratio = numerator.degrees / denominator.degrees;
				correction = (ratio * denominator.exponential - numerator.exponential) / numerator.degrees;
			}
			else
			{
				const WorkT ratio = denominator.degrees / numerator.degrees;
				correction = (denominator.exponential - ratio * numerator.exponential) / denominator.degrees;
			}
			return base_diff + WorkT{2} * correction;
		}

		template <class WorkT>
		inline WorkT ComputeNormalizedGammaLogValue(const NormalizedGammaLogSample<WorkT>& sample)
		{
			return sample.base_log - WorkT{2} * (sample.exponential / sample.degrees);
		}

		template <class T, class WorkT>
		inline T ConvertSignedLogMagnitude(WorkT log_magnitude, WorkT sign_source)
		{
			const T maximum = (std::numeric_limits<T>::max)();
			const WorkT log_maximum = std::log(static_cast<WorkT>(maximum));
			if (log_magnitude > log_maximum)
				return std::copysign((std::numeric_limits<T>::infinity)(), static_cast<T>(sign_source));
			if (log_magnitude == -(std::numeric_limits<WorkT>::infinity)())
				return std::copysign(T{0}, static_cast<T>(sign_source));
			const WorkT magnitude = std::exp(log_magnitude);
			if (magnitude > static_cast<WorkT>(maximum))
				return std::copysign((std::numeric_limits<T>::infinity)(), static_cast<T>(sign_source));
			return std::copysign(static_cast<T>(magnitude), static_cast<T>(sign_source));
		}

		template <class T, class WorkT>
		inline T ExpToPositiveSample(WorkT log_sample)
		{
			const T maximum = (std::numeric_limits<T>::max)();
			const WorkT log_maximum = std::log(static_cast<WorkT>(maximum));
			if (log_sample > log_maximum)
				return (std::numeric_limits<T>::infinity)();

			const WorkT sample = std::exp(log_sample);
			if (sample > static_cast<WorkT>(maximum))
				return (std::numeric_limits<T>::infinity)();
			return static_cast<T>(sample);
		}

		template <class T>
		inline bool RequiresStableStudentT(T degrees)
		{
			using WorkT = StableDistributionWorkType<T>;
			// 大自由度下的指数增量会丢失有效位；小形状 Gamma 的幂变换则可能下溢。
			const WorkT precision_boundary = WorkT{1} / std::sqrt(static_cast<WorkT>(std::numeric_limits<T>::epsilon()));
			const T denorm_min = std::numeric_limits<T>::denorm_min();
			const T smallest_positive = denorm_min > T{0} ? denorm_min : std::numeric_limits<T>::min();
			const WorkT gamma_underflow_boundary = WorkT{2} /
				(-std::log(static_cast<WorkT>(smallest_positive)));
			const WorkT work_degrees = static_cast<WorkT>(degrees);
			return work_degrees <= gamma_underflow_boundary || work_degrees >= precision_boundary;
		}

		template <class T, class Engine>
		inline T SampleStableStudentT(Engine& engine, T degrees)
		{
			using WorkT = StableDistributionWorkType<T>;
			const WorkT work_degrees = static_cast<WorkT>(degrees);
			const NormalizedGammaLogSample<WorkT> gamma = SampleNormalizedGammaLog(engine, work_degrees);
			std::normal_distribution<WorkT> normal(WorkT{0}, WorkT{1});
			const WorkT normal_sample = normal(engine);
			if (normal_sample == WorkT{0})
				return T{0};
			const WorkT log_magnitude = std::log(std::abs(normal_sample)) -
				ComputeNormalizedGammaLogValue(gamma) / WorkT{2};
			return ConvertSignedLogMagnitude<T>(log_magnitude, normal_sample);
		}

		template <class T>
		inline bool RequiresStableFisherF(T m, T n)
		{
			using WorkT = StableDistributionWorkType<T>;
			const WorkT work_m = static_cast<WorkT>(m);
			const WorkT work_n = static_cast<WorkT>(n);
			const WorkT maximum = static_cast<WorkT>((std::numeric_limits<T>::max)());
			const WorkT large_degree_boundary = std::sqrt(maximum);
			const bool degree_scale_risk = work_m >= large_degree_boundary || work_n >= large_degree_boundary;
			const T denorm_min = std::numeric_limits<T>::denorm_min();
			const T smallest_positive = denorm_min > T{0} ? denorm_min : std::numeric_limits<T>::min();
			const WorkT gamma_underflow_boundary = WorkT{2} / (-std::log(static_cast<WorkT>(smallest_positive)));
			return degree_scale_risk || work_m <= gamma_underflow_boundary || work_n <= gamma_underflow_boundary;
		}

		template <class T, class Engine>
		inline T SampleStableFisherF(Engine& engine, T m, T n)
		{
			using WorkT = StableDistributionWorkType<T>;
			const NormalizedGammaLogSample<WorkT> numerator = SampleNormalizedGammaLog(engine, static_cast<WorkT>(m));
			const NormalizedGammaLogSample<WorkT> denominator = SampleNormalizedGammaLog(engine, static_cast<WorkT>(n));
			const WorkT log_ratio = ComputeNormalizedGammaLogRatio(numerator, denominator);
			return ExpToPositiveSample<T>(log_ratio);
		}
	}

	/// @brief 用 [min, max) 范围的随机浮点数填充迭代器区间
	/// @param first 起始迭代器
	/// @param last 结束迭代器/哨兵
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	template <class It, class Sentinel, std::floating_point T>
		requires std::output_iterator<It, T> && std::sentinel_for<Sentinel, It>
	inline void RandFill(It first, Sentinel last, T min, T max)
	{
		RandFill(DefaultEngine(), first, last, min, max);
	}

	/// @brief 用 [min, max] 范围的随机整数填充迭代器区间（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param first 起始迭代器
	/// @param last 结束迭代器/哨兵
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	template <class It, class Sentinel, class T, detail::RandomEngine Engine>
		requires detail::RandFillable<It, T> && std::integral<T> && (!std::same_as<std::remove_cv_t<T>, bool>) && std::sentinel_for<Sentinel, It>
	inline void RandFill(Engine& engine, It first, Sentinel last, T min, T max)
	{
		if (min > max)
			throw std::invalid_argument("RandFill: min > max");
		using DistType = std::conditional_t<(sizeof(T) < sizeof(short)),
			std::conditional_t<std::is_signed_v<T>, int, unsigned int>, T>;
		std::uniform_int_distribution<DistType> dist(static_cast<DistType>(min), static_cast<DistType>(max));
		for (; first != last; ++first)
			*first = static_cast<T>(dist(engine));
	}

	/// @brief 用 [min, max) 范围的随机浮点数填充迭代器区间（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param first 起始迭代器
	/// @param last 结束迭代器/哨兵
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	template <class It, class Sentinel, std::floating_point T, detail::RandomEngine Engine>
		requires std::output_iterator<It, T> && std::sentinel_for<Sentinel, It>
	inline void RandFill(Engine& engine, It first, Sentinel last, T min, T max)
	{
		detail::RealIntervalKernel<T> kernel(min, max, "RandFill");
		if (kernel.IsDegenerate())
		{
			const T val = kernel.DegenerateValue();
			for (; first != last; ++first)
				*first = val;
			return;
		}
		for (; first != last; ++first)
			*first = kernel.Generate(engine);
	}

	/// @brief 生成含 n 个随机整数的 vector
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max] 的随机整数 vector
	template <std::integral T = int>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
	{
		return RandVector(DefaultEngine(), min, max, n);
	}

	/// @brief 生成含 n 个随机浮点数的 vector
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max) 的随机浮点数 vector
	template <std::floating_point T = double>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
	{
		return RandVector(DefaultEngine(), min, max, n);
	}

	/// @brief 生成含 n 个随机整数的 vector（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max] 的随机整数 vector
	template <std::integral T = int, detail::RandomEngine Engine>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	[[nodiscard]]
	inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n)
	{
		if (min > max)
			throw std::invalid_argument("RandVector: min > max");
		std::vector<T> v;
		v.reserve(n);
		using DistType = std::conditional_t<(sizeof(T) < sizeof(short)),
			std::conditional_t<std::is_signed_v<T>, int, unsigned int>, T>;
		std::uniform_int_distribution<DistType> dist(static_cast<DistType>(min), static_cast<DistType>(max));
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(static_cast<T>(dist(engine)));
		return v;
	}

	/// @brief 生成含 n 个随机浮点数的 vector（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max) 的随机浮点数 vector
	template <std::floating_point T = double, detail::RandomEngine Engine>
	[[nodiscard]]
	inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n)
	{
		detail::RealIntervalKernel<T> kernel(min, max, "RandVector");
		std::vector<T> v;
		v.reserve(n);
		if (kernel.IsDegenerate())
		{
			v.assign(n, kernel.DegenerateValue());
			return v;
		}
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(kernel.Generate(engine));
		return v;
	}

	namespace detail
	{
		template <class WeightContainer>
		struct PreparedWeights
		{
			using Size = typename WeightContainer::size_type;
			std::vector<Size> active_indices;
			std::vector<double> normalized_weights;
		};

		template <class WeightContainer>
		[[nodiscard]]
		inline PreparedWeights<WeightContainer> PrepareWeights(const WeightContainer& weights)
		{
			if (weights.empty())
			{
				throw std::invalid_argument("RandWeighted: weights container cannot be empty");
			}

			using RawWeightType = typename WeightContainer::value_type;
			using WeightType = std::remove_cv_t<RawWeightType>;

			static_assert(std::is_arithmetic_v<WeightType> && !std::is_same_v<WeightType, bool>,
				"RandWeighted: weight element type must be a non-bool arithmetic type");

			using WorkType = std::conditional_t<
				(std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits ||
				 std::numeric_limits<long double>::max_exponent > std::numeric_limits<double>::max_exponent),
				long double, double>;

			WorkType maxWeight = 0;
			for (const auto& w : weights)
			{
				if constexpr (std::is_floating_point_v<WeightType>)
				{
					if (!std::isfinite(w) || w < 0)
					{
						throw std::invalid_argument("RandWeighted: weights must be finite and non-negative");
					}
				}
				else
				{
					if (w < 0)
					{
						throw std::invalid_argument("RandWeighted: weights must be non-negative");
					}
				}

				const auto val = static_cast<WorkType>(w);
				if (val > maxWeight)
				{
					maxWeight = val;
				}
			}

			if (maxWeight <= 0)
			{
				throw std::invalid_argument("RandWeighted: at least one weight must be strictly positive");
			}

			PreparedWeights<WeightContainer> result;
			typename WeightContainer::size_type currentIndex = 0;

			for (const auto& w : weights)
			{
				if (w > 0)
				{
					const WorkType scaled = static_cast<WorkType>(w) / maxWeight;
					const double doubleWeight = static_cast<double>(scaled);
					if (doubleWeight <= 0.0)
					{
						throw std::range_error("RandWeighted: positive weight underflowed to zero after normalization");
					}
					result.active_indices.push_back(currentIndex);
					result.normalized_weights.push_back(doubleWeight);
				}
				++currentIndex;
			}

			return result;
		}
	}

	/// @brief 按权重随机选取索引
	/// @param weights 权重容器（元素为数值类型）
	/// @return 按权重概率选中的索引值
	template <class WeightContainer>
	[[nodiscard]]
	inline typename WeightContainer::size_type RandWeighted(const WeightContainer& weights)
	{
		return RandWeighted(DefaultEngine(), weights);
	}

	/// @brief 按权重随机选取索引（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param weights 权重容器（元素为数值类型）
	/// @return 按权重概率选中的索引值
	template <detail::RandomEngine Engine, class WeightContainer>
	[[nodiscard]]
	inline typename WeightContainer::size_type RandWeighted(Engine& engine, const WeightContainer& weights)
	{
		const auto prepared = detail::PrepareWeights(weights);
		std::discrete_distribution<std::size_t> dist(prepared.normalized_weights.begin(), prepared.normalized_weights.end());
		return prepared.active_indices[dist(engine)];
	}

	/// @brief 按预构建权重分布随机选取索引；支持分布对象复用，查找复杂度取决于标准库实现
	/// @param dist 预构建的 std::discrete_distribution 对象
	/// @return 按权重概率选中的索引值
	template <class IntType>
	[[nodiscard]]
	inline IntType RandWeighted(std::discrete_distribution<IntType>& dist)
	{
		return dist(DefaultEngine());
	}

	/// @brief 按预构建权重分布随机选取索引（指定引擎）；支持分布对象复用，查找复杂度取决于标准库实现
	/// @param engine 自定义随机数引擎
	/// @param dist 预构建的 std::discrete_distribution 对象
	/// @return 按权重概率选中的索引值
	template <detail::RandomEngine Engine, class IntType>
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
	template <std::integral T, detail::RandomEngine Engine>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	[[nodiscard]]
	inline T RandInt(Engine& engine, T min, T max)
	{
		if (min > max)
			throw std::invalid_argument("RandInt: min > max");
		using ValueType = std::remove_cv_t<T>;
		using DistType = std::conditional_t<(sizeof(ValueType) < sizeof(short)),
			std::conditional_t<std::is_signed_v<ValueType>, int, unsigned int>,
			std::conditional_t<std::is_signed_v<ValueType>,
				std::make_signed_t<ValueType>, std::make_unsigned_t<ValueType>>>;
		std::uniform_int_distribution<DistType> dist(static_cast<DistType>(min), static_cast<DistType>(max));
		return static_cast<T>(dist(engine));
	}

	/// @brief 采用无偏 Bit-Extraction 直通算法生成 [0, 1) 半开区间的随机浮点数（指定引擎重载）
	/// @tparam T 浮点数类型（float / double）
	/// @param engine 伪随机数生成引擎（自动兼容 32 位与 64 位输出引擎）
	/// @return [0, 1) 范围内的无偏伪随机浮点数
	template <std::floating_point T = double, detail::RandomEngine Engine>
	[[nodiscard]]
	inline constexpr T RandCanonical(Engine& engine)
	{
		if constexpr (std::is_same_v<T, double>)
		{
			if constexpr (detail::IsFull64BitEngine<Engine>)
			{
				const std::uint64_t r = static_cast<std::uint64_t>(engine());
				return static_cast<double>(r >> 11) * 0x1.0p-53;
			}
			else if constexpr (detail::IsFull32BitEngine<Engine>)
			{
				const std::uint64_t high = static_cast<std::uint64_t>(engine());
				const std::uint64_t low  = static_cast<std::uint64_t>(engine());
				const std::uint64_t r = (high << 32) | low;
				return static_cast<double>(r >> 11) * 0x1.0p-53;
			}
			else
			{
				return std::generate_canonical<double, 53>(engine);
			}
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			if constexpr (detail::IsFull64BitEngine<Engine>)
			{
				const std::uint64_t r = static_cast<std::uint64_t>(engine());
				return static_cast<float>(r >> 40) * 0x1.0p-24f;
			}
			else if constexpr (detail::IsFull32BitEngine<Engine>)
			{
				const std::uint32_t r = static_cast<std::uint32_t>(engine());
				return static_cast<float>(r >> 8) * 0x1.0p-24f;
			}
			else
			{
				return std::generate_canonical<float, 24>(engine);
			}
		}
		else
		{
			return std::generate_canonical<T, std::numeric_limits<T>::digits>(engine);
		}
	}

	namespace detail
	{
	template <class T>
	class TriangularKernel
	{
	public:
		using WorkType = std::conditional_t<std::is_same_v<T, float>, double, T>;

		TriangularKernel(T min, T peak, T max)
			: m_min(min), m_peak(peak), m_max(max), m_isDegenerate(min == peak && peak == max)
		{
			if (!std::isfinite(min) || !std::isfinite(peak) || !std::isfinite(max)
				|| min > peak || peak > max)
			{
				throw std::invalid_argument("RandTriangular: invalid min, peak, or max");
			}
			if (m_isDegenerate)
			{
				return;
			}

			if (min < T{0} && max > T{0}
				&& max > (std::numeric_limits<T>::max)() + min)
			{
				throw std::invalid_argument("RandTriangular: range width overflow");
			}
			const T returnWidth = max - min;
			if (!std::isfinite(returnWidth))
			{
				throw std::invalid_argument("RandTriangular: range width overflow");
			}

			m_minWork = static_cast<WorkType>(min);
			m_peakWork = static_cast<WorkType>(peak);
			m_maxWork = static_cast<WorkType>(max);
			m_width = m_maxWork - m_minWork;
			m_leftWidth = m_peakWork - m_minWork;
			m_rightWidth = m_maxWork - m_peakWork;
			m_leftRatio = m_leftWidth / m_width;
			m_rightRatio = m_rightWidth / m_width;
			m_leftAnchorSwitch = m_leftRatio * Quarter;
			m_rightAnchorSwitch = WorkType{1} - m_rightRatio * Quarter;
			m_leftAnchorSwitchValue = static_cast<T>(m_minWork + m_leftWidth * Half);
			m_rightAnchorSwitchValue = static_cast<T>(m_maxWork - m_rightWidth * Half);
			m_upperCorrected = std::nextafter(max, min);
		}

		[[nodiscard]]
		bool IsDegenerate() const noexcept
		{
			return m_isDegenerate;
		}

		[[nodiscard]]
		T DegenerateValue() const noexcept
		{
			return m_min;
		}

		[[nodiscard]]
		T Quantile(WorkType uniform) const
		{
			if (!std::isfinite(uniform) || uniform < WorkType{0} || uniform >= WorkType{1})
			{
				throw std::invalid_argument("RandTriangular: uniform input is outside [0, 1)");
			}
			if (m_isDegenerate)
			{
				return m_min;
			}
			if (uniform == WorkType{0})
			{
				return m_min;
			}
			if (uniform == m_leftRatio)
			{
				return m_peak;
			}

			WorkType sample{};
			if (uniform < m_leftRatio)
			{
				if (uniform < m_leftAnchorSwitch)
				{
					const WorkType distanceFromMin =
						m_leftWidth * std::sqrt(uniform / m_leftRatio);
					sample = m_minWork + distanceFromMin;
				}
				else
				{
					const WorkType modeGap = m_leftRatio - uniform;
					const WorkType modeDenominator =
						WorkType{1} + std::sqrt(uniform / m_leftRatio);
					const WorkType distanceFromPeak = (m_width * modeGap) / modeDenominator;
					sample = m_peakWork - distanceFromPeak;
				}
			}
			else
			{
				if (uniform < m_rightAnchorSwitch)
				{
					const WorkType modeGap = uniform - m_leftRatio;
					const WorkType modeDenominator =
						WorkType{1} + std::sqrt((WorkType{1} - uniform) / m_rightRatio);
					const WorkType distanceFromPeak = (m_width * modeGap) / modeDenominator;
					sample = m_peakWork + distanceFromPeak;
				}
				else
				{
					const WorkType distanceFromMax =
						m_rightWidth * std::sqrt((WorkType{1} - uniform) / m_rightRatio);
					sample = m_maxWork - distanceFromMax;
				}
			}

			if (!std::isfinite(sample) || sample < m_minWork || sample > m_maxWork)
			{
				throw std::runtime_error("TriangularKernel: numerical failure in quantile conversion");
			}
			T result = static_cast<T>(sample);
			if (!std::isfinite(result))
			{
				throw std::runtime_error("TriangularKernel: non-finite result");
			}
			if (result < m_min)
			{
				result = m_min;
			}
			if (uniform < m_leftRatio)
			{
				if (uniform < m_leftAnchorSwitch && result > m_leftAnchorSwitchValue)
				{
					result = m_leftAnchorSwitchValue;
				}
				else if (uniform >= m_leftAnchorSwitch && result < m_leftAnchorSwitchValue)
				{
					result = m_leftAnchorSwitchValue;
				}
			}
			else if (uniform < m_rightAnchorSwitch && result > m_rightAnchorSwitchValue)
			{
				result = m_rightAnchorSwitchValue;
			}
			else if (uniform >= m_rightAnchorSwitch && result < m_rightAnchorSwitchValue)
			{
				result = m_rightAnchorSwitchValue;
			}
			if (result >= m_max)
			{
				result = m_upperCorrected;
			}
			return result;
		}

		template <class Engine>
		[[nodiscard]]
		T Generate(Engine& engine) const
		{
			if (m_isDegenerate)
			{
				return m_min;
			}
			return Quantile(static_cast<WorkType>(RandCanonical<T>(engine)));
		}

	private:
		static constexpr WorkType Half = WorkType{1} / WorkType{2};
		static constexpr WorkType Quarter = WorkType{1} / WorkType{4};

		T m_min;
		T m_peak;
		T m_max;
		T m_upperCorrected{0};
		bool m_isDegenerate;
		WorkType m_minWork{0};
		WorkType m_peakWork{0};
		WorkType m_maxWork{0};
		WorkType m_width{0};
		WorkType m_leftWidth{0};
		WorkType m_rightWidth{0};
		WorkType m_leftRatio{0};
		WorkType m_rightRatio{0};
		WorkType m_leftAnchorSwitch{0};
		WorkType m_rightAnchorSwitch{1};
		T m_leftAnchorSwitchValue{0};
		T m_rightAnchorSwitchValue{0};
	};
	}

	/// @brief 生成三角分布随机数
	/// @param min 下界（含）
	/// @param peak 众数
	/// @param max 上界（不含）
	/// @return 服从以 peak 为众数的 [min, max) 三角分布随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandTriangular(T min, T peak, T max)
	{
		detail::TriangularKernel<T> kernel(min, peak, max);
		if (kernel.IsDegenerate())
		{
			return kernel.DegenerateValue();
		}
		return kernel.Generate(DefaultEngine());
	}

	/// @brief 生成三角分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界（含）
	/// @param peak 众数
	/// @param max 上界（不含）
	/// @return 服从以 peak 为众数的 [min, max) 三角分布随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandTriangular(Engine& engine, T min, T peak, T max)
	{
		detail::TriangularKernel<T> kernel(min, peak, max);
		return kernel.Generate(engine);
	}

	/// @brief 生成 [min, max) 范围内的随机浮点数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界（含，默认 0）
	/// @param max 上界（不含，默认 1）
	/// @return 均匀分布于 [min, max) 的随机浮点数
	template <std::floating_point T = double, detail::RandomEngine Engine>
	[[nodiscard]]
	inline T RandReal(Engine& engine, T min = T{0}, T max = T{1})
	{
		detail::RealIntervalKernel<T> kernel(min, max, "RandReal");
		if (kernel.IsDegenerate())
		{
			return kernel.DegenerateValue();
		}
		if (min == T{0} && max == T{1})
		{
			return RandCanonical<T>(engine);
		}
		return kernel.Generate(engine);
	}



	namespace detail
	{
		// 前向声明（定义见下方"编译期随机"节）
		[[nodiscard]]
		inline constexpr std::uint64_t BoundedRand(Xoshiro256StarStar& rng, std::uint64_t range) noexcept;

		// RandSample 分支选择阈值：n·K < size 时用 hash-set（实测交叉点 n≈N/127，K=64 留 2× 裕度）
		inline constexpr std::uint64_t HashSetThresholdK = 64;
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
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline char RandChar(Engine& engine, CharSet cs)
	{
		const auto charset = detail::CharSetString(cs);
		if (charset.empty())
			throw std::invalid_argument("RandChar: charset is empty");
		std::uniform_int_distribution<std::size_t> dist(0, charset.size() - 1);
		return charset[dist(engine)];
	}

	////////////////////////////////////////////////////////////////
	//
	//	扩展便捷 API
	//


	// ============================================================
	// RandSample 迭代器版
	// 路径 1：随机访问迭代器 —— hash-set / 索引数组双分支
	// 路径 2：输入迭代器 —— reservoir sampling (Algorithm R)
	// ============================================================

	namespace detail
	{
		// 抽样适配与位图路径在调用点展开，索引操作内核持有分配与遍历。
#if defined(_MSC_VER)
#define RANDX_DETAIL_SAMPLE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define RANDX_DETAIL_SAMPLE_INLINE inline __attribute__((always_inline))
#else
#define RANDX_DETAIL_SAMPLE_INLINE inline
#endif
		template <class Diff>
		inline void ValidateSampleSize(Diff size)
		{
			using IndexLimit = std::conditional_t<
				(std::numeric_limits<std::size_t>::digits < std::numeric_limits<std::uint64_t>::digits),
				std::size_t, std::uint64_t>;
			if constexpr (std::numeric_limits<Diff>::digits > std::numeric_limits<IndexLimit>::digits)
			{
				if (size > static_cast<Diff>((std::numeric_limits<IndexLimit>::max)()))
					throw std::length_error("RandSample: range exceeds supported index size");
			}
		}

		inline constexpr std::uint64_t SampleBitmapWordBits = std::numeric_limits<std::uint64_t>::digits;
		inline constexpr std::uint64_t SampleBitmapThresholdK = HashSetThresholdK * SampleBitmapWordBits;
		inline constexpr std::uint64_t SampleBitmapDensityDivisor = 2;

		RANDX_DETAIL_SAMPLE_INLINE bool IsEmptySampleRequest(std::size_t count) noexcept
		{
			return count == 0;
		}

		// 完整同宽引擎的区间映射在抽取点展开，保留标准库的分布算法。
		template <class Distribution, class Engine>
#if defined(__GNUC__) || defined(__clang__)
		__attribute__((flatten))
#endif
		RANDX_DETAIL_SAMPLE_INLINE auto DrawFullRangeSampleDistribution(Distribution& distribution, Engine& engine) -> decltype(distribution(engine))
		{
			return distribution(engine);
		}

		template <class UInt, class Engine>
		RANDX_DETAIL_SAMPLE_INLINE UInt DrawSampleDistribution(std::uniform_int_distribution<UInt>& distribution, Engine& engine)
		{
			if constexpr (Engine::min() == UInt{0} && Engine::max() == (std::numeric_limits<UInt>::max)())
				return DrawFullRangeSampleDistribution(distribution, engine);
			return distribution(engine);
		}

		template <class T, class Diff, class It, class Engine>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleBitmap(Engine& engine, It first, std::uint64_t size, std::size_t n)
		{
			const std::size_t wordCount = static_cast<std::size_t>(
				size / SampleBitmapWordBits + (size % SampleBitmapWordBits != 0));
			std::vector<std::uint64_t> selected(wordCount, 0);
			std::vector<T> result;
			result.reserve(n);
			std::uniform_int_distribution<std::uint64_t> indices(0, size - 1);
			// 每个索引占一位；重复索引重抽，接受顺序仍是均匀的无放回抽样。
			while (result.size() < n)
			{
				const std::uint64_t index = DrawSampleDistribution(indices, engine);
				auto& word = selected[static_cast<std::size_t>(index / SampleBitmapWordBits)];
				const std::uint64_t mask = std::uint64_t{1} << (index % SampleBitmapWordBits);
				if ((word & mask) == 0)
				{
					word |= mask;
					result.push_back(first[static_cast<Diff>(index)]);
				}
			}
			return result;
		}

		// 固定区间分布的生命周期由入口策略选择，碰撞重试也属于一次抽取。
		enum class SampleDistributionLifetime { Selection, Draw };

		template <class UInt, SampleDistributionLifetime Lifetime>
		class SampleFixedIndexDistribution;

		template <class UInt>
		class SampleFixedIndexDistribution<UInt, SampleDistributionLifetime::Selection>
		{
			std::uniform_int_distribution<UInt> distribution;
		public:
			SampleFixedIndexDistribution(UInt lower, UInt upper) : distribution(lower, upper) {}
			template <class Engine>
			UInt operator()(Engine& engine) { return distribution(engine); }
		};

		template <class UInt>
		class SampleFixedIndexDistribution<UInt, SampleDistributionLifetime::Draw>
		{
			UInt lower;
			UInt upper;
		public:
			SampleFixedIndexDistribution(UInt lowerBound, UInt upperBound) : lower(lowerBound), upper(upperBound) {}
			template <class Engine>
			UInt operator()(Engine& engine)
			{
				return RandInt<UInt>(engine, lower, upper);
			}
		};

		template <class UInt, SampleDistributionLifetime Lifetime, class Engine>
		RANDX_DETAIL_SAMPLE_INLINE UInt DrawReservoirSampleIndex(SampleFixedIndexDistribution<UInt, Lifetime>& distribution, Engine& engine)
		{
			if constexpr (Lifetime == SampleDistributionLifetime::Selection &&
				Engine::min() == UInt{0} && Engine::max() == (std::numeric_limits<UInt>::max)())
				return DrawFullRangeSampleDistribution(distribution, engine);
			return distribution(engine);
		}

		inline constexpr std::size_t SampleLinearIndexCapacity = 2;
		inline constexpr std::size_t SampleSingleValueCount = 1;

		// 小样本分支保持独立调用边界，限制通用抽样内核的展开规模。
		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class Engine>
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		inline std::vector<T> SampleLinearIndices(Engine& engine, It& first, std::uint64_t size, std::size_t count)
		{
			// 单个索引和索引对使用定长存储，每次重抽至多比较一个已选索引。
			std::array<std::uint64_t, SampleLinearIndexCapacity> selected{};
			std::vector<T> result;
			result.reserve(count);
			SampleFixedIndexDistribution<std::uint64_t, Lifetime> dist(0, size - 1);
			while (result.size() < count)
			{
				const std::uint64_t index = dist(engine);
				const auto selectedEnd = selected.begin() + result.size();
				if (std::find(selected.begin(), selectedEnd, index) == selectedEnd)
				{
					selected[result.size()] = index;
					result.push_back(first[static_cast<Diff>(index)]);
				}
			}
			return result;
		}
#if defined(_MSC_VER)
#define RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY __declspec(noinline) inline
#elif defined(__GNUC__) || defined(__clang__)
#define RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY inline __attribute__((noinline))
#else
#define RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY inline
#endif

		// 批量复制与稀疏抽样持有独立的分配和遍历边界。
		template <class T, class Diff, class It>
		RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY std::vector<T> CopySamplePopulation(It& first, Diff size)
		{
			// 标准容器迭代器与原生指针保持连续存储的批量复制能力。
			if constexpr (std::is_trivially_copyable_v<T> &&
				(std::is_pointer_v<It> ||
				 std::is_same_v<It, typename std::vector<T>::iterator> ||
				 std::is_same_v<It, typename std::vector<T>::const_iterator>))
				return std::vector<T>(first, first + size);
			std::vector<T> all;
			all.reserve(static_cast<std::size_t>(size));
			for (Diff i = 0; i < size; ++i)
				all.push_back(first[i]);
			return all;
		}

		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class Engine>
		RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY std::vector<T> SampleSparseIndices(Engine& engine, It& first, std::uint64_t sizeU, std::size_t count)
		{
			// hash-set 分支：O(n) 内存，O(n) 期望时间
			std::unordered_set<std::uint64_t> selected;
			selected.reserve(count);
			std::vector<T> result;
			result.reserve(count);
			SampleFixedIndexDistribution<std::uint64_t, Lifetime> dist(0, sizeU - 1);
			while (result.size() < count)
			{
				const std::uint64_t idx = dist(engine);
				if (selected.insert(idx).second)
					result.push_back(first[static_cast<Diff>(idx)]);
			}
			return result;
		}

		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class Engine>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleDenseIndicesInline(Engine& engine, It& first, Diff size, std::size_t count)
		{
			// 索引数组分支：O(N) 内存，O(N) 时间，无碰撞
			const std::size_t sz = static_cast<std::size_t>(size);
			std::vector<std::size_t> indices(sz);
			for (std::size_t i = 0; i < sz; ++i)
				indices[i] = i;

			// Fisher-Yates 前 n 步：j ∈ [i, size-1]
			for (std::size_t i = 0; i < count; ++i)
			{
				SampleFixedIndexDistribution<std::size_t, Lifetime> dist(i, sz - 1);
				const std::size_t j = dist(engine);
				std::swap(indices[i], indices[j]);
			}

			std::vector<T> result;
			result.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
				result.push_back(first[static_cast<Diff>(indices[i])]);
			return result;
		}

		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class Engine>
		RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY std::vector<T> SampleDenseIndicesOperation(Engine& engine, It& first, Diff size, std::size_t count)
		{
			return SampleDenseIndicesInline<T, Diff, Lifetime>(engine, first, size, count);
		}

		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class Engine>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleDenseIndices(Engine& engine, It& first, Diff size, std::size_t count)
		{
			// 完整 64 位引擎在调用点保留状态，其他引擎保持独立的映射与遍历边界。
			if constexpr (Engine::min() == std::uint64_t{0} && Engine::max() == (std::numeric_limits<std::uint64_t>::max)())
				return SampleDenseIndicesInline<T, Diff, Lifetime>(engine, first, size, count);
			return SampleDenseIndicesOperation<T, Diff, Lifetime>(engine, first, size, count);
		}
#undef RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY

		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class GetEngine>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleRandomAccess(It& first, Diff size, Diff n, GetEngine&& getEngine)
		{
			if (n <= 0 || size <= 0)
				return {};
			detail::ValidateSampleSize(size);
			if (n >= size)
			{
				if constexpr (std::is_integral_v<Diff>)
				{
					if (size == static_cast<Diff>(SampleSingleValueCount))
					{
						std::vector<T> all;
						all.reserve(SampleSingleValueCount);
						all.push_back(first[Diff{0}]);
						return all;
					}
				}
				return CopySamplePopulation<T>(first, size);
			}

			auto& rng = getEngine();
			const auto sizeU = static_cast<std::uint64_t>(size);
			const auto nU = static_cast<std::uint64_t>(n);
			const auto nSample = static_cast<std::size_t>(n);
			if (nU <= (sizeU - 1) / detail::HashSetThresholdK)
			{
				if (nSample <= SampleLinearIndexCapacity)
					return SampleLinearIndices<T, Diff, Lifetime>(rng, first, sizeU, nSample);
				return SampleSparseIndices<T, Diff, Lifetime>(rng, first, sizeU, nSample);
			}
			return SampleDenseIndices<T, Diff, Lifetime>(rng, first, size, nSample);
		}


		template <class T, class Diff, SampleDistributionLifetime Lifetime = SampleDistributionLifetime::Selection, class It, class Sentinel, class GetEngine>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleReservoir(It& first, Sentinel& last, Diff n, GetEngine&& getEngine)
		{
			if (n <= 0)
				return {};

			std::vector<T> reservoir;
			using Count = std::uint64_t;
			constexpr Count maxCount = (std::numeric_limits<Count>::max)();
			Count requestedCount;
			if constexpr (std::numeric_limits<Diff>::digits <= std::numeric_limits<Count>::digits)
			{
				requestedCount = static_cast<Count>(n);
			}
			else
			{
				requestedCount = n > static_cast<Diff>(maxCount) ? maxCount : static_cast<Count>(n);
			}

			Count count = 0;
			while (reservoir.size() < requestedCount && first != last)
			{
				if (count == maxCount)
					throw std::length_error("SampleReservoir: range exceeds count capacity");
				reservoir.push_back(*first);
				++first;
				++count;
			}

			if (first == last)
				return reservoir;  // 元素不足 n，返回全部
			if (count == maxCount)
				throw std::length_error("SampleReservoir: range exceeds count capacity");

			// Algorithm R：第 i 个元素（i >= n，0-indexed）以 n/(i+1) 概率替换蓄水池随机位置
			// 关键：j ∈ [0, i]（闭区间），uniform_int_distribution(0, i) 正好是 [0, i] 闭区间
			auto& rng = getEngine();
			while (first != last)
			{
				if (count == maxCount)
					throw std::length_error("SampleReservoir: range exceeds count capacity");
				SampleFixedIndexDistribution<Count, Lifetime> dist(Count{0}, count);
				const auto j = DrawReservoirSampleIndex(dist, rng);
				if (j < static_cast<Count>(reservoir.size()))
					reservoir[static_cast<std::size_t>(j)] = *first;
				++first;
				++count;
			}
			return reservoir;
		}
	}

	/// @brief 无放回抽样（随机访问迭代器版，hash-set / 索引数组双分支）
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	// 路径 1：随机访问迭代器（hash-set / 索引数组双分支）
	template <std::random_access_iterator It, std::sentinel_for<It> Sentinel>
	[[nodiscard]]
	RANDX_DETAIL_SAMPLE_INLINE std::vector<std::iter_value_t<It>>
	RandSample(It first, Sentinel last, std::iter_difference_t<It> n)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		const Diff size = static_cast<Diff>(std::ranges::distance(first, last));
		// 整型差值使用空请求快路径；其他差值类型由内核判定。
		if constexpr (std::is_integral_v<Diff>)
			if (n <= 0 || size <= 0) return {};
		return detail::SampleRandomAccess<T, Diff, detail::SampleDistributionLifetime::Selection>(
			first, size, n, []() -> Xoshiro256StarStar& { return DefaultEngine(); });
	}

	/// @brief 无放回抽样：按索引从容器中随机抽取 n 个元素
	/// @param c 源容器（const 限定后仍支持随机访问遍历）
	/// @param n 抽取数量（若 n >= 容器大小则返回全部元素的副本）
	/// @return 含 n 个随机选取元素的 vector
	template <class Container>
		requires std::ranges::random_access_range<const Container>
			&& std::copy_constructible<std::ranges::range_value_t<const Container>>
	[[nodiscard]]
	RANDX_DETAIL_SAMPLE_INLINE auto RandSample(const Container& c, std::size_t n)
	{
		using T = std::ranges::range_value_t<const Container>;
		using Diff = std::ranges::range_difference_t<const Container>;
		if (detail::IsEmptySampleRequest(n)) return std::vector<T>{};
		Diff count;
		if constexpr (std::numeric_limits<Diff>::digits < std::numeric_limits<std::size_t>::digits)
		{
			constexpr auto maxCount = static_cast<std::size_t>((std::numeric_limits<Diff>::max)());
			count = static_cast<Diff>((std::min)(n, maxCount));
		}
		else
		{
			count = static_cast<Diff>(n);
		}
		const auto first = std::ranges::begin(c);
		const auto last = std::ranges::end(c);
		const Diff size = std::ranges::distance(first, last);
		if (size <= 0) return std::vector<T>{};
		auto& engine = DefaultEngine();
		detail::ValidateSampleSize(size);
		const auto sizeU = static_cast<std::uint64_t>(size);
		if (n > (sizeU - 1) / detail::SampleBitmapThresholdK && n <= sizeU / detail::SampleBitmapDensityDivisor)
			return detail::SampleBitmap<T, Diff>(engine, first, sizeU, n);
		return RandSample(engine, first, first + size, count);
	}

	/// @brief 无放回抽样（输入迭代器版，reservoir sampling Algorithm R）
	/// @note 元素须可复制构造和复制赋值
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	// 路径 2：输入迭代器（reservoir sampling, Algorithm R）
	template <std::input_iterator It, std::sentinel_for<It> Sentinel>
		requires (!std::random_access_iterator<It>)
			&& std::copy_constructible<std::iter_value_t<It>>
			&& std::is_copy_assignable_v<std::iter_value_t<It>>
	[[nodiscard]]
	inline std::vector<std::iter_value_t<It>>
	RandSample(It first, Sentinel last, std::iter_difference_t<It> n)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		return detail::SampleReservoir<T, Diff>(first, last, n, []() -> Xoshiro256StarStar& { return DefaultEngine(); });
	}

	/// @brief 无放回抽样（指定引擎，随机访问迭代器版）
	/// @param engine 自定义随机数引擎
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	// 引擎重载 —— 随机访问迭代器
	template <std::random_access_iterator It, std::sentinel_for<It> Sentinel, detail::RandomEngine Engine>
	[[nodiscard]]
	RANDX_DETAIL_SAMPLE_INLINE std::vector<std::iter_value_t<It>>
	RandSample(Engine& engine, It first, Sentinel last, std::iter_difference_t<It> n)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		const Diff size = static_cast<Diff>(std::ranges::distance(first, last));
		// 整型差值使用空请求快路径；其他差值类型由内核判定。
		if constexpr (std::is_integral_v<Diff>)
			if (n <= 0 || size <= 0) return {};
		return detail::SampleRandomAccess<T, Diff, detail::SampleDistributionLifetime::Draw>(
			first, size, n, [&engine]() -> Engine& { return engine; });
	}

	/// @brief 无放回抽样（指定引擎，输入迭代器版，reservoir sampling）
	/// @note 元素须可复制构造和复制赋值
	/// @param engine 自定义随机数引擎
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	// 引擎重载 —— 输入迭代器（reservoir）
	template <std::input_iterator It, std::sentinel_for<It> Sentinel, detail::RandomEngine Engine>
		requires (!std::random_access_iterator<It>)
			&& std::copy_constructible<std::iter_value_t<It>>
			&& std::is_copy_assignable_v<std::iter_value_t<It>>
	[[nodiscard]]
	inline std::vector<std::iter_value_t<It>>
	RandSample(Engine& engine, It first, Sentinel last, std::iter_difference_t<It> n)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		return detail::SampleReservoir<T, Diff, detail::SampleDistributionLifetime::Draw>(first, last, n, [&engine]() -> Engine& { return engine; });
	}

	/// @brief 无放回抽样：从容器中随机抽取 n 个元素（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param c 源容器（const 限定后仍支持随机访问遍历）
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	template <detail::RandomEngine Engine, class Container>
		requires std::ranges::random_access_range<const Container>
			&& std::copy_constructible<std::ranges::range_value_t<const Container>>
	[[nodiscard]]
	RANDX_DETAIL_SAMPLE_INLINE auto RandSample(Engine& engine, const Container& c, std::size_t n)
	{
		using T = std::ranges::range_value_t<const Container>;
		using Diff = std::ranges::range_difference_t<const Container>;
		if (detail::IsEmptySampleRequest(n)) return std::vector<T>{};
		Diff count;
		if constexpr (std::numeric_limits<Diff>::digits < std::numeric_limits<std::size_t>::digits)
		{
			constexpr auto maxCount = static_cast<std::size_t>((std::numeric_limits<Diff>::max)());
			count = static_cast<Diff>((std::min)(n, maxCount));
		}
		else
		{
			count = static_cast<Diff>(n);
		}
		const auto first = std::ranges::begin(c);
		const auto last = std::ranges::end(c);
		const Diff size = std::ranges::distance(first, last);
		if (size <= 0) return std::vector<T>{};
		detail::ValidateSampleSize(size);
		const auto sizeU = static_cast<std::uint64_t>(size);
		if (n > (sizeU - 1) / detail::SampleBitmapThresholdK && n <= sizeU / detail::SampleBitmapDensityDivisor)
			return detail::SampleBitmap<T, Diff>(engine, first, sizeU, n);
		return RandSample(engine, first, first + size, count);
	}
#undef RANDX_DETAIL_SAMPLE_INLINE

	/// @brief 生成 [0, n) 的随机排列
	/// @param n 排列长度
	/// @return 含 0 到 n-1 随机排列的 vector
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
			std::swap(perm[i], perm[j]);
		}
		return perm;
	}

	/// @}

	/// @defgroup strings 字符串与 ID
	/// @brief RandString / RandUUID
	/// @{

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
	template <detail::RandomEngine Engine>
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
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline std::string RandString(Engine& engine, std::size_t n, CharSet cs)
	{
		return RandString(engine, n, detail::CharSetString(cs));
	}

	/// @brief 生成指数分布随机数
	/// @param lambda 速率参数（默认 1，均值 = 1/lambda）
	/// @return 服从 Exp(lambda) 的随机数
	template <std::floating_point T = double>
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
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandExp(Engine& engine, T lambda = T{1})
	{
		if (!std::isfinite(lambda) || lambda <= T{0})
			throw std::invalid_argument("RandExp: lambda must be positive");
		std::exponential_distribution<T> dist(lambda);
		return dist(engine);
	}

	namespace detail
	{
		template <class T>
		struct IsStandardIntegerDistributionType : std::bool_constant<
			std::is_same_v<T, short> ||
			std::is_same_v<T, unsigned short> ||
			std::is_same_v<T, int> ||
			std::is_same_v<T, unsigned int> ||
			std::is_same_v<T, long> ||
			std::is_same_v<T, unsigned long> ||
			std::is_same_v<T, long long> ||
			std::is_same_v<T, unsigned long long>>
		{
		};

		template <class T>
		inline void ValidatePoissonMean(double mean)
		{
			if (!std::isfinite(mean) || mean < 0.0)
				throw std::invalid_argument("RandPoisson: mean must be non-negative");

			constexpr int PoissonDoublePrecisionDigits = std::numeric_limits<double>::digits;
			if constexpr (std::numeric_limits<T>::digits <= PoissonDoublePrecisionDigits)
			{
				if (mean > static_cast<double>((std::numeric_limits<T>::max)()))
					throw std::invalid_argument("RandPoisson: mean exceeds maximum value of return type");
			}
			else
			{
				const double maximumExclusiveMean = std::ldexp(1.0, std::numeric_limits<T>::digits);
				if (mean >= maximumExclusiveMean)
					throw std::invalid_argument("RandPoisson: mean exceeds maximum value of return type");
			}
		}

		template <class T>
		struct IsGeometricDistributionType : std::bool_constant<
			std::is_integral_v<T> &&
			std::is_same_v<T, std::remove_cv_t<T>> &&
			!std::is_same_v<T, bool> &&
			(std::numeric_limits<T>::digits <= std::numeric_limits<std::uint64_t>::digits)>
		{
		};
	}

	/// @brief 生成泊松分布随机数
	/// @tparam T 返回类型为 short、int、long、long long 或其对应的无符号类型
	/// @param mean 均值参数（默认 1.0）
	/// @return 服从 Poisson(mean) 的随机整数
	template <class T = int>
		requires (detail::IsStandardIntegerDistributionType<T>::value)
	[[nodiscard]]
	inline T RandPoisson(double mean = 1.0)
	{
		detail::ValidatePoissonMean<T>(mean);
		if (mean == 0.0) return T{0};
		std::poisson_distribution<T> dist(mean);
		return dist(DefaultEngine());
	}

	/// @brief 生成泊松分布随机数（指定引擎重载）
	/// @tparam T 返回类型为 short、int、long、long long 或其对应的无符号类型
	/// @param engine 自定义随机数引擎
	/// @param mean 均值参数（默认 1.0）
	/// @return 服从 Poisson(mean) 的随机整数
	template <detail::RandomEngine Engine, class T = int>
		requires (detail::IsStandardIntegerDistributionType<T>::value)
	[[nodiscard]]
	inline T RandPoisson(Engine& engine, double mean = 1.0)
	{
		detail::ValidatePoissonMean<T>(mean);
		if (mean == 0.0) return T{0};
		std::poisson_distribution<T> dist(mean);
		return dist(engine);
	}

	/// @brief 生成伽马分布随机数
	/// @param alpha 形状参数（默认 1）
	/// @param beta 尺度参数（默认 1）
	/// @return 服从 Gamma(alpha, beta) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandGamma(T alpha = T{1}, T beta = T{1})
	{
		if (!std::isfinite(alpha) || !std::isfinite(beta) || alpha <= T{0} || beta <= T{0})
			throw std::invalid_argument("RandGamma: alpha and beta must be positive");
		constexpr int GammaShapeSplitCount = 2;
		// 同尺度 Gamma 的形状可相加；半形状采样保持形状翻倍的中间量可表示。
		constexpr T GammaShapeSplitThreshold =
			(std::numeric_limits<T>::max)() / static_cast<T>(GammaShapeSplitCount);
		if (alpha > GammaShapeSplitThreshold)
		{
			const T splitShape = alpha / static_cast<T>(GammaShapeSplitCount);
			std::gamma_distribution<T> splitDistribution(splitShape, beta);
			const T firstSample = splitDistribution(DefaultEngine());
			const T secondSample = splitDistribution(DefaultEngine());
			return firstSample + secondSample;
		}
		std::gamma_distribution<T> dist(alpha, beta);
		return dist(DefaultEngine());
	}

	/// @brief 生成伽马分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param alpha 形状参数（默认 1）
	/// @param beta 尺度参数（默认 1）
	/// @return 服从 Gamma(alpha, beta) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandGamma(Engine& engine, T alpha = T{1}, T beta = T{1})
	{
		if (!std::isfinite(alpha) || !std::isfinite(beta) || alpha <= T{0} || beta <= T{0})
			throw std::invalid_argument("RandGamma: alpha and beta must be positive");
		constexpr int GammaShapeSplitCount = 2;
		// 同尺度 Gamma 的形状可相加；半形状采样保持形状翻倍的中间量可表示。
		constexpr T GammaShapeSplitThreshold =
			(std::numeric_limits<T>::max)() / static_cast<T>(GammaShapeSplitCount);
		if (alpha > GammaShapeSplitThreshold)
		{
			const T splitShape = alpha / static_cast<T>(GammaShapeSplitCount);
			std::gamma_distribution<T> splitDistribution(splitShape, beta);
			const T firstSample = splitDistribution(engine);
			const T secondSample = splitDistribution(engine);
			return firstSample + secondSample;
		}
		std::gamma_distribution<T> dist(alpha, beta);
		return dist(engine);
	}

	/// @brief 生成二项分布随机数
	/// @tparam T 返回类型为 short、int、long、long long 或其对应的无符号类型
	/// @param t 试验次数（默认 1）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 B(t, p) 的随机整数
	template <class T = int>
		requires (detail::IsStandardIntegerDistributionType<T>::value)
	[[nodiscard]]
	inline T RandBinomial(T t = 1, double p = 0.5)
	{
		if (t < 0 || !std::isfinite(p) || p < 0.0 || p > 1.0)
			throw std::invalid_argument("RandBinomial: invalid t or p");
		std::binomial_distribution<T> dist(t, p);
		return dist(DefaultEngine());
	}

	/// @brief 生成二项分布随机数（指定引擎重载）
	/// @tparam T 返回类型为 short、int、long、long long 或其对应的无符号类型
	/// @param engine 自定义随机数引擎
	/// @param t 试验次数（默认 1）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 B(t, p) 的随机整数
	template <detail::RandomEngine Engine, class T = int>
		requires (detail::IsStandardIntegerDistributionType<T>::value)
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
	template <std::floating_point T = double>
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
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandLogNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandLogNormal: invalid mean or stddev");
		std::lognormal_distribution<T> dist(mean, stddev);
		return dist(engine);
	}

	/// @brief 生成几何分布随机数（首次成功前的失败次数）
	/// @tparam T bool 以外的 cv 未限定整数类型，数值位数不超过 std::uint64_t 位宽；该范围内字符类型也受支持
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 Geometric(p) 的随机整数
	/// @throw std::overflow_error 抽样值超出返回类型范围时抛出
	template <class T = int>
		requires (detail::IsGeometricDistributionType<T>::value)
	[[nodiscard]]
	inline T RandGeometric(double p = 0.5)
	{
		return RandGeometric<Xoshiro256StarStar, T>(DefaultEngine(), p);
	}

	/// @brief 生成几何分布随机数（指定引擎重载）
	/// @tparam T bool 以外的 cv 未限定整数类型，数值位数不超过 std::uint64_t 位宽；该范围内字符类型也受支持
	/// @param engine 自定义随机数引擎
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 Geometric(p) 的随机整数
	/// @throw std::overflow_error 抽样值超出返回类型范围时抛出
	template <detail::RandomEngine Engine, class T = int>
		requires (detail::IsGeometricDistributionType<T>::value)
	[[nodiscard]]
	inline T RandGeometric(Engine& engine, double p = 0.5)
	{
		if (!std::isfinite(p) || p <= 0.0 || p > 1.0)
			throw std::invalid_argument("RandGeometric: p must be in (0, 1]");
		if (p == 1.0)
			return T{0};
		constexpr double maxT = static_cast<double>((std::numeric_limits<T>::max)());
		if (p < 1.0 / (maxT + 1.0))
			throw std::invalid_argument("RandGeometric: p is too small for return type");
		const double denom = -std::log1p(-p);
		int exponent = 0;
		(void)std::frexp(denom, &exponent);
		constexpr int MaxBlockBits = std::numeric_limits<std::uint64_t>::digits - 1;
		const int blockBits = (std::min)(MaxBlockBits, (std::max)(0, -exponent));
		const std::uint64_t blockSize = std::uint64_t{1} << blockBits;
		// q=1-p，X=B*G+R：P(G=g)=(1-q^B)q^(Bg)，P(R=r)=p*q^r/(1-q^B)。
		// B 取二次幂，使块内拒绝采样的接受概率有界，整数余数保留全部低位。
		std::exponential_distribution<double> exp_dist(1.0);
		const double blocks = exp_dist(engine) / (denom * static_cast<double>(blockSize));
		std::uint64_t remainder = 0;
		if (blockSize > 1)
		{
			std::uniform_int_distribution<std::uint64_t> offsets(0, blockSize - 1);
			do
			{
				remainder = offsets(engine);
			} while (exp_dist(engine) < denom * static_cast<double>(remainder));
		}
		const double upperBlocks = std::ldexp(1.0, std::numeric_limits<T>::digits - blockBits);
		if (!std::isfinite(blocks) || blocks >= upperBlocks)
			throw std::overflow_error("RandGeometric: generated value exceeds return type range");
		return static_cast<T>((static_cast<std::uint64_t>(blocks) << blockBits) | remainder);
	}

	/// @brief 生成柯西分布随机数
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Cauchy(a, b) 的随机数
	template <std::floating_point T = double>
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
	template <detail::RandomEngine Engine, std::floating_point T = double>
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
	template <std::floating_point T = double>
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
	template <detail::RandomEngine Engine, std::floating_point T = double>
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
	template <std::floating_point T = double>
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
	template <detail::RandomEngine Engine, std::floating_point T = double>
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
	template <std::floating_point T = double>
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
	template <detail::RandomEngine Engine, std::floating_point T = double>
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
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandStudentT(T n = T{1})
	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandStudentT: n must be positive");
		if (detail::RequiresStableStudentT(n))
			return detail::SampleStableStudentT(DefaultEngine(), n);
		std::student_t_distribution<T> dist(n);
		return dist(DefaultEngine());
	}

	/// @brief 生成学生 t 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 自由度（默认 1）
	/// @return 服从 StudentT(n) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandStudentT(Engine& engine, T n = T{1})
	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandStudentT: n must be positive");
		if (detail::RequiresStableStudentT(n))
			return detail::SampleStableStudentT(engine, n);
		std::student_t_distribution<T> dist(n);
		return dist(engine);
	}

	/// @brief 生成 Fisher F 分布随机数
	/// @param m 第一自由度（默认 1）
	/// @param n 第二自由度（默认 1）
	/// @return 服从 FisherF(m, n) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandFisherF(T m = T{1}, T n = T{1})
	{
		if (!std::isfinite(m) || !std::isfinite(n) || m <= T{0} || n <= T{0})
			throw std::invalid_argument("RandFisherF: invalid m or n");
		if (detail::RequiresStableFisherF(m, n))
			return detail::SampleStableFisherF(DefaultEngine(), m, n);
		std::fisher_f_distribution<T> dist(m, n);
		return dist(DefaultEngine());
	}

	/// @brief 生成 Fisher F 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param m 第一自由度（默认 1）
	/// @param n 第二自由度（默认 1）
	/// @return 服从 FisherF(m, n) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandFisherF(Engine& engine, T m = T{1}, T n = T{1})
	{
		if (!std::isfinite(m) || !std::isfinite(n) || m <= T{0} || n <= T{0})
			throw std::invalid_argument("RandFisherF: invalid m or n");
		if (detail::RequiresStableFisherF(m, n))
			return detail::SampleStableFisherF(engine, m, n);
		std::fisher_f_distribution<T> dist(m, n);
		return dist(engine);
	}

	namespace detail
	{
		template <class T>
		using BetaWorkType = std::conditional_t<std::is_same_v<T, float>, double, T>;

		template <class WorkT>
		struct DecomposedGammaLog
		{
			WorkT base_log{0};
			WorkT exp_val{0};
			WorkT shape{1};
			bool has_extra{false};
		};

		template <class WorkT, class Engine>
		inline void SampleGammaLogScale(Engine& engine, WorkT shape, DecomposedGammaLog<WorkT>& out)
		{
			WorkT effective_shape = shape;
			out.shape = shape;
			if (shape < WorkT{1})
			{
				effective_shape = shape + WorkT{1};
				std::exponential_distribution<WorkT> exp_dist(WorkT{1});
				out.exp_val = exp_dist(engine);
				out.has_extra = true;
			}
			else
			{
				out.exp_val = WorkT{0};
				out.has_extra = false;
			}

			const WorkT d = effective_shape - (WorkT{1} / WorkT{3});
			const WorkT c = (WorkT{1} / WorkT{3}) / std::sqrt(d);

			constexpr std::size_t MaxAttempts = 1000;
			std::normal_distribution<WorkT> norm_dist(WorkT{0}, WorkT{1});

			for (std::size_t attempt = 0; attempt < MaxAttempts; ++attempt)
			{
				const WorkT z = norm_dist(engine);
				const WorkT cz = c * z;
				if (cz <= WorkT{-1})
				{
					continue;
				}

				const WorkT log_v = WorkT{3} * std::log1p(cz);
				const WorkT u = RandCanonical<WorkT>(engine);
				if (u <= WorkT{0})
				{
					continue;
				}

				const WorkT z2 = z * z;
				const WorkT z4 = z2 * z2;
				if (u < WorkT{1} - WorkT{0.0331} * z4)
				{
					out.base_log = std::log(d) + log_v;
					return;
				}

				WorkT diff = WorkT{0};
				const WorkT abs_cz = std::abs(cz);
				if (abs_cz < WorkT{1e-2})
				{
					const WorkT x = cz;
					const WorkT x2 = x * x;
					const WorkT x4 = x2 * x2;
					const WorkT poly = -WorkT{0.75} + x * (WorkT{0.6} + x * (-WorkT{0.5} + x * (WorkT{3.0/7.0} - WorkT{0.375} * x)));
					diff = d * x4 * poly;
				}
				else
				{
					const WorkT x = cz;
					const WorkT x2 = x * x;
					const WorkT x3 = x2 * x;
					const WorkT log1p_minus_x = std::log1p(x) - x;
					diff = d * (WorkT{1.5} * x2 - x3 + WorkT{3} * log1p_minus_x);
				}

				if (std::log(u) < diff)
				{
					out.base_log = std::log(d) + log_v;
					return;
				}
			}

			throw std::runtime_error("RandBeta: Gamma rejection sampling failed to converge");
		}

		template <class WorkT>
		inline WorkT ComputeBetaSampleFromLogScale(const DecomposedGammaLog<WorkT>& a_sample, const DecomposedGammaLog<WorkT>& b_sample)
		{
			const WorkT base_diff = b_sample.base_log - a_sample.base_log;
			WorkT delta = WorkT{0};

			if (a_sample.has_extra && b_sample.has_extra)
			{
				const WorkT sa = a_sample.shape;
				const WorkT sb = b_sample.shape;
				if (sa == sb)
				{
					const WorkT diff = a_sample.exp_val - b_sample.exp_val;
					delta = diff / sa;
				}
				else if (sa < sb)
				{
					const WorkT r = sa / sb;
					const WorkT diff = a_sample.exp_val - r * b_sample.exp_val;
					delta = diff / sa;
				}
				else
				{
					const WorkT r = sb / sa;
					const WorkT diff = r * a_sample.exp_val - b_sample.exp_val;
					delta = diff / sb;
				}
			}
			else if (a_sample.has_extra && !b_sample.has_extra)
			{
				delta = a_sample.exp_val / a_sample.shape;
			}
			else if (!a_sample.has_extra && b_sample.has_extra)
			{
				delta = -b_sample.exp_val / b_sample.shape;
			}

			const WorkT L = base_diff + delta;

			if (std::isnan(L))
			{
				throw std::runtime_error("RandBeta: NaN encountered during Beta calculation");
			}

			if (std::abs(L) < WorkT{0.5})
			{
				const WorkT em1 = std::expm1(L);
				return WorkT{0.5} - em1 / (WorkT{2} * (WorkT{2} + em1));
			}

			if (L >= WorkT{0.5})
			{
				const WorkT exp_neg_L = std::exp(-L);
				return exp_neg_L / (WorkT{1} + exp_neg_L);
			}

			const WorkT exp_L = std::exp(L);
			return WorkT{1} / (WorkT{1} + exp_L);
		}

		template <class WorkT>
		inline WorkT ComputeBetaSampleFromLogScale(WorkT d_a, WorkT e_a, WorkT d_b, WorkT e_b)
		{
			DecomposedGammaLog<WorkT> sample_a, sample_b;
			sample_a.base_log = std::log(d_a) + e_a;
			sample_b.base_log = std::log(d_b) + e_b;
			return ComputeBetaSampleFromLogScale(sample_a, sample_b);
		}
	}

	/// @brief 生成 Beta 分布随机数
	/// @param a 形状参数（默认 1）
	/// @param b 形状参数（默认 1）
	/// @return 服从 Beta(a, b) 的随机数
	/// @note 无 STL 对应，自实现 Gamma(a)/(Gamma(a)+Gamma(b))
	template <std::floating_point T = double>
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
	/// @note 无 STL 对应，自实现 Gamma(a)/(Gamma(a)+Gamma(b))
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandBeta(Engine& engine, T a = T{1}, T b = T{1})
	{
		if (!std::isfinite(a) || !std::isfinite(b) || a <= T{0} || b <= T{0})
			throw std::invalid_argument("RandBeta: invalid a or b");

		using WorkT = detail::BetaWorkType<T>;
		const WorkT wa = static_cast<WorkT>(a);
		const WorkT wb = static_cast<WorkT>(b);

		constexpr WorkT LargeShapeThreshold = WorkT{1000};
		constexpr WorkT SmallShapeThreshold = WorkT{1};
		if (wa >= LargeShapeThreshold || wb >= LargeShapeThreshold ||
		    wa < SmallShapeThreshold || wb < SmallShapeThreshold)
		{
			detail::DecomposedGammaLog<WorkT> sample_a, sample_b;
			detail::SampleGammaLogScale(engine, wa, sample_a);
			detail::SampleGammaLogScale(engine, wb, sample_b);
			const WorkT res = detail::ComputeBetaSampleFromLogScale(sample_a, sample_b);
			return static_cast<T>(res);
		}
		else
		{
			std::gamma_distribution<WorkT> distA(wa, WorkT{1});
			std::gamma_distribution<WorkT> distB(wb, WorkT{1});
			const WorkT x = distA(engine);
			const WorkT y = distB(engine);
			const WorkT res = detail::NormalizeBetaSample(x, y);
			return static_cast<T>(res);
		}
	}

	/// @brief 生成 N 位随机整数
	/// @tparam N 位数（1-64，且不超过 T 的位宽）
	/// @return 均匀分布于 [0, 2^N) 的随机整数
	template <int N, std::integral T = std::uint64_t>
		requires (!std::same_as<std::remove_cv_t<T>, bool>) && (N > 0) && (N <= 64) && (N <= std::numeric_limits<T>::digits)
	[[nodiscard]]
	inline T RandBits()
	{
		return RandBits<N, T>(DefaultEngine());
	}

	/// @brief 生成 N 位随机整数（指定引擎重载）
	/// @tparam N 位数（1-64，且不超过 T 的位宽）
	/// @param engine 自定义随机数引擎
	/// @return 均匀分布于 [0, 2^N) 的随机整数
	template <int N, std::integral T = std::uint64_t, detail::RandomEngine Engine>
		requires (!std::same_as<std::remove_cv_t<T>, bool>) && (N > 0) && (N <= 64) && (N <= std::numeric_limits<T>::digits)
	[[nodiscard]]
	inline T RandBits(Engine& engine)
	{
		if constexpr (detail::IsFull64BitEngine<Engine>)
		{
			const std::uint64_t val = static_cast<std::uint64_t>(engine());
			if constexpr (N == 64)
				return static_cast<T>(val);
			else
				return static_cast<T>(val & ((std::uint64_t{1} << N) - 1));
		}
		else if constexpr (detail::IsFull32BitEngine<Engine>)
		{
			if constexpr (N <= 32)
			{
				const std::uint32_t val = static_cast<std::uint32_t>(engine());
				if constexpr (N == 32)
					return static_cast<T>(val);
				else
					return static_cast<T>(val & ((std::uint32_t{1} << N) - 1));
			}
			else
			{
				const std::uint64_t lo = static_cast<std::uint64_t>(engine());
				const std::uint64_t hi = static_cast<std::uint64_t>(engine());
				const std::uint64_t val = (hi << 32) | lo;
				if constexpr (N == 64)
					return static_cast<T>(val);
				else
					return static_cast<T>(val & ((std::uint64_t{1} << N) - 1));
			}
		}
		else
		{
			if constexpr (N == 64)
			{
				std::uniform_int_distribution<std::uint64_t> dist(0, (std::numeric_limits<std::uint64_t>::max)());
				return static_cast<T>(dist(engine));
			}
			else
			{
				constexpr std::uint64_t maxVal = (std::uint64_t{1} << N) - 1;
				std::uniform_int_distribution<std::uint64_t> dist(0, maxVal);
				return static_cast<T>(dist(engine));
			}
		}
	}

	/// @brief 生成随机 UUID v4 字符串
	/// @return 格式为 xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx 的 UUID 字符串
	/// @warning 使用默认 PRNG（非 CSPRNG），不适用于安全敏感标识符；
	///          安全场景请改用 ChaCha20 引擎重载或 SecureRandomBytes
	template <detail::RandomEngine Engine>
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

// Generated by tools/generate_stream_jump_powers.py from the maintained engine recurrences and jump polynomials.
// Coefficients are little-endian in powers of the one-step state transition.
	namespace detail
	{
		template <class Engine>
		struct StreamJumpPowerTable
		{
			static constexpr bool enabled = false;
		};

		template <>
		struct StreamJumpPowerTable<Xoshiro256StarStar>
		{
			static constexpr bool enabled = true;
			using engine_type = Xoshiro256StarStar;
			using word_type = typename engine_type::result_type;
			using polynomial_type = std::array<word_type, std::tuple_size<typename engine_type::state_type>::value>;
			static constexpr std::size_t powers_per_half = std::numeric_limits<std::uint32_t>::digits;
			static_assert(std::tuple_size<typename engine_type::state_type>::value == 4, "stream jump polynomial state width");
			static_assert(std::numeric_limits<word_type>::digits == 64, "stream jump polynomial word width");
			inline static constexpr std::array<polynomial_type, powers_per_half> jump_powers = {{
				polynomial_type{{ 0x180EC6D33CFD0ABAULL, 0xD5A61266F0C9392CULL, 0xA9582618E03FC9AAULL, 0x39ABDC4529B1661CULL }},
				polynomial_type{{ 0x8CFE9BD9AB71D992ULL, 0xCCFC8CA2814DE79EULL, 0xA5A28CCCB37DBA5BULL, 0xA23E49EE6F1A7A8DULL }},
				polynomial_type{{ 0x1B2A94A672A48C05ULL, 0x5E38F4FBB6FCDA72ULL, 0xCA8A45310219DC67ULL, 0xD4E9921BCCB8090BULL }},
				polynomial_type{{ 0xF30974A2B1DBBB71ULL, 0x34CD4CC8228D74ACULL, 0xFA0587A90F717438ULL, 0xEE658F69DEB5DF26ULL }},
				polynomial_type{{ 0xB42BD4670583B289ULL, 0xD2C0D8E0C8A2FB9BULL, 0x2573E3218D8BB7DAULL, 0xD7AAAF48AA459C58ULL }},
				polynomial_type{{ 0xF6A5AB84EFB67883ULL, 0xCC7EFDCFED1AC303ULL, 0xD82BE75B83DBC2D0ULL, 0x8FD437C01ABEAB24ULL }},
				polynomial_type{{ 0xC85EE5171484F5A4ULL, 0xEDC8B8D02A22310BULL, 0xB0B87A330B854C8AULL, 0x7D16742ECEB4D5ABULL }},
				polynomial_type{{ 0x4298BA0E862A6007ULL, 0x4157DC48443E3565ULL, 0x13C97C0891CAB48AULL, 0x6533981804B420EAULL }},
				polynomial_type{{ 0xEE5F5A6F02DFE47CULL, 0xEDC28C89CB341660ULL, 0x613B2ED9F0ACC107ULL, 0xA1EE335D14807AE0ULL }},
				polynomial_type{{ 0x5EC3050C6B43565AULL, 0x4B26F71C1FB1B47BULL, 0x0531513E8E0AC706ULL, 0x799D469B2145A8A3ULL }},
				polynomial_type{{ 0x34F0A6799020283EULL, 0x7123F2290A1F413BULL, 0xB6ACD7BE4906B73DULL, 0x6007BB31EC5A2964ULL }},
				polynomial_type{{ 0xAA0711C54877FEBDULL, 0x54FE6DF4CFF0DB73ULL, 0x7E42D6F544840499ULL, 0xEC907801890A47ABULL }},
				polynomial_type{{ 0x03833E601D82A673ULL, 0x3EC263F5C999196EULL, 0xD8C4367E574AB160ULL, 0x964E9D188C16508EULL }},
				polynomial_type{{ 0xD64F3F2AAF8F2171ULL, 0xF524FD4408357A5CULL, 0x15AC212F3B861B5AULL, 0x24D9BA21277DD8D8ULL }},
				polynomial_type{{ 0xFE9B778D7D1CA2DEULL, 0xBBE0E2C0C44B2E1CULL, 0x17A7AF3E97D8C402ULL, 0xF89354CFE1E6B5FBULL }},
				polynomial_type{{ 0x695CF225704E767DULL, 0xF4873D277CD1AB72ULL, 0xAAD8C318BC459CCEULL, 0xB89526857566CD94ULL }},
				polynomial_type{{ 0x3DCD32F39276A95FULL, 0xC51212C8B1AA2787ULL, 0x962C90A866EA6719ULL, 0xB81875D0F4F6F253ULL }},
				polynomial_type{{ 0xB43CF8E4EAF8E068ULL, 0x1C554E97B2277F47ULL, 0xA5A140826C351D07ULL, 0x11495A1B200D4EB8ULL }},
				polynomial_type{{ 0x417B73B324735D32ULL, 0xFF957B6F55288048ULL, 0x05AF69BF1FB82891ULL, 0x3E53BFA0DB28E110ULL }},
				polynomial_type{{ 0xB6C7A6004612889CULL, 0xFDB3F4EA18F0A56BULL, 0xD3DA65E82BDD39E2ULL, 0x48F6214560239B46ULL }},
				polynomial_type{{ 0xF1267BA0EC3C645EULL, 0xD9DC0929A54FEA75ULL, 0xEC60B640D685171DULL, 0xDE364EF64A484F59ULL }},
				polynomial_type{{ 0x2761CBAB38E0F580ULL, 0xD7F1C5ADE3DE404AULL, 0xCB6286958A9AF01AULL, 0x2B29C7D3EF18D3B3ULL }},
				polynomial_type{{ 0x5A5CE93F67A3CDD6ULL, 0x547DB3576511EDC2ULL, 0x99455C744595C01FULL, 0x6A3B6A431109E3D1ULL }},
				polynomial_type{{ 0xAFD80C1C832A739EULL, 0x0D9D73DA9F40F374ULL, 0xED1D0A619AA60748ULL, 0x00D2333B0C03F620ULL }},
				polynomial_type{{ 0x11428CEB13F2CC2CULL, 0xEF46E42368BAEAD3ULL, 0x2A47BD3FC39081DAULL, 0x3F03458E0273439BULL }},
				polynomial_type{{ 0x47558E815C898E8BULL, 0x9F8160E9D0124398ULL, 0x0FDCFD4AB0F5AFEEULL, 0xADE2626C292A2A9FULL }},
				polynomial_type{{ 0xE848FF06D72A9252ULL, 0xF8BE2D3D6CE206B0ULL, 0xD84FC5F798C1A55EULL, 0xC35ABE5CEBAB1BA4ULL }},
				polynomial_type{{ 0xB0DD0EDB19AF078CULL, 0xEE1D857A675CA074ULL, 0x60EF7116E6F3C1E0ULL, 0x7C25B2C3282FB730ULL }},
				polynomial_type{{ 0xB51A19064886308AULL, 0x6B590805D407E77EULL, 0x57059D3707EE283AULL, 0x6298F48FA13CC12FULL }},
				polynomial_type{{ 0x4F1102ACB29C3230ULL, 0xCF69CEE6182FA164ULL, 0x1780BE415C86B5D5ULL, 0xAB5D0760D1FE77DCULL }},
				polynomial_type{{ 0xC639B7C24B26EF11ULL, 0xA57D650A8007D505ULL, 0xD81275131F4F91F8ULL, 0x10000E5F7BF7A58BULL }},
				polynomial_type{{ 0x295B23EAA04478EDULL, 0xF1D3279F36823213ULL, 0x743EEDC2EDE6D478ULL, 0x09D89163F581D1E0ULL }}
			}};
			inline static constexpr std::array<polynomial_type, powers_per_half> long_jump_powers = {{
				polynomial_type{{ 0x76E15D3EFEFDCBBFULL, 0xC5004E441C522FB3ULL, 0x77710069854EE241ULL, 0x39109BB02ACBE635ULL }},
				polynomial_type{{ 0x85D1837E6F0CD3FEULL, 0xA4B0488571EDCB9DULL, 0xE9EDB73CB3E9FB7CULL, 0xBA70F1BD97FC40B0ULL }},
				polynomial_type{{ 0xAC54FA504C60E306ULL, 0x0B893C16E4A7F3B3ULL, 0xAFF90EDA09EA8B4CULL, 0x3727C275522644A7ULL }},
				polynomial_type{{ 0x302EDA308643AB47ULL, 0xC9A202B2322BB7F6ULL, 0xD4483FF9A9AC5A23ULL, 0x574E4D0093E3A2E4ULL }},
				polynomial_type{{ 0x261882D92EC8429FULL, 0xABFFFE7AC9EA1612ULL, 0x236417DB3B031424ULL, 0xEC6AA16A8FFC76FAULL }},
				polynomial_type{{ 0x52F6A62700009087ULL, 0xF7C39D8FC76906A3ULL, 0x285943D7FB75D765ULL, 0x88E5349D50F3DDEFULL }},
				polynomial_type{{ 0x3FACC68ED0053AC4ULL, 0xFC0C646FB82AFCEBULL, 0xF055378C576C5C9AULL, 0x21588C86CC534C29ULL }},
				polynomial_type{{ 0xFE596054913ED407ULL, 0x3D38FF4FC965C1FAULL, 0x776751B126655D13ULL, 0x443C1363FD5C7D43ULL }},
				polynomial_type{{ 0x1A672A03C71ADC2EULL, 0x6217B3306E3E9557ULL, 0x163160EFCAD9C046ULL, 0x5243E79672334390ULL }},
				polynomial_type{{ 0x58CE1E7D6EA9281FULL, 0x5348B64C107873B6ULL, 0xDABE97E1DD9A59C1ULL, 0x2DCEC71C419BAA62ULL }},
				polynomial_type{{ 0x955659C7B8793ECFULL, 0x37FAE57370F8BC19ULL, 0xFBA1683B54B1E0F6ULL, 0xE91553475948D23EULL }},
				polynomial_type{{ 0xBB5B5C8AA1AD89E1ULL, 0x9D7C00C8471DDC07ULL, 0xA910BDEFF21CE218ULL, 0x540FCA0570720EB7ULL }},
				polynomial_type{{ 0x0612914F1B46C912ULL, 0x6D8ABCE0CF641CFCULL, 0x32F22FB19AC4550BULL, 0xC4B65C3551C83C69ULL }},
				polynomial_type{{ 0x536E6114E4189CFCULL, 0xBE100596C8DA9541ULL, 0xEE7EB44F2FDBD1B8ULL, 0xB1170D0754BEEAA4ULL }},
				polynomial_type{{ 0xBEB789DBBC4EA209ULL, 0x267D7103EF9F83A3ULL, 0x93F548C2CAB0A32CULL, 0x45CAC579389AF5CAULL }},
				polynomial_type{{ 0x65CEB6CDE220E757ULL, 0xD6F9074A4C2732F7ULL, 0xA8E0425B0D01CD1EULL, 0x2B75C5D185461341ULL }},
				polynomial_type{{ 0xAFBACB099D1967BDULL, 0x1AF87374102C1031ULL, 0x470868184FCC3F5FULL, 0x114DCBB43B155057ULL }},
				polynomial_type{{ 0x5F98E9B5AD62427DULL, 0xF27E722D27743CD9ULL, 0x7EBE95D47CD1DAF2ULL, 0x1B98494373C20B8AULL }},
				polynomial_type{{ 0x8F1D0F5EC26521A6ULL, 0x036E9886F63C9933ULL, 0x4AC6FAB0688E4CCDULL, 0x93D03EEA25D1D816ULL }},
				polynomial_type{{ 0xDD4E745E4412A26AULL, 0xBB62B24404A1BE96ULL, 0x9C227B5BA376FAEEULL, 0x08615908BCC4C8F2ULL }},
				polynomial_type{{ 0xEBE0D315A9CB279BULL, 0xC7A967D45D82BBCAULL, 0x64D85CC844957794ULL, 0xF6A1EF6A7D3B2545ULL }},
				polynomial_type{{ 0x29BFB1BDC678FCBEULL, 0x611E5AEDD44A4FD4ULL, 0xD188547DEB3F0136ULL, 0x2B8DD348E0F767AEULL }},
				polynomial_type{{ 0xFAD25FA87D091580ULL, 0x5154A018EBA8E309ULL, 0xBD9B522FB9F15D0BULL, 0xFCD653BC999D276BULL }},
				polynomial_type{{ 0x29C79A4CEDB3BAF2ULL, 0x946592914B67E34FULL, 0x04921932AAF82150ULL, 0xB36394657868F06EULL }},
				polynomial_type{{ 0x6CBFCD64BF69402CULL, 0xCA9A2B49A6E6B16DULL, 0xBA835279FFB6A358ULL, 0xFBDF21DA0BB9ADD0ULL }},
				polynomial_type{{ 0x23436782D086CA23ULL, 0x0CF66F05D413A46DULL, 0xBB90914A9C9871A3ULL, 0xEDCCE16AEB59E5ADULL }},
				polynomial_type{{ 0x130E23FA572004A9ULL, 0xF9CE20DEC18C4B44ULL, 0x5CEA7B8A1AC11DE9ULL, 0x6608D757C7D36BE3ULL }},
				polynomial_type{{ 0x70C7A48F09B95BB9ULL, 0xD03A1ED309668F2FULL, 0xA955E448A10873D4ULL, 0xD5D4C6699513858FULL }},
				polynomial_type{{ 0x72015CF80CE336F4ULL, 0x619C9D98F6F33BCBULL, 0x59F1B7E5D5FBFDC3ULL, 0x16CAC53FC2905146ULL }},
				polynomial_type{{ 0x5F340FCB5BE19401ULL, 0xCE2129CD34AE493AULL, 0x14690CFA36C329EDULL, 0xC6E96787AEDC5C40ULL }},
				polynomial_type{{ 0x7AD9F632881E960FULL, 0xB8052DCCA0E13395ULL, 0xD457241F6A9863ACULL, 0xF8D2E75E66D53D83ULL }},
				polynomial_type{{ 0x23336699F63C8E45ULL, 0x33B2E33E1D4E5BDBULL, 0x37FDEEE585FDCD8EULL, 0x9A5144DA7F765FD8ULL }}
			}};
		};

		template <>
		struct StreamJumpPowerTable<Xoroshiro128StarStar>
		{
			static constexpr bool enabled = true;
			using engine_type = Xoroshiro128StarStar;
			using word_type = typename engine_type::result_type;
			using polynomial_type = std::array<word_type, std::tuple_size<typename engine_type::state_type>::value>;
			static constexpr std::size_t powers_per_half = std::numeric_limits<std::uint32_t>::digits;
			static_assert(std::tuple_size<typename engine_type::state_type>::value == 2, "stream jump polynomial state width");
			static_assert(std::numeric_limits<word_type>::digits == 64, "stream jump polynomial word width");
			inline static constexpr std::array<polynomial_type, powers_per_half> jump_powers = {{
				polynomial_type{{ 0xDF900294D8F554A5ULL, 0x170865DF4B3201FCULL }},
				polynomial_type{{ 0x2992EAD4972EAED2ULL, 0xB2A7B279A8CB1F50ULL }},
				polynomial_type{{ 0xC026A7D9E04A7700ULL, 0xE7859C665BE57882ULL }},
				polynomial_type{{ 0xB4CB6197DEA2B1FEULL, 0x4B4A7AA8C389701CULL }},
				polynomial_type{{ 0x0DCFC5B909E7DF4DULL, 0xADB7753D55646EEFULL }},
				polynomial_type{{ 0x468431669864F789ULL, 0xC80926301806A352ULL }},
				polynomial_type{{ 0x22B6C1736285FCC8ULL, 0xC05DA051EC96AF1DULL }},
				polynomial_type{{ 0x74C1DAAC8729D8BBULL, 0xF88F6BAC8FD30448ULL }},
				polynomial_type{{ 0x847757C126B23E45ULL, 0x752B98D002C408F7ULL }},
				polynomial_type{{ 0x0F9EAA62D0C9E2A3ULL, 0x1AA7BC96DBACE110ULL }},
				polynomial_type{{ 0x7475D71B98314377ULL, 0xC469B29353A4984BULL }},
				polynomial_type{{ 0xBBB7D266D61C85EAULL, 0x4B6DD41BCE3BB499ULL }},
				polynomial_type{{ 0xC419B3742570E16FULL, 0xE023777E70B3A2F8ULL }},
				polynomial_type{{ 0x2A71DB3A3CE8B968ULL, 0x131E94FB35203D80ULL }},
				polynomial_type{{ 0x2897BB8961B4DCE9ULL, 0x9240C95B1E7FA08BULL }},
				polynomial_type{{ 0xF0FC3553D7881D5FULL, 0xB879FCA0915F893FULL }},
				polynomial_type{{ 0xE754DB3FBC7536BCULL, 0x2ADCA86FBEFE1366ULL }},
				polynomial_type{{ 0x0A9E201ADFE7BAA9ULL, 0x0A40A688D77855BAULL }},
				polynomial_type{{ 0x1D0D601E49C35837ULL, 0x17771C905E0775A8ULL }},
				polynomial_type{{ 0x9B031395AEC7B584ULL, 0x2CF775E419A607E0ULL }},
				polynomial_type{{ 0x79EAD2EEDDF66699ULL, 0x93A7CF27DEC9B306ULL }},
				polynomial_type{{ 0xE1B9805C107679FCULL, 0x93615189FE85B7D5ULL }},
				polynomial_type{{ 0x2C3925DCD790E3D6ULL, 0x466421124B50FBFBULL }},
				polynomial_type{{ 0xDCA9B0FA4E95600EULL, 0x1CDA7BD04E3BB94BULL }},
				polynomial_type{{ 0xEFC7905E1CBB5FFBULL, 0x5EC431D73BBFE49FULL }},
				polynomial_type{{ 0x854414811D534483ULL, 0x31A1F85FD532F302ULL }},
				polynomial_type{{ 0xADB9BA2958F30B6EULL, 0xED9B991C09177E2FULL }},
				polynomial_type{{ 0x76F8FDF26B0D1CBBULL, 0x38D9E87DFFDFCA70ULL }},
				polynomial_type{{ 0x51F21CDDCEBDB8C7ULL, 0xD8E9E7254052AF4DULL }},
				polynomial_type{{ 0xA03F796EFB295305ULL, 0x62769780D13FBC08ULL }},
				polynomial_type{{ 0x4F2083F6B19E628AULL, 0x66E5456C2EAEDBFFULL }},
				polynomial_type{{ 0x8B2BE9CD79734BEDULL, 0xACE8D6CE8E3FBA17ULL }}
			}};
			inline static constexpr std::array<polynomial_type, powers_per_half> long_jump_powers = {{
				polynomial_type{{ 0xD2A98B26625EEE7BULL, 0xDDDF9B1090AA7AC1ULL }},
				polynomial_type{{ 0x4FFF128094EDD94CULL, 0x00D67DC46AD28695ULL }},
				polynomial_type{{ 0x726438E9A1D3C6EAULL, 0xF9540570703E7CF3ULL }},
				polynomial_type{{ 0x92CC6A0937C9D34EULL, 0x066A9599766619B5ULL }},
				polynomial_type{{ 0xC5730DE058E1047FULL, 0xA4E540C7AC49AA1BULL }},
				polynomial_type{{ 0xE408BBECDA066551ULL, 0xC2EDFC1AB51C00ADULL }},
				polynomial_type{{ 0xC5477EA8821CE588ULL, 0xF11753A4339E78C3ULL }},
				polynomial_type{{ 0x3C6058E633063180ULL, 0xBB42E906EFB12540ULL }},
				polynomial_type{{ 0xBEC40E0518086E21ULL, 0x4E86F36C495EEEDBULL }},
				polynomial_type{{ 0x465276434FD98954ULL, 0xE8345A7C487FEFD6ULL }},
				polynomial_type{{ 0x3ADAEA5CDFE12E3BULL, 0x688B762874221434ULL }},
				polynomial_type{{ 0xC9DFFA95904E99B1ULL, 0x833801923A05F253ULL }},
				polynomial_type{{ 0xA10C3FB0B18DF787ULL, 0x58A00D23A8086646ULL }},
				polynomial_type{{ 0xA4E41F760281C3D0ULL, 0xEC69708D487DBFC4ULL }},
				polynomial_type{{ 0xB8880FFF0E41261CULL, 0x47176F17DE7FF0E9ULL }},
				polynomial_type{{ 0x58EE3B30F542767EULL, 0x4F40C533643920EAULL }},
				polynomial_type{{ 0x15F2D25B60C5ACD7ULL, 0x83FD48D6B9620584ULL }},
				polynomial_type{{ 0xE448C83950A687EAULL, 0x0CE303C7D3AABBC8ULL }},
				polynomial_type{{ 0xA6FF7863C363CFD4ULL, 0x1746715DF0DD8FE3ULL }},
				polynomial_type{{ 0x7E9D8517B195D9C9ULL, 0xC00185964CAEF8BBULL }},
				polynomial_type{{ 0x40DDB4DAF3FBDDA8ULL, 0xB6BDE02BD004B144ULL }},
				polynomial_type{{ 0x7A794B820672A49BULL, 0xBA43C63EC5A9F187ULL }},
				polynomial_type{{ 0xC1BE31E7536236FBULL, 0x2467071B1D261621ULL }},
				polynomial_type{{ 0xF0EEC34DAEA486FBULL, 0x5A6FC0435F011DAAULL }},
				polynomial_type{{ 0xF42C01A2A3815DB4ULL, 0xA5AF34331C044D81ULL }},
				polynomial_type{{ 0xDF7964C343B312DEULL, 0xDB43B553CD16EA44ULL }},
				polynomial_type{{ 0x8454182464C29903ULL, 0x432C2BBCD03E65F6ULL }},
				polynomial_type{{ 0x7B6C0ECC6CB5ADBBULL, 0xCDF56412D1E7BA6EULL }},
				polynomial_type{{ 0x380B97764C9F7748ULL, 0xAC13C8B2FF838036ULL }},
				polynomial_type{{ 0x1868A9F5A4FD4D64ULL, 0x71D208CC2E5C56E9ULL }},
				polynomial_type{{ 0xE89F5FE075D74A79ULL, 0xD1D08A01B73DE005ULL }},
				polynomial_type{{ 0x25AA87F3C2704C69ULL, 0xA9495C12936AD0FDULL }}
			}};
		};

		template <>
		struct StreamJumpPowerTable<Xoshiro128StarStar>
		{
			static constexpr bool enabled = true;
			using engine_type = Xoshiro128StarStar;
			using word_type = typename engine_type::result_type;
			using polynomial_type = std::array<word_type, std::tuple_size<typename engine_type::state_type>::value>;
			static constexpr std::size_t powers_per_half = std::numeric_limits<std::uint32_t>::digits;
			static_assert(std::tuple_size<typename engine_type::state_type>::value == 4, "stream jump polynomial state width");
			static_assert(std::numeric_limits<word_type>::digits == 32, "stream jump polynomial word width");
			inline static constexpr std::array<polynomial_type, powers_per_half> jump_powers = {{
				polynomial_type{{ 0x8764000BU, 0xF542D2D3U, 0x6FA035C3U, 0x77F2DB5BU }},
				polynomial_type{{ 0x9B802A8BU, 0x794805EDU, 0x5EB170F0U, 0x7C0F7916U }},
				polynomial_type{{ 0x1A235895U, 0x008078D6U, 0x18ECA90EU, 0x5F292782U }},
				polynomial_type{{ 0xF70585FBU, 0x4E0C5957U, 0xBCE250C3U, 0x17A896FFU }},
				polynomial_type{{ 0xD2F6556FU, 0x4A18286DU, 0x3628D30BU, 0x55160319U }},
				polynomial_type{{ 0x7A7FAF9AU, 0xA16BBAFDU, 0x0E0CE4FBU, 0x3C7D15DEU }},
				polynomial_type{{ 0xF28E46EBU, 0x5DE8D870U, 0x99C73881U, 0x138475D2U }},
				polynomial_type{{ 0x606A7785U, 0x20E6D45FU, 0x1B647514U, 0x86EB7CA9U }},
				polynomial_type{{ 0x49666ECCU, 0x3789D8A5U, 0x6A660A93U, 0xD71038C4U }},
				polynomial_type{{ 0x5128E049U, 0x57728E18U, 0x914D8F82U, 0x770B4AAEU }},
				polynomial_type{{ 0xF4C220B9U, 0x204509E7U, 0xF72ABAA8U, 0x87A9BA17U }},
				polynomial_type{{ 0xA770745CU, 0x6305AEB1U, 0x514FB641U, 0x53F14381U }},
				polynomial_type{{ 0xEF0C0748U, 0x37C6BFD3U, 0xCE823C5FU, 0x614B1BE8U }},
				polynomial_type{{ 0xA7598B6EU, 0x56ACC333U, 0x7616ABEBU, 0x444C7482U }},
				polynomial_type{{ 0x3B8E5872U, 0x95B59666U, 0x250A934EU, 0xE1C8CD14U }},
				polynomial_type{{ 0x61AF734BU, 0xCAFB7BEFU, 0x40320995U, 0x52C3FEFDU }},
				polynomial_type{{ 0x1E448B65U, 0x3D04F456U, 0x0065B6C1U, 0x03EDE698U }},
				polynomial_type{{ 0x999C0C61U, 0x8F514F34U, 0x208AE8A1U, 0xA286055DU }},
				polynomial_type{{ 0xFD77B051U, 0xDC74937CU, 0x87C9CAA7U, 0x87C3B447U }},
				polynomial_type{{ 0x5CB18704U, 0x3861888CU, 0x421E95F0U, 0x84702775U }},
				polynomial_type{{ 0x796E8F1CU, 0x17386578U, 0xA950E8B9U, 0x5122B999U }},
				polynomial_type{{ 0xFD714F38U, 0x6A60580CU, 0x1DE92DC7U, 0x0A378A8DU }},
				polynomial_type{{ 0x920394A9U, 0x59E5F42EU, 0xA82AFDB9U, 0x29EC5ED3U }},
				polynomial_type{{ 0x9D4E636EU, 0x91C22DB3U, 0xF24479F8U, 0xB34270EEU }},
				polynomial_type{{ 0xF610CDC8U, 0x935A2512U, 0xA972EFE6U, 0x866BC548U }},
				polynomial_type{{ 0xF67E06E0U, 0x830FC62FU, 0x426D33F9U, 0x36C311B2U }},
				polynomial_type{{ 0x82E394F4U, 0x8E7AE190U, 0x74DA71B9U, 0x2B8B3AC4U }},
				polynomial_type{{ 0x1B17A73EU, 0x48EC363CU, 0x9F3A8665U, 0x1BA09EC7U }},
				polynomial_type{{ 0x5EEE0D0EU, 0x8A54B514U, 0x268D5B56U, 0x7C53CF77U }},
				polynomial_type{{ 0xECB31E06U, 0x1DEF52D6U, 0x5EC53D4FU, 0xCB831ED8U }},
				polynomial_type{{ 0x196075BFU, 0xC31DB8FBU, 0x2E624B60U, 0xBA7E0917U }},
				polynomial_type{{ 0xF59F8398U, 0x7E8F6A86U, 0xC9BA6AFBU, 0xC28A81EDU }}
			}};
			inline static constexpr std::array<polynomial_type, powers_per_half> long_jump_powers = {{
				polynomial_type{{ 0xB523952EU, 0x0B6F099FU, 0xCCF5A0EFU, 0x1C580662U }},
				polynomial_type{{ 0xEEB0E0A4U, 0x77133E23U, 0xDC596025U, 0x97F55FE2U }},
				polynomial_type{{ 0x9E9B45ACU, 0x6D495900U, 0x69AC41E5U, 0x0356E935U }},
				polynomial_type{{ 0x407883F3U, 0x547D4854U, 0x9065599BU, 0x662B6AC9U }},
				polynomial_type{{ 0x667EE2DEU, 0x8A954D8BU, 0x6551C593U, 0x2FCDF7E4U }},
				polynomial_type{{ 0xFB5707AAU, 0xDAA2886AU, 0xB233CD67U, 0x0F4183CAU }},
				polynomial_type{{ 0x40DBCD63U, 0x8E131A4FU, 0x224FC251U, 0xC64784EEU }},
				polynomial_type{{ 0x4F4DB4FFU, 0x7B6EA15FU, 0xB29E13B7U, 0x563B1EA7U }},
				polynomial_type{{ 0xBBD3AE5AU, 0xEBF544E9U, 0xD28EC540U, 0x5CE3332FU }},
				polynomial_type{{ 0xD39C61EBU, 0x1F4DD02EU, 0x95A4E90FU, 0xA9AC90E8U }},
				polynomial_type{{ 0x790C846CU, 0xD428B915U, 0xD2660F23U, 0x725DCD70U }},
				polynomial_type{{ 0x08EFF263U, 0xF39FF6C1U, 0x513D8BA0U, 0xCA4404CAU }},
				polynomial_type{{ 0x26534B4DU, 0xCF8DB66BU, 0x6102F64BU, 0xF84F07E3U }},
				polynomial_type{{ 0xA88724C5U, 0x0870D7D7U, 0x181F9787U, 0xDC3D5D45U }},
				polynomial_type{{ 0xDBA73489U, 0x0DF0EC1FU, 0x43005E2EU, 0xD543EDF1U }},
				polynomial_type{{ 0x6D73A1E7U, 0xFE43B2A7U, 0xF9A46A20U, 0x58859A86U }},
				polynomial_type{{ 0xA683B6D0U, 0xAFC4A733U, 0x1BF94979U, 0xF904DD9FU }},
				polynomial_type{{ 0x2EE03D84U, 0x75C74E3DU, 0x96EFBFD6U, 0x7D256F6CU }},
				polynomial_type{{ 0x3AD0EBE7U, 0x13F14F31U, 0x796D291CU, 0xA42BBFDDU }},
				polynomial_type{{ 0xCE04DDB0U, 0x1FC44A96U, 0xB6A00A91U, 0x8A6C4326U }},
				polynomial_type{{ 0x4E519967U, 0x0D7A869EU, 0x40012492U, 0x6DC7C036U }},
				polynomial_type{{ 0x9E4D0A48U, 0x6A86DB67U, 0xAE852B9BU, 0x6CC51CEBU }},
				polynomial_type{{ 0x5A52E97FU, 0x77BEACCEU, 0xB8030B6CU, 0x5EAD7C39U }},
				polynomial_type{{ 0x022CEFBEU, 0x7D88E3D4U, 0x858BBDFEU, 0x6B644146U }},
				polynomial_type{{ 0x90067A45U, 0xB7CE03BCU, 0xDE4AC3E8U, 0x99853A2CU }},
				polynomial_type{{ 0xE3A7CCF3U, 0x35C9B163U, 0xBB5B8048U, 0x31AC55D8U }},
				polynomial_type{{ 0x8D4A33DBU, 0x169E96EFU, 0x3788B4A3U, 0x622CD32EU }},
				polynomial_type{{ 0x0513F190U, 0x06F60339U, 0x93608184U, 0x4576959DU }},
				polynomial_type{{ 0x1A64167BU, 0x05C745C5U, 0xE2F50D3AU, 0x8ABC30FAU }},
				polynomial_type{{ 0x1741BB62U, 0x3AFD4BA4U, 0xB268FAEFU, 0x18BF57C6U }},
				polynomial_type{{ 0x39B7B7B9U, 0x31BB1001U, 0xD95F2DCCU, 0x5686C6E7U }},
				polynomial_type{{ 0x54D81F7EU, 0x0453F0FEU, 0x3BEF4345U, 0x9D5E1791U }}
			}};
		};

	}

namespace detail
{
	template <class Engine, class = void>
	struct StreamEngineHasLongJump : std::false_type {};

	template <class Engine>
	struct StreamEngineHasLongJump<Engine, std::void_t<decltype(std::declval<Engine&>().longJump())>>
		: std::true_type {};

	inline constexpr std::size_t StreamIdHalfBits = std::numeric_limits<std::uint32_t>::digits;
	inline constexpr std::uint64_t StreamIdLowMask = (std::numeric_limits<std::uint32_t>::max)();

	static_assert(StreamIdHalfBits * 2 == std::numeric_limits<std::uint64_t>::digits);

	template <class Engine, class Polynomial>
	constexpr void ApplyStreamJumpPolynomial(Engine& engine, const Polynomial& polynomial) noexcept
	{
		using word_type = typename Polynomial::value_type;
		using state_type = typename Engine::state_type;
		state_type accumulated{};
		auto* accumulatedWords = accumulated.data();
		const std::size_t stateSize = accumulated.size();
		for (std::size_t word = 0; word < polynomial.size(); ++word)
		{
			const word_type coefficient = polynomial.data()[word];
			for (int bit = 0; bit < std::numeric_limits<word_type>::digits; ++bit)
			{
				if ((coefficient & (word_type{1} << bit)) != 0)
				{
					const state_type current = engine.serialize();
					const auto* currentWords = current.data();
					for (std::size_t index = 0; index < stateSize; ++index)
						accumulatedWords[index] ^= currentWords[index];
				}
				(void)engine();
			}
		}
		engine.deserialize(accumulated);
	}

	template <class Engine>
	constexpr void ApplyStreamIdJumps(Engine& engine, std::uint64_t streamId)
	{
		if (streamId == 0)
			return;
		if constexpr (StreamJumpPowerTable<Engine>::enabled)
		{
			using table_type = StreamJumpPowerTable<Engine>;
			const std::uint64_t longJumps = streamId >> StreamIdHalfBits;
			const std::uint64_t shortJumps = streamId & StreamIdLowMask;
			if ((longJumps & std::uint64_t{1}) != 0)
				engine.longJump();
			if (longJumps > std::uint64_t{1})
			{
				for (std::size_t bit = 1; bit < table_type::powers_per_half; ++bit)
				{
					const std::uint64_t mask = std::uint64_t{1} << bit;
					if ((longJumps & mask) != 0)
						ApplyStreamJumpPolynomial(engine, table_type::long_jump_powers[bit]);
				}
			}
			if ((shortJumps & std::uint64_t{1}) != 0)
				engine.jump();
			if (shortJumps > std::uint64_t{1})
			{
				for (std::size_t bit = 1; bit < table_type::powers_per_half; ++bit)
				{
					const std::uint64_t mask = std::uint64_t{1} << bit;
					if ((shortJumps & mask) != 0)
						ApplyStreamJumpPolynomial(engine, table_type::jump_powers[bit]);
				}
			}
		}
		else if constexpr (StreamEngineHasLongJump<Engine>::value)
		{
			const std::uint64_t longJumps = streamId >> StreamIdHalfBits;
			const std::uint64_t shortJumps = streamId & StreamIdLowMask;
			for (std::uint64_t index = 0; index < longJumps; ++index)
				engine.longJump();
			for (std::uint64_t index = 0; index < shortJumps; ++index)
				engine.jump();
		}
		else
		{
			for (std::uint64_t index = 0; index < streamId; ++index)
				engine.jump();
		}
	}

	template <class Engine>
	[[nodiscard]]
	inline constexpr Engine MakeStreamEngineWithId(std::uint64_t streamId, std::uint64_t seed)
	{
		Engine engine{ seed };
		ApplyStreamIdJumps(engine, streamId);
		if constexpr (std::is_constructible<Engine, Engine&&>::value)
			return engine;
		else
			// 被删除的移动构造会阻断右值构造，此时从局部左值复制。
			return static_cast<Engine&>(engine);
	}
}

	////////////////////////////////////////////////////////////////
	//
	//	多流接口（并行计算）
	//

	/// @brief 从同一种子定位流编号对应的引擎起点
	/// @param streamId 流编号；内置引擎先执行高 32 位指定的 longJump，再执行低 32 位指定的 jump
	/// @param seed 共享种子（默认 DefaultSeed）
	/// @return 指定流起点的引擎实例
	/// @note 流的使用预算由所选引擎的 jump 距离决定，参见 API 文档的多流契约
	/// @note Xoroshiro64 系列无 jump 函数，不支持多流
	template <class Engine>
		requires detail::StreamEngine<Engine>
	[[nodiscard]]
	inline constexpr Engine MakeStreamEngine(std::uint64_t streamId, std::uint64_t seed = DefaultSeed)
	{
		return detail::MakeStreamEngineWithId<Engine>(streamId, seed);
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

	/// @brief 编译期生成 [min, max] 范围内的随机整数
	/// @param min 下界（含）
	/// @param max 上界（含）
	/// @return 使用固定种子在编译期确定的随机整数（Lemire 有界法，无模偏差）
	template <std::integral T = int, std::uint64_t Seed = DefaultSeed>
	[[nodiscard]]
	inline constexpr T RandIntCE(T min, T max)
	{
		if (min > max) throw std::invalid_argument("RandIntCE: min > max");
		Xoshiro256StarStar rng{ Seed };
		using U = std::make_unsigned_t<T>;
		const U u_min = static_cast<U>(min);
		const U u_max = static_cast<U>(max);
		const U diff = u_max - u_min;
		if (diff == (std::numeric_limits<U>::max)())
		{
			return static_cast<T>(u_min + static_cast<U>(rng()));
		}
		const auto range = static_cast<std::uint64_t>(diff) + 1;
		return static_cast<T>(u_min + static_cast<U>(detail::BoundedRand(rng, range)));
	}

	/// @brief 编译期生成 [0, max] 范围内的随机整数
	/// @param max 上界（含）
	/// @return 使用固定种子在编译期确定的随机整数
	template <std::integral T = int, std::uint64_t Seed = DefaultSeed>
	[[nodiscard]]
	inline constexpr T RandIntCE(T max)
	{
		return RandIntCE<T, Seed>(T{0}, max);
	}

	////////////////////////////////////////////////////////////////
	//
	//	编译期洗牌（constexpr）
	//

	/// @brief 编译期 Fisher-Yates 洗牌
	/// @tparam It 随机访问迭代器类型
	/// @tparam Seed 编译期随机种子（默认 DefaultSeed；如需不同洗牌结果可传入自定义 Seed 模板参数）
	/// @param first 起始迭代器
	/// @param last 结束迭代器
	/// @note 内部统一使用 Xoshiro256StarStar 在 constexpr 上下文中生成随机排列
	template <std::random_access_iterator It, std::uint64_t Seed = DefaultSeed>
	constexpr void ShuffleCE(It first, It last)
	{
		const auto diff = last - first;
		if (diff < 2) return;
		const auto n = static_cast<std::uint64_t>(diff);
		Xoshiro256StarStar rng{ Seed };
		for (std::uint64_t i = n - 1; i > 0; --i)
		{
			const auto j = detail::BoundedRand(rng, i + 1);
			if (i != j)
			{
				std::ranges::iter_swap(first + i, first + j);
			}
		}
	}

	/// @brief 编译期洗牌数组版本（返回打乱后的副本）
	/// @tparam T 数组元素类型
	/// @tparam N 数组长度
	/// @tparam Seed 编译期随机种子（默认 DefaultSeed）
	/// @param arr 待打乱的数组
	/// @return 打乱后的数组副本
	template <class T, std::size_t N, std::uint64_t Seed = DefaultSeed>
	[[nodiscard]]
	constexpr std::array<T, N> ShuffledArray(std::array<T, N> arr)
	{
		ShuffleCE<decltype(arr.begin()), Seed>(arr.begin(), arr.end());
		return arr;
	}

	/// @}

	////////////////////////////////////////////////////////////////
	//
	//	静态断言：确认引擎满足 uniform_random_bit_generator 概念
	//

	static_assert(std::uniform_random_bit_generator<SplitMix64>);
	static_assert(std::uniform_random_bit_generator<Xoshiro256StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro128StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoshiro128StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro64StarStar>);
	static_assert(std::uniform_random_bit_generator<SFC64>);
	static_assert(std::uniform_random_bit_generator<RomuDuoJr>);
	static_assert(std::uniform_random_bit_generator<ChaCha20>);

	/// @defgroup ranges Ranges 风格 API
	/// @brief 接受整个 range 对象而非迭代器对，与 STL ranges 算法无缝组合（C++23 专属）
	/// @{

	namespace ranges
	{
		/// @brief 随机选取一个元素并返回元素值拷贝
		/// @param r 源 range（需满足 sized_range 或 forward_range）
		/// @return 随机选取的元素值拷贝
		/// @note 随机访问迭代器入口返回迭代器，输入迭代器入口与 ranges 入口返回元素值。
		template <std::ranges::input_range R>
			requires (std::ranges::sized_range<R> || std::ranges::forward_range<R>)
				&& std::copy_constructible<std::ranges::range_value_t<R>>
				&& ((std::random_access_iterator<std::ranges::iterator_t<R>>
					&& std::sized_sentinel_for<std::ranges::sentinel_t<R>, std::ranges::iterator_t<R>>)
					|| std::is_copy_assignable_v<std::ranges::range_value_t<R>>)
		[[nodiscard]]
		inline std::ranges::range_value_t<R>
		RandElement(R&& r)
		{
			using I = std::ranges::iterator_t<R>;
			using S = std::ranges::sentinel_t<R>;
			if constexpr (std::random_access_iterator<I> && std::sized_sentinel_for<S, I>)
				return *RandX::RandElement(std::ranges::begin(r), std::ranges::end(r));
			else
				return RandX::RandElement(std::ranges::begin(r), std::ranges::end(r));
		}

		/// @brief 随机选取一个元素（指定引擎重载）
		/// @param engine 自定义随机数引擎
		/// @param r 源 range（需满足 sized_range 或 forward_range）
		/// @return 随机选取的元素值拷贝
		template <detail::RandomEngine Engine, std::ranges::input_range R>
			requires (std::ranges::sized_range<R> || std::ranges::forward_range<R>)
				&& std::copy_constructible<std::ranges::range_value_t<R>>
				&& ((std::random_access_iterator<std::ranges::iterator_t<R>>
					&& std::sized_sentinel_for<std::ranges::sentinel_t<R>, std::ranges::iterator_t<R>>)
					|| std::is_copy_assignable_v<std::ranges::range_value_t<R>>)
		[[nodiscard]]
		inline std::ranges::range_value_t<R>
		RandElement(Engine& engine, R&& r)
		{
			using I = std::ranges::iterator_t<R>;
			using S = std::ranges::sentinel_t<R>;
			if constexpr (std::random_access_iterator<I> && std::sized_sentinel_for<S, I>)
				return *RandX::RandElement(engine, std::ranges::begin(r), std::ranges::end(r));
			else
				return RandX::RandElement(engine, std::ranges::begin(r), std::ranges::end(r));
		}

		/// @brief 无放回抽样（复用迭代器版实现，自动选择 random_access / input 路径）
		/// @note 输入迭代器路径要求元素可复制赋值
		/// @param r 源 range
		/// @param n 抽取数量
		/// @return 含 n 个随机选取元素的 vector
		template <std::ranges::input_range R>
			requires std::copy_constructible<std::ranges::range_value_t<R>>
				&& (std::ranges::random_access_range<R>
					|| std::is_copy_assignable_v<std::ranges::range_value_t<R>>)
		[[nodiscard]]
		inline std::vector<std::ranges::range_value_t<R>>
		RandSample(R&& r, std::ranges::range_difference_t<R> n)
		{
			return RandX::RandSample(std::ranges::begin(r), std::ranges::end(r), n);
		}

		/// @brief 随机打乱 range（要求 random_access + sized + permutable）
		/// @param r 待打乱的 range
		template <std::ranges::random_access_range R>
			requires std::ranges::sized_range<R> && std::permutable<std::ranges::iterator_t<R>>
		inline void
		RandShuffle(R&& r)
		{
			std::ranges::shuffle(r, RandX::DefaultEngine());
		}

		/// @brief 用随机数填充 range
		/// @param r 目标 range
		/// @param min 随机数下界
		/// @param max 随机数上界
		/// @note T 从 min/max 推导：RandFill(v, 0, 99) -> T=int; RandFill(v, 0.0, 1.0) -> T=double
		template <class T, std::ranges::output_range<const T&> R>
		inline void
		RandFill(R&& r, T min, T max)
		{
			RandX::RandFill(std::ranges::begin(r), std::ranges::end(r), min, max);
		}
	}

	/// @}

}
