	template <std::same_as<std::uint32_t> Uint32>
	inline constexpr float FloatFromBits(const Uint32 i) noexcept
// @randx-include ../common/convenience/float_from_bits_body.inc

	template <std::same_as<std::uint64_t> Uint64>
	inline constexpr double DoubleFromBits(const Uint64 i) noexcept
// @randx-include ../common/convenience/double_from_bits_body.inc

	namespace detail
	{
		// 安全擦除后端及兼容实现
// @randx-include ../common/security/secure_wipe_portable.inc
// @randx-include ../common/security/secure_wipe_definition.inc

		// RAII 敏感内存擦除守卫（覆盖异常展开路径）
// @randx-include ../common/security/scoped_wiper_definition.inc

// @randx-include ../common/security/hardware_rand64_definition.inc

// @randx-include ../common/security/os_entropy_reader.inc

// @randx-include ../common/security/entropy_test_hooks.inc

// @randx-include ../common/security/fill_os_entropy_definition.inc

// @randx-include ../common/security/get_os_entropy_bytes_definition.inc

// @randx-include ../common/security/has_crypto_grade_os_entropy_definition.inc

// @randx-include ../common/security/chacha20_constants_and_quarter_round.inc

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
		// @randx-include ../common/distributions/normalize_beta_sample_body.inc

		template <RandomEngine Engine>
		[[nodiscard]]
		inline std::uint64_t Generate64Bits(Engine& engine)
// @randx-include ../common/distributions/generate64_bits_body.inc

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

// @randx-include ../common/serialization/stream_format_guard_definition.inc
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
// @randx-include ../common/serialization/stream_write_body.inc

	// 流式恢复引擎状态
	// 若解析失败（读取不足、流错误或状态非法/全零），setstate(failbit) 且引擎状态保持不变
	// （与 std::random_engine 一致：先读取到临时 state，全部成功才 deserialize）
	template <class CharT, class Traits, detail::SerializableEngine Engine>
	std::basic_istream<CharT, Traits>&
	operator>>(std::basic_istream<CharT, Traits>& is, Engine& engine)
// @randx-include ../common/serialization/stream_read_body.inc
