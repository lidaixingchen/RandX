	// @randx-include ../common/engines/stream_jump_powers.inc

// @randx-include ../common/engines/stream_jump_apply.inc

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
