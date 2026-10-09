	////////////////////////////////////////////////////////////////
	//
	//	SplitMix64
	//
// @randx-include ../common/engines/splitmix64.inc

	template <class SeedSeq,
		std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>
			&& !std::is_same_v<std::decay_t<SeedSeq>, typename SplitMix64::state_type>
			&& !std::is_same_v<std::decay_t<SeedSeq>, SplitMix64>>*>
	inline constexpr SplitMix64::SplitMix64(SeedSeq& seq)
// @randx-include ../common/engines/splitmix64_seed_seq_constructor_body.inc

// @randx-include ../common/engines/splitmix64_shared_tail.inc

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro256**
	//
// @randx-include ../common/engines/xoshiro256_star_star_next_definition.inc

// @randx-include ../common/engines/jump_method_definitions.inc

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro128**
	//
// @randx-include ../common/engines/xoroshiro128_star_star_next_definition.inc

// @randx-include ../common/engines/xoroshiro128_jump_definitions.inc

	////////////////////////////////////////////////////////////////
	//
	//	xoshiro128**
	//
// @randx-include ../common/engines/xoshiro128_star_star_next_definition.inc

// @randx-include ../common/engines/xoshiro128_jump_definitions.inc

	////////////////////////////////////////////////////////////////
	//
	//	xoroshiro64**
	//
// @randx-include ../common/engines/xoroshiro64_star_star_next_definition.inc

	////////////////////////////////////////////////////////////////
	//
	//	SFC64 (Small Fast Counter)
	//
// @randx-include ../common/engines/sfc64_sfc64_definition.inc

	template <class SeedSeq,
		std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>
			&& !std::is_same_v<std::decay_t<SeedSeq>, typename SFC64::state_type>
			&& !std::is_same_v<std::decay_t<SeedSeq>, SFC64>>*>
	inline constexpr SFC64::SFC64(SeedSeq& seq)
		: Base()
// @randx-include ../common/engines/sfc64_seed_seq_constructor_body.inc

// @randx-include ../common/engines/sfc64_next_definition.inc

	////////////////////////////////////////////////////////////////
	//
	//	RomuDuoJr
	//
// @randx-include ../common/engines/romu_duo_jr_next_definition.inc

	////////////////////////////////////////////////////////////////
	//
	//	多流接口（并行计算）
	//

	// 从同一种子定位流起点：内置引擎先执行高 32 位的 longJump，再执行低 32 位的 jump。
	// 流的使用预算由所选引擎的 jump 距离决定，参见 API 文档的多流契约。
	// 注意：Xoroshiro64 系列无 jump 函数，不支持多流
	namespace detail
	{
		template <class Engine, class = void>
		struct HasJump : std::false_type {};
		template <class Engine>
		struct HasJump<Engine, std::void_t<decltype(std::declval<Engine&>().jump())>>
			: std::is_same<void, decltype(std::declval<Engine&>().jump())> {};

		template <class Engine, class = void>
		struct HasStreamSeedConstructor : std::false_type {};
		template <class Engine>
		struct HasStreamSeedConstructor<Engine,
			std::void_t<decltype(Engine{ std::declval<std::uint64_t>() })>> : std::true_type {};

		template <class Engine>
		struct HasStreamEngine : std::bool_constant<
			HasJump<Engine>::value && HasStreamSeedConstructor<Engine>::value> {};

		template <class Engine, class = void>
		struct HasLongJump : std::false_type {};
		template <class Engine>
		struct HasLongJump<Engine, std::void_t<decltype(std::declval<Engine&>().longJump())>> : std::true_type {};
	}

// @randx-include ../common/engines/stream_jump_powers.inc

