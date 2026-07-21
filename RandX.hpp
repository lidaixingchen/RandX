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
//		ChaCha20               64-bit  无周期      56B    密码学安全 CSPRNG（RFC 7539）
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
# include <limits>
# include <concepts>
# include <random>
# include <algorithm>
# include <cassert>
# include <type_traits>
# include <ranges>
# include <string>
# include <string_view>
# include <unordered_set>
# include <vector>
# include <stdexcept>
# include <chrono>    // std::chrono（RandomSeed 时间戳兜底用）
# include <ios>       // std::ios_base::failbit（流状态标志完整定义）
# include <istream>   // std::basic_istream（operator>> 所需完整类型）
# include <ostream>   // std::basic_ostream（operator<< 所需完整类型）
# if defined(_MSC_VER) && (defined(__x86_64__) || defined(_M_X64))
#	include <immintrin.h>
# endif
// ── A3 跨平台 OS 熵源头文件（条件包含） ──
# if __has_include(<bcrypt.h>)
// MinGW 的 <bcrypt.h> 依赖 <windows.h> 提供的 ULONG/NTSTATUS 等类型
#	if defined(__MINGW32__) || defined(__MINGW64__)
#		ifndef WIN32_LEAN_AND_MEAN
#			define WIN32_LEAN_AND_MEAN
#		endif
#		include <windows.h>
#	endif
#	include <bcrypt.h>
#	pragma comment(lib, "bcrypt.lib")
# elif __has_include(<sys/random.h>)
#	include <sys/random.h>
#	include <cerrno>
# elif __has_include(<Security/Security.h>)
#	include <Security/Security.h>
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

		friend auto operator <=>(const SplitMix64&, const SplitMix64&) = default;
	
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
		explicit constexpr Xoshiro256StarStar(SeedSeq& seq);

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
		explicit constexpr Xoroshiro128StarStar(SeedSeq& seq);

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
		explicit constexpr Xoshiro128StarStar(SeedSeq& seq);

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
		explicit constexpr Xoroshiro64StarStar(SeedSeq& seq);

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

		friend auto operator <=>(const RomuDuoJr&, const RomuDuoJr&) = default;

	private:

		std::uint64_t m_x;
		std::uint64_t m_y;
	};

	// ChaCha20 (RFC 7539) — 密码学安全 PRNG (CSPRNG)
	// 输出：64 位（每次 operator() 返回 8 字节，一个 block 服务 8 次调用）
	// 状态：key(256-bit) + counter(32-bit) + nonce(96-bit)，自动 reseed（2^20 字节阈值）提供前向安全
	// 安全边界：不提供 serialize/deserialize/jump（状态导出违背 CSPRNG 安全模型）
	// 线程安全：非线程安全（与库内其他引擎一致，每线程持有独立实例）
	class ChaCha20
	{
	public:

		using result_type = std::uint64_t;

		// 构造方式 1：从 OS 熵自动播种（密码学安全，默认）
		ChaCha20();

		// 构造方式 2：显式种子（仅测试/复现，非密码学安全：种子空间仅 64-bit）
		[[nodiscard]]
		explicit ChaCha20(std::uint64_t seed);

		// 构造方式 3：直接指定 key + nonce + counter（高级用法/测试复现）
		// key 须为 32 字节，nonce 须为 12 字节，否则 throw std::invalid_argument
		// counter 为 32-bit block 计数器初值（默认 0；KAT 测试时显式传 1）
		// 注：此构造路径不调用 SecureRandomBytes，调用方须自行保证 key/nonce 的熵源
		ChaCha20(const std::uint8_t* key, std::size_t keyLen,
		         const std::uint8_t* nonce, std::size_t nonceLen,
		         std::uint32_t counter = 0);

		result_type operator()();

		void discard(unsigned long long n);

		// 从 OS 熵重新播种（手动触发，重置 counter 与字节缓存）
		void reseed();

		[[nodiscard]]
		static constexpr result_type min() noexcept { return 0; }

		[[nodiscard]]
		static constexpr result_type max() noexcept { return UINT64_MAX; }

		// 不提供：serialize/deserialize, operator<</>>, jump/longJump（CSPRNG 安全约束）

	private:

		std::array<std::uint32_t, 12> m_state;   // key(8) + counter(1) + nonce(3)，常数省略（generateBlock 时补齐）
		std::array<std::uint8_t, 64>  m_buffer;  // 当前 block 的字节缓存
		std::size_t                   m_bufferPos;       // 缓存消费位置 [0, 64)，==64 时触发新 block
		std::uint64_t                 m_bytesSinceReseed; // 自上次 reseed 以来输出的字节数

		void generateBlock();        // 跑一次 ChaCha20 block 函数填充 m_buffer
		void reseedIfNecessary();    // m_bytesSinceReseed >= 阈值时自动 reseed
	};
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

	// ── A3 跨平台 OS 密码学熵源 ──
	// 用 OS 密码学 API 填充 [buf, buf+n) 字节；成功返回 true。
	// 平台优先级：Windows BCryptGenRandom → Linux getrandom → macOS SecRandomCopyBytes → std::random_device 兜底
	// 注：getrandom 可能短读，内部循环直至填满；BCryptGenRandom/SecRandomCopyBytes 一次填满
	[[nodiscard]]
	inline bool GetOsEntropyBytes(void* buf, std::size_t n) noexcept
	{
		if (n == 0) return true;
		auto* p = static_cast<std::uint8_t*>(buf);

#	if __has_include(<bcrypt.h>)
		// Windows: BCryptGenRandom（一次调用填满）
		return (::BCryptGenRandom(nullptr, p, static_cast<ULONG>(n),
		                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0);

#	elif __has_include(<sys/random.h>)
		// Linux: getrandom（循环处理短读与 EINTR）
		std::size_t filled = 0;
		while (filled < n)
		{
			const long ret = ::getrandom(p + filled, n - filled, 0);
			if (ret < 0)
			{
				if (errno == EINTR) continue;  // 被信号打断，重试
				return false;                   // ENOSYS/EFAULT 等不可恢复错误
			}
			if (ret == 0) return false;
			filled += static_cast<std::size_t>(ret);
		}
		return true;

#	elif __has_include(<Security/Security.h>)
		// macOS: SecRandomCopyBytes（一次调用填满）
		return (::SecRandomCopyBytes(kSecRandomDefault, n, p) == errSecSuccess);

#	else
		// 回退：std::random_device（标准未保证密码学安全）
		try
		{
			std::random_device rd;
			std::size_t filled = 0;
			while (filled < n)
			{
				const auto r = rd();
				const std::size_t chunk = (std::min)(sizeof(r), n - filled);
				std::memcpy(p + filled, &r, chunk);
				filled += chunk;
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
#	endif
	}

	// 返回 true 当且仅当编译期检测到 OS 密码学熵源 API（BCryptGenRandom/getrandom/SecRandomCopyBytes）
	// 返回 false 表示当前运行在 std::random_device 兜底路径，ChaCha20() 默认构造不保证密码学安全
	[[nodiscard]]
	inline bool HasCryptoGradeOsEntropy() noexcept
	{
#	if __has_include(<bcrypt.h>) || __has_include(<sys/random.h>) || __has_include(<Security/Security.h>)
		return true;
#	else
		return false;
#	endif
	}

	// ── A4 ChaCha20 常数与辅助 ──
	// ChaCha20 常数 "expand 32-byte k"（RFC 7539 §2.3）
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

	// 迭代器可填充约束（RandFill 用）
	template <class It, class T>
	concept RandFillable = std::output_iterator<It, T>
		&& (std::integral<T> || std::floating_point<T>);

	}

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
		auto state = engine.serialize();
		for (std::size_t i = 0; i < state.size(); ++i)
		{
			if (i != 0) os << ' ';
			os << state[i];
		}
		return os;
	}

	// 流式恢复引擎状态
	// 若解析失败（读取不足或流错误），setstate(failbit) 且引擎状态保持不变
	// （与 std::random_engine 一致：先读取到临时 state，全部成功才 deserialize）
	template <class CharT, class Traits, detail::SerializableEngine Engine>
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

	////////////////////////////////////////////////////////////////
	//
	//	SplitMix64
	//
	inline constexpr SplitMix64::SplitMix64(const state_type state) noexcept
		: m_state(state) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, SplitMix64>)
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
	inline constexpr Xoshiro256StarStar::Xoshiro256StarStar(const std::uint64_t seed) noexcept
		: m_state(SplitMix64{ seed }.generateSeedSequence<4>()) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoshiro256StarStar>)
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
	//	xoroshiro128**
	//
	inline constexpr Xoroshiro128StarStar::Xoroshiro128StarStar(const std::uint64_t seed) noexcept
		: m_state(SplitMix64{ seed }.generateSeedSequence<2>()) {}

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, Xoroshiro128StarStar>)
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

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, SFC64>)
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

	template <class SeedSeq>
		requires (!std::same_as<std::remove_cvref_t<SeedSeq>, RomuDuoJr>)
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
			// 最终兜底：时间戳（非密码学，仅保证 RandomSeed 永不抛异常）
			return static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
		}
	}

	// ── A3 密码学安全熵源公开 API ──

	// 用 OS 密码学熵源填充 [buf, buf+n) 字节
	// 失败抛 std::runtime_error（CSPRNG 无熵不可用 = 致命错误）
	inline void SecureRandomBytes(void* buf, std::size_t n)
	{
		if (n == 0) return;
		if (!detail::GetOsEntropyBytes(buf, n))
			throw std::runtime_error("SecureRandomBytes: OS entropy source unavailable");
	}

	// 密码学安全种子（复用 SecureRandomBytes，取前 8 字节）
	[[nodiscard]]
	inline std::uint64_t SecureSeed()
	{
		std::uint64_t seed;
		SecureRandomBytes(&seed, sizeof(seed));
		return seed;
	}

	// 返回 true 当且仅当 OS 密码学熵源（BCryptGenRandom/getrandom/SecRandomCopyBytes）可用；
	// 返回 false 表示当前运行在 std::random_device 兜底路径，不保证密码学安全
	[[nodiscard]]
	inline bool IsOsCryptoEntropyAvailable() noexcept
	{
		return detail::HasCryptoGradeOsEntropy();
	}

	////////////////////////////////////////////////////////////////
	//
	//	ChaCha20 (RFC 7539) — CSPRNG 引擎实现
	//
	//  状态矩阵布局（16 × uint32，常数省略存于 m_state[0..11]）：
	//    0  1  2  3      "expa"  "nd 3"  "2-by"  "te k"   ← 常数（generateBlock 时补齐）
	//    4  5  6  7      key[0]  key[1]  key[2]  key[3]   ← m_state[0..3]
	//    8  9 10 11      key[4]  key[5]  key[6]  key[7]   ← m_state[4..7]
	//   12 13 14 15      ctr    nonce[0] nonce[1] nonce[2]← m_state[8..11]
	//
	//  生成流程：operator() → reseedIfNecessary → (缓存耗尽时)generateBlock → 取 8 字节
	//

	// 构造方式 1：从 OS 熵自动播种（密码学安全，默认）
	inline ChaCha20::ChaCha20()
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0)
	{
		reseed();  // 从 OS 熵获取 key + nonce，重置 counter
	}

	// 构造方式 2：显式种子（仅测试/复现，非密码学安全）
	// 用 SplitMix64 将 64-bit 种子扩展为 32 字节 key + 12 字节 nonce
	inline ChaCha20::ChaCha20(const std::uint64_t seed)
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0)
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
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0)
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
		if (m_bytesSinceReseed >= detail::ChaCha20ReseedThreshold)
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

	// 生成 [min, max] 范围内的随机字符
	// 内部用 int64_t 避免 char32_t 范围（最大 0xFFFFFFFF）溢出 int32_t
	template <detail::Character CharT>
	[[nodiscard]]
	inline CharT RandChar(CharT min, CharT max)
	{
		std::uniform_int_distribution<std::int64_t> dist(
			static_cast<std::int64_t>(min),
			static_cast<std::int64_t>(max));
		return static_cast<CharT>(dist(DefaultEngine()));
	}

	// 生成 [CharT{}, max] 范围内的随机字符
	template <detail::Character CharT = char>
	[[nodiscard]]
	inline CharT RandChar(CharT max)
	{
		return RandChar<CharT>(CharT{}, max);
	}

	// 指定引擎的重载
	template <detail::Character CharT, class Engine>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT min, CharT max)
	{
		std::uniform_int_distribution<std::int64_t> dist(
			static_cast<std::int64_t>(min),
			static_cast<std::int64_t>(max));
		return static_cast<CharT>(dist(engine));
	}

	template <detail::Character CharT = char, class Engine>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT max)
	{
		return RandChar<CharT>(engine, CharT{}, max);
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

	// 迭代器版 RandElement —— 随机访问迭代器：O(1) 直接定位
	template <std::random_access_iterator It>
	[[nodiscard]]
	inline It RandElement(It first, It last)
	{
		using Diff = std::iter_difference_t<It>;
		const Diff n = std::distance(first, last);
		if (n <= 0)
			throw std::invalid_argument("RandElement: empty range");
		return std::next(first, RandInt<Diff>(Diff{0}, n - 1));
	}

	// 迭代器版 RandElement —— 输入迭代器（非 random_access）：O(n) reservoir sampling
	template <std::input_iterator It>
		requires (!std::random_access_iterator<It>)
	[[nodiscard]]
	inline It RandElement(It first, It last)
	{
		if (first == last)
			throw std::invalid_argument("RandElement: empty range");
		It selected = first;
		++first;
		for (std::iter_difference_t<It> i = 1; first != last; ++first, ++i)
		{
			if (RandInt<std::iter_difference_t<It>>(0, i) == 0)
				selected = first;
		}
		return selected;
	}

	// 引擎重载 —— 随机访问迭代器
	template <std::random_access_iterator It, class Engine>
	[[nodiscard]]
	inline It RandElement(Engine& engine, It first, It last)
	{
		using Diff = std::iter_difference_t<It>;
		const Diff n = std::distance(first, last);
		if (n <= 0)
			throw std::invalid_argument("RandElement: empty range");
		return std::next(first, RandInt<Diff>(engine, Diff{0}, n - 1));
	}

	// 引擎重载 —— 输入迭代器（非 random_access）
	template <std::input_iterator It, class Engine>
		requires (!std::random_access_iterator<It>)
	[[nodiscard]]
	inline It RandElement(Engine& engine, It first, It last)
	{
		if (first == last)
			throw std::invalid_argument("RandElement: empty range");
		It selected = first;
		++first;
		for (std::iter_difference_t<It> i = 1; first != last; ++first, ++i)
		{
			if (RandInt<std::iter_difference_t<It>>(engine, std::iter_difference_t<It>{0}, i) == 0)
				selected = first;
		}
		return selected;
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

	// 用 [min, max] 范围的随机数填充迭代器区间
	// 注意：T 从 min/max 推导，不从迭代器 value_type 推导。
	// 若容器元素类型与 min/max 字面量类型不一致，需显式指定 T 或用匹配类型的字面量。
	// 例：RandFill(v.begin(), v.end(), 0.0, 1.0);  // T=double
	//     RandFill<double>(v.begin(), v.end(), 0, 1);  // 显式指定 T=double
	template <class It, class T>
		requires detail::RandFillable<It, T>
	inline void RandFill(It first, It last, T min, T max)
	{
		std::uniform_int_distribution<T> dist(min, max);
		for (; first != last; ++first)
			*first = dist(DefaultEngine());
	}

	// RandFill 浮点版特化
	template <class It, std::floating_point T>
		requires std::output_iterator<It, T>
	inline void RandFill(It first, It last, T min, T max)
	{
		std::uniform_real_distribution<T> dist(min, max);
		for (; first != last; ++first)
			*first = dist(DefaultEngine());
	}

	// 引擎重载
	template <class It, class T, class Engine>
		requires detail::RandFillable<It, T>
	inline void RandFill(Engine& engine, It first, It last, T min, T max)
	{
		std::uniform_int_distribution<T> dist(min, max);
		for (; first != last; ++first)
			*first = dist(engine);
	}

	template <class It, std::floating_point T, class Engine>
		requires std::output_iterator<It, T>
	inline void RandFill(Engine& engine, It first, It last, T min, T max)
	{
		std::uniform_real_distribution<T> dist(min, max);
		for (; first != last; ++first)
			*first = dist(engine);
	}

	// 生成含 n 个随机整数的 vector
	template <std::integral T = int>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
	{
		std::vector<T> v;
		v.reserve(n);
		std::uniform_int_distribution<T> dist(min, max);
		auto& engine = DefaultEngine();
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(engine));
		return v;
	}

	// 生成含 n 个随机浮点数的 vector
	template <std::floating_point T = double>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
	{
		std::vector<T> v;
		v.reserve(n);
		std::uniform_real_distribution<T> dist(min, max);
		auto& engine = DefaultEngine();
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(dist(engine));
		return v;
	}

	// 引擎重载
	template <std::integral T = int, class Engine>
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

	template <std::floating_point T = double, class Engine>
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
				return "0123456789abcdefABCDEF";
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

	// 从预设字符集随机取一个字符（固定返回 char：预设集均为 ASCII 范围）
	[[nodiscard]]
	inline char RandChar(CharSet cs)
	{
		const auto charset = detail::CharSetString(cs);
		if (charset.empty())
			throw std::invalid_argument("RandChar: charset is empty");
		auto& rng = DefaultEngine();
		return charset[static_cast<std::size_t>(
			detail::BoundedRand(rng, static_cast<std::uint64_t>(charset.size())))];
	}

	// 引擎重载（用 uniform_int_distribution 支持任意引擎，与 RandChar(Engine&, CharT, CharT) 模式一致）
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
		std::vector<T> pool(c.begin(), c.end());
		const auto size = static_cast<std::uint64_t>(pool.size());
		if (n >= pool.size()) return pool;
		auto& rng = DefaultEngine();
		for (std::uint64_t i = 0; i < n; ++i)
		{
			const auto j = i + detail::BoundedRand(rng, size - i);
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
	template <std::random_access_iterator It>
	[[nodiscard]]
	inline std::vector<std::iter_value_t<It>>
	RandSample(It first, It last, std::iter_difference_t<It> n)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		const Diff size = std::distance(first, last);
		if (n <= 0 || size == 0)
			return {};
		if (n >= size)
			return std::vector<T>(first, last);

		auto& rng = DefaultEngine();

		// 分支选择：n·K < size 时 hash-set 内存优（O(n)）；否则索引数组常数优（O(N)）
		// 用 uint64_t 避免 n*K 溢出（n 是 iter_difference_t，可能 32 位）
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
				const Diff idx = static_cast<Diff>(
					detail::BoundedRand(rng, sizeU));
				if (selected.insert(idx).second)
					result.push_back(first[idx]);
			}
			return result;
		}

		// 索引数组分支：O(N) 内存，O(N) 时间，无碰撞
		std::vector<Diff> indices(static_cast<std::size_t>(size));
		for (Diff i = 0; i < size; ++i)
			indices[static_cast<std::size_t>(i)] = i;

		// Fisher-Yates 前 n 步：j ∈ [i, size)
		for (Diff i = 0; i < n; ++i)
		{
			const Diff j = i + static_cast<Diff>(
				detail::BoundedRand(rng, static_cast<std::uint64_t>(size - i)));
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
	template <std::input_iterator It>
		requires (!std::random_access_iterator<It>)
	[[nodiscard]]
	inline std::vector<std::iter_value_t<It>>
	RandSample(It first, It last, std::iter_difference_t<It> n)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
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
		// 关键：j ∈ [0, i]（闭区间，i+1 个取值），故 BoundedRand(rng, i+1)
		auto& rng = DefaultEngine();
		for (; first != last; ++i, ++first)
		{
			const Diff j = static_cast<Diff>(
				detail::BoundedRand(rng, static_cast<std::uint64_t>(i + 1)));
			if (j < n)
				reservoir[static_cast<std::size_t>(j)] = *first;
		}
		return reservoir;
	}

	// 引擎重载 —— 随机访问迭代器
	template <std::random_access_iterator It, class Engine>
	[[nodiscard]]
	inline std::vector<std::iter_value_t<It>>
	RandSample(Engine& engine, It first, It last, std::iter_difference_t<It> n)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		const Diff size = std::distance(first, last);
		if (n <= 0 || size == 0)
			return {};
		if (n >= size)
			return std::vector<T>(first, last);

		const auto sizeU = static_cast<std::uint64_t>(size);
		// 线性阈值：n·K < size 时用 hash-set（实测交叉点 n≈N/127，K=64 留 2× 裕度）
		if (static_cast<std::uint64_t>(n) * detail::HashSetThresholdK < sizeU)
		{
			// hash-set 分支：用 RandInt 适配任意引擎
			std::unordered_set<Diff> selected;
			selected.reserve(static_cast<std::size_t>(n));
			std::vector<T> result;
			result.reserve(static_cast<std::size_t>(n));
			while (result.size() < static_cast<std::size_t>(n))
			{
				const Diff idx = RandInt<Diff>(engine, Diff{0}, static_cast<Diff>(sizeU - 1));
				if (selected.insert(idx).second)
					result.push_back(first[idx]);
			}
			return result;
		}

		// 索引数组分支：Fisher-Yates 前 n 步，j ∈ [i, size-1]
		std::vector<Diff> indices(static_cast<std::size_t>(size));
		for (Diff i = 0; i < size; ++i)
			indices[static_cast<std::size_t>(i)] = i;

		for (Diff i = 0; i < n; ++i)
		{
			const Diff j = RandInt<Diff>(engine, i, static_cast<Diff>(size - 1));
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
	template <std::input_iterator It, class Engine>
		requires (!std::random_access_iterator<It>)
	[[nodiscard]]
	inline std::vector<std::iter_value_t<It>>
	RandSample(Engine& engine, It first, It last, std::iter_difference_t<It> n)
	{
		using Diff = std::iter_difference_t<It>;
		using T = std::iter_value_t<It>;
		if (n <= 0)
			return {};

		std::vector<T> reservoir;
		reservoir.reserve(static_cast<std::size_t>(n));

		Diff i = 0;
		for (; i < n && first != last; ++i, ++first)
			reservoir.push_back(*first);

		if (first == last)
			return reservoir;

		// Algorithm R：j ∈ [0, i] 闭区间，RandInt(a,b) 是闭区间故上界为 i
		for (; first != last; ++i, ++first)
		{
			const Diff j = RandInt<Diff>(engine, Diff{0}, i);
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
		for (std::uint64_t i = static_cast<std::uint64_t>(n) - 1; i > 0; --i)
		{
			const auto j = detail::BoundedRand(rng, i + 1);
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
		const auto range = static_cast<std::uint64_t>(charset.size());
		for (std::size_t i = 0; i < length; ++i)
			result[i] = charset[detail::BoundedRand(rng, range)];
		return result;
	}

	// 从预设字符集生成随机字符串（委托 string_view 版，零重复代码）
	[[nodiscard]]
	inline std::string RandString(std::size_t n, CharSet cs)
	{
		return RandString(n, detail::CharSetString(cs));
	}

	// 引擎重载：从预设字符集生成随机字符串（用 uniform_int_distribution 支持任意引擎）
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
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandExp(T lambda = T{1})
	{
		std::exponential_distribution<T> dist(lambda);
		return dist(DefaultEngine());
	}

	// 泊松分布随机数（参数 mean）
	template <std::integral T = int>
	[[nodiscard]]
	inline T RandPoisson(double mean = 1.0)
	{
		std::poisson_distribution<T> dist(mean);
		return dist(DefaultEngine());
	}

	// 伽马分布随机数（参数 alpha, beta）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandGamma(T alpha = T{1}, T beta = T{1})
	{
		std::gamma_distribution<T> dist(alpha, beta);
		return dist(DefaultEngine());
	}

	// 二项分布随机数（t 次试验，每次成功概率 p）
	template <std::integral T = int>
	[[nodiscard]]
	inline T RandBinomial(T t = 1, double p = 0.5)
	{
		std::binomial_distribution<T> dist(t, p);
		return dist(DefaultEngine());
	}

	// 二项分布引擎重载
	template <std::integral T = int, class Engine>
	[[nodiscard]]
	inline T RandBinomial(Engine& engine, T t = 1, double p = 0.5)
	{
		std::binomial_distribution<T> dist(t, p);
		return dist(engine);
	}

	// 对数正态分布随机数（参数 mean, stddev）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandLogNormal(T mean = T{0}, T stddev = T{1})
	{
		std::lognormal_distribution<T> dist(mean, stddev);
		return dist(DefaultEngine());
	}

	// 对数正态分布引擎重载
	template <std::floating_point T = double, class Engine>
	[[nodiscard]]
	inline T RandLogNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
	{
		std::lognormal_distribution<T> dist(mean, stddev);
		return dist(engine);
	}

	// 几何分布随机数（首次成功前的失败次数，参数 p）
	template <std::integral T = int>
	[[nodiscard]]
	inline T RandGeometric(double p = 0.5)
	{
		std::geometric_distribution<T> dist(p);
		return dist(DefaultEngine());
	}

	// 几何分布引擎重载
	template <std::integral T = int, class Engine>
	[[nodiscard]]
	inline T RandGeometric(Engine& engine, double p = 0.5)
	{
		std::geometric_distribution<T> dist(p);
		return dist(engine);
	}

	// 柯西分布随机数（位置参数 a，尺度参数 b）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandCauchy(T a = T{0}, T b = T{1})
	{
		std::cauchy_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	// 柯西分布引擎重载
	template <std::floating_point T = double, class Engine>
	[[nodiscard]]
	inline T RandCauchy(Engine& engine, T a = T{0}, T b = T{1})
	{
		std::cauchy_distribution<T> dist(a, b);
		return dist(engine);
	}

	// 韦布尔分布随机数（形状参数 a，尺度参数 b）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandWeibull(T a = T{1}, T b = T{1})
	{
		std::weibull_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	// 韦布尔分布引擎重载
	template <std::floating_point T = double, class Engine>
	[[nodiscard]]
	inline T RandWeibull(Engine& engine, T a = T{1}, T b = T{1})
	{
		std::weibull_distribution<T> dist(a, b);
		return dist(engine);
	}

	// 极值分布（Gumbel）随机数（位置参数 a，尺度参数 b）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandExtremeValue(T a = T{0}, T b = T{1})
	{
		std::extreme_value_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}

	// 极值分布引擎重载
	template <std::floating_point T = double, class Engine>
	[[nodiscard]]
	inline T RandExtremeValue(Engine& engine, T a = T{0}, T b = T{1})
	{
		std::extreme_value_distribution<T> dist(a, b);
		return dist(engine);
	}

	// 卡方分布随机数（自由度 n）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandChiSquared(T n = T{1})
	{
		std::chi_squared_distribution<T> dist(n);
		return dist(DefaultEngine());
	}

	// 卡方分布引擎重载
	template <std::floating_point T = double, class Engine>
	[[nodiscard]]
	inline T RandChiSquared(Engine& engine, T n = T{1})
	{
		std::chi_squared_distribution<T> dist(n);
		return dist(engine);
	}

	// 学生 t 分布随机数（自由度 n）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandStudentT(T n = T{1})
	{
		std::student_t_distribution<T> dist(n);
		return dist(DefaultEngine());
	}

	// 学生 t 分布引擎重载
	template <std::floating_point T = double, class Engine>
	[[nodiscard]]
	inline T RandStudentT(Engine& engine, T n = T{1})
	{
		std::student_t_distribution<T> dist(n);
		return dist(engine);
	}

	// Fisher F 分布随机数（自由度 m, n）
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandFisherF(T m = T{1}, T n = T{1})
	{
		std::fisher_f_distribution<T> dist(m, n);
		return dist(DefaultEngine());
	}

	// Fisher F 分布引擎重载
	template <std::floating_point T = double, class Engine>
	[[nodiscard]]
	inline T RandFisherF(Engine& engine, T m = T{1}, T n = T{1})
	{
		std::fisher_f_distribution<T> dist(m, n);
		return dist(engine);
	}

	// Beta 分布随机数（形状参数 a, b；无 STL，自实现 Gamma(a)/(Gamma(a)+Gamma(b))）
	template <std::floating_point T = double>
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
	template <std::floating_point T = double, class Engine>
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
	template <int N, std::integral T = std::uint64_t>
		requires (N > 0 && N <= 64)
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
			if (i == 8 || i == 13 || i == 18 || i == 23) continue;  // 保留 '-'
			if (i == 14) { uuid[i] = '4'; continue; }  // 版本号
			if (i == 19) { uuid[i] = hex[8 + (rng() & 3)]; continue; }  // 变体位 [89ab]
			uuid[i] = hex[rng() & 15];
		}
		return uuid;
	}

	////////////////////////////////////////////////////////////////
	//
	//	多流接口（并行计算）
	//

	// 从同一种子创建第 streamId 个不重叠子序列的引擎
	// 每个流之间间隔 2^128 步（xoshiro256）或 2^64 步（xoroshiro128/xoshiro128）
	// 注意：Xoroshiro64 系列无 jump 函数，不支持多流
	template <class Engine>
		requires requires(Engine& e) { e.jump(); }
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
		const auto n = static_cast<std::uint64_t>(last - first);
		if (n < 2) return;
		Xoshiro256StarStar rng{ Seed };
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
	static_assert(std::uniform_random_bit_generator<Xoshiro256StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro128StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoshiro128StarStar>);
	static_assert(std::uniform_random_bit_generator<Xoroshiro64StarStar>);
	static_assert(std::uniform_random_bit_generator<SFC64>);
	static_assert(std::uniform_random_bit_generator<RomuDuoJr>);
	static_assert(std::uniform_random_bit_generator<ChaCha20>);

	////////////////////////////////////////////////////////////////
	//
	//	ranges 风格 API（C++23 专属）
	//
	//	接受整个 range 对象而非迭代器对，与 STL ranges 算法
	//	（std::views::filter / std::views::transform）无缝组合。
	//	通过命名空间隔离避免与容器版重载歧义：
	//	  RandX::RandElement(v)         —— 容器版
	//	  RandX::ranges::RandElement(v) —— ranges 版
	//
	namespace ranges
	{
		// 随机选取一个元素（返回值拷贝，非迭代器）
		// 约束：sized_range || forward_range，确保能计算大小或多次遍历
		// 语义差异：迭代器版返回迭代器（可修改原元素），ranges 版返回值拷贝
		template <std::ranges::input_range R>
			requires std::ranges::sized_range<R> || std::ranges::forward_range<R>
		[[nodiscard]]
		inline std::ranges::range_value_t<R>
		RandElement(R&& r)
		{
			return *RandX::RandElement(std::ranges::begin(r), std::ranges::end(r));
		}

		// 无放回抽样（复用迭代器版实现，自动选择 random_access / input 路径）
		template <std::ranges::input_range R>
		[[nodiscard]]
		inline std::vector<std::ranges::range_value_t<R>>
		RandSample(R&& r, std::ranges::range_difference_t<R> n)
		{
			return RandX::RandSample(std::ranges::begin(r), std::ranges::end(r), n);
		}

		// 随机打乱（要求 random_access + sized）
		template <std::ranges::random_access_range R>
			requires std::ranges::sized_range<R>
		inline void
		RandShuffle(R&& r)
		{
			std::ranges::shuffle(r, RandX::DefaultEngine());
		}

		// 填充（模板化，支持任意整型/浮点 T）
		// 调用示例：RandFill(v, 0, 99)  -> T=int
		//           RandFill(v, 0.0, 1.0) -> T=double
		//           RandFill(v, 0.0f, 1.0f) -> T=float
		template <class T, std::ranges::output_range<const T&> R>
		inline void
		RandFill(R&& r, T min, T max)
		{
			RandX::RandFill(std::ranges::begin(r), std::ranges::end(r), min, max);
		}
	}

}
