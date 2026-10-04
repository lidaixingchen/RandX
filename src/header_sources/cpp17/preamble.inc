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
# include <iterator>
# include <cstddef>
# include <stdexcept>
# include <chrono>    // std::chrono（RandomSeed 时间戳兜底用）
# include <atomic>    // std::atomic（RandomSeed 兜底计数）
# include <functional>// std::hash（RandomSeed 线程 Hash）
# include <thread>    // std::this_thread（RandomSeed 线程 ID）
# include <ios>       // std::ios_base::failbit（operator>> 所需）
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
# include <chrono>   // std::chrono（RandomSeed 时间戳兜底用）
# include <cstring>  // std::memcpy（std::random_device 回退路径用）
# if __has_cpp_attribute(nodiscard) >= 201907L
#	define RANDX_NODISCARD_CXX20 [[nodiscard]]
# else
#	define RANDX_NODISCARD_CXX20
# endif