// @randx-include ../common/engines/stream_jump_apply.inc

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
			decltype(std::end(std::declval<C&>())),
			decltype(std::size(std::declval<C&>()))>>
			: is_random_access_iterator<decltype(std::begin(std::declval<C&>()))> {};

		template <class C>
		inline constexpr bool is_random_access_container_v =
			is_random_access_container<C>::value;

		template <class E, class = void>
		struct has_result_type : std::false_type {};

		template <class E>
		struct has_result_type<E, std::void_t<typename E::result_type>>
			: std::bool_constant<
				std::is_unsigned_v<typename E::result_type> &&
				!std::is_same_v<typename E::result_type, bool>> {};

		template <class E, class = void>
		struct has_invocable_engine : std::false_type {};

		template <class E>
		struct has_invocable_engine<E, std::enable_if_t<has_result_type<E>::value,
			std::void_t<decltype(std::declval<E&>()())>>>
			: std::is_same<decltype(std::declval<E&>()()), typename E::result_type> {};

		template <class E, class = void>
		struct has_min_max : std::false_type {};

		template <class E>
		struct has_min_max<E, std::enable_if_t<has_invocable_engine<E>::value,
			std::void_t<
				decltype(E::min()),
				decltype(E::max()),
				std::integral_constant<bool, (E::min() < E::max())>
			>>>
			: std::bool_constant<
				std::is_same_v<decltype(E::min()), typename E::result_type> &&
				std::is_same_v<decltype(E::max()), typename E::result_type> &&
				(E::min() < E::max())> {};

		template <class E>
		struct is_random_engine : has_min_max<std::remove_cv_t<std::remove_reference_t<E>>> {};

		template <class E>
		inline constexpr bool is_random_engine_v = is_random_engine<E>::value;

		template <class Engine, class = void>
		struct is_full_64bit_engine : std::false_type {};

		template <class Engine>
		struct is_full_64bit_engine<Engine, std::enable_if_t<is_random_engine_v<Engine>>>
			: std::bool_constant<
				(sizeof(typename std::remove_cv_t<std::remove_reference_t<Engine>>::result_type) >= sizeof(std::uint64_t) &&
				 static_cast<std::uint64_t>(std::remove_cv_t<std::remove_reference_t<Engine>>::min()) == 0ULL &&
				 static_cast<std::uint64_t>(std::remove_cv_t<std::remove_reference_t<Engine>>::max()) == (std::numeric_limits<std::uint64_t>::max)())> {};

		template <class Engine>
		inline constexpr bool is_full_64bit_engine_v = is_full_64bit_engine<Engine>::value;

		template <class Engine>
		inline constexpr bool IsFull64BitEngine = is_full_64bit_engine_v<Engine>;

		template <class Engine, class = void>
		struct is_full_32bit_engine : std::false_type {};

		template <class Engine>
		struct is_full_32bit_engine<Engine, std::enable_if_t<is_random_engine_v<Engine>>>
			: std::bool_constant<
				(static_cast<std::uint64_t>(std::remove_cv_t<std::remove_reference_t<Engine>>::min()) == 0ULL &&
				 static_cast<std::uint64_t>(std::remove_cv_t<std::remove_reference_t<Engine>>::max()) == 0xFFFFFFFFULL)> {};

		template <class Engine>
		inline constexpr bool is_full_32bit_engine_v = is_full_32bit_engine<Engine>::value;

		template <class Engine>
		inline constexpr bool IsFull32BitEngine = is_full_32bit_engine_v<Engine>;

		template <class T>
		inline T NormalizeBetaSample(T x, T y)
		// @randx-include ../common/distributions/normalize_beta_sample_body.inc

		template <class Engine,
			std::enable_if_t<is_random_engine_v<Engine>>* = nullptr>
		[[nodiscard]]
		inline std::uint64_t Generate64Bits(Engine& engine)
// @randx-include ../common/distributions/generate64_bits_body.inc

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

	template <class E>
	using RandomEngine = detail::is_random_engine<E>;

	template <class E>
	inline constexpr bool is_random_engine_v = detail::is_random_engine_v<E>;

	template <class Engine, std::enable_if_t<detail::HasStreamEngine<Engine>::value>* = nullptr>
	[[nodiscard]]
	inline constexpr Engine MakeStreamEngine(std::uint64_t streamId, std::uint64_t seed = DefaultSeed)
	{
		return detail::MakeStreamEngineWithId<Engine>(streamId, seed);
	}
