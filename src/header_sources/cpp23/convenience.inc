	/// @brief 重置默认引擎的种子（用于测试复现）
	/// @param seed 新的种子值
// @randx-include ../common/engines/reseed_definition.inc

	/// @brief 重置默认引擎为真随机种子
// @randx-include ../common/engines/reseed_random_definition.inc

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
	// @randx-include ../common/distributions/rand_int_01_body.inc

	/// @brief 生成 [0, max] 范围内的随机整数
	/// @param max 上界（含）
	/// @return 均匀分布于 [0, max] 的随机整数
	template <std::integral T = int>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	[[nodiscard]]
	inline T RandInt(T max)
	// @randx-include ../common/distributions/rand_int_02_body.inc

	/// @brief 生成 [min, max) 范围内的随机浮点数
	/// @param min 下界（含，默认 0）
	/// @param max 上界（不含，默认 1）
	/// @return 均匀分布于 [min, max) 的随机浮点数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandReal(T min = T{0}, T max = T{1})
	// @randx-include ../common/distributions/rand_real_01_body.inc

	/// @brief 采用无偏 Bit-Extraction 直通算法生成 [0, 1) 半开区间的随机浮点数（默认线程引擎）
	/// @tparam T 浮点数类型（float / double）
	/// @return [0, 1) 范围内的无偏伪随机浮点数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandCanonical()
	// @randx-include ../common/distributions/rand_canonical_body.inc

	/// @brief 生成 [0.0, 1.0) 半开区间的双精度浮点数（直通 Bit-Extraction 极速 API）
	[[nodiscard]]
	inline double RandCanonicalDouble()
	// @randx-include ../common/distributions/rand_canonical_double_body.inc

	/// @brief 生成 [0.0f, 1.0f) 半开区间的单精度浮点数（直通 Bit-Extraction 极速 API）
	[[nodiscard]]
	inline float RandCanonicalFloat()
	// @randx-include ../common/distributions/rand_canonical_float_body.inc

	/// @brief 生成随机布尔值
	/// @param p 为 true 的概率（默认 0.5）
	/// @return 以概率 p 返回 true
// @randx-include ../common/convenience/rand_bool_definition.inc

	/// @brief 生成随机布尔值（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 为 true 的概率（默认 0.5）
	/// @return 以概率 p 返回 true
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline bool RandBool(Engine& engine, double p = 0.5)
// @randx-include ../common/convenience/rand_bool_body.inc

	/// @brief 伯努利分布（RandBool 的别名封装，对齐 \<random\> 命名）
	/// @param p 成功概率（默认 0.5）
	/// @return 以概率 p 返回 true
// @randx-include ../common/distributions/rand_bernoulli_definition.inc

	/// @brief 伯努利分布（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 成功概率（默认 0.5）
	/// @return 以概率 p 返回 true
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline bool RandBernoulli(Engine& engine, double p = 0.5)
// @randx-include ../common/distributions/rand_bernoulli_body.inc

	/// @brief 生成 [min, max] 范围内的随机字符
	/// @param min 下界字符（含）
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [min, max] 的随机字符
	/// @note 内部用 int64_t 避免 char32_t 范围（最大 0xFFFFFFFF）溢出 int32_t
	template <detail::Character CharT>
	[[nodiscard]]
	inline CharT RandChar(CharT min, CharT max)
// @randx-include ../common/convenience/rand_char_interval_body.inc

	/// @brief 生成 [CharT{}, max] 范围内的随机字符
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [CharT{}, max] 的随机字符
	template <detail::Character CharT = char>
	[[nodiscard]]
	inline CharT RandChar(CharT max)
// @randx-include ../common/convenience/rand_char_02_body.inc

	/// @brief 生成 [min, max] 范围内的随机字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界字符（含）
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [min, max] 的随机字符
	template <detail::Character CharT, detail::RandomEngine Engine>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT min, CharT max)
// @randx-include ../common/convenience/rand_char_interval_engine_body.inc

	/// @brief 生成 [CharT{}, max] 范围内的随机字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [CharT{}, max] 的随机字符
	template <detail::Character CharT = char, detail::RandomEngine Engine>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT max)
// @randx-include ../common/convenience/rand_char_03_body.inc

	/// @}

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
// @randx-include ../common/convenience/rand_element_body.inc

	/// @brief 从容器中随机取一个元素（右值容器，按值返回以避免悬垂引用）
	/// @param c 源容器（需支持 operator[] 和 size()）
	/// @return 容器中随机选取的一个元素的副本
	/// @throw std::invalid_argument 容器为空时抛出
	template <class Container>
		requires std::ranges::random_access_range<Container>
	[[nodiscard]]
	inline std::ranges::range_value_t<Container> RandElement(Container&& c)
// @randx-include ../common/convenience/rand_element_body.inc

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
	[[nodiscard]]
	inline std::iter_value_t<It> RandElement(It first, Sentinel last)
	{
		if (first == last)
			throw std::invalid_argument("RandElement: empty range");
		std::iter_value_t<It> selected = *first;
		++first;
		for (std::iter_difference_t<It> i = 1; first != last; ++first, ++i)
		{
			if (RandInt<std::iter_difference_t<It>>(0, i) == 0)
				selected = *first;
		}
		return selected;
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
	[[nodiscard]]
	inline std::iter_value_t<It> RandElement(Engine& engine, It first, Sentinel last)
	{
		if (first == last)
			throw std::invalid_argument("RandElement: empty range");
		std::iter_value_t<It> selected = *first;
		++first;
		for (std::iter_difference_t<It> i = 1; first != last; ++first, ++i)
		{
			if (RandInt<std::iter_difference_t<It>>(engine, std::iter_difference_t<It>{0}, i) == 0)
				selected = *first;
		}
		return selected;
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
// @randx-include ../common/distributions/rand_normal_body.inc

	/// @brief 生成正态分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 均值（默认 0）
	/// @param stddev 标准差（默认 1）
	/// @return 服从 N(mean, stddev) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
// @randx-include ../common/distributions/rand_normal_02_body.inc

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
// @randx-include ../common/convenience/rand_fill_02_body.inc

	namespace detail
	{
		// @randx-include ../common/distributions/real_interval_kernel.inc
	}

	/// @brief 用 [min, max) 范围的随机浮点数填充迭代器区间
	/// @param first 起始迭代器
	/// @param last 结束迭代器/哨兵
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	template <class It, class Sentinel, std::floating_point T>
		requires std::output_iterator<It, T> && std::sentinel_for<Sentinel, It>
	inline void RandFill(It first, Sentinel last, T min, T max)
// @randx-include ../common/convenience/rand_fill_04_body.inc

	/// @brief 用 [min, max] 范围的随机整数填充迭代器区间（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param first 起始迭代器
	/// @param last 结束迭代器/哨兵
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	template <class It, class Sentinel, class T, detail::RandomEngine Engine>
		requires detail::RandFillable<It, T> && std::integral<T> && (!std::same_as<std::remove_cv_t<T>, bool>) && std::sentinel_for<Sentinel, It>
	inline void RandFill(Engine& engine, It first, Sentinel last, T min, T max)
// @randx-include ../common/convenience/rand_fill_03_body.inc

	/// @brief 用 [min, max) 范围的随机浮点数填充迭代器区间（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param first 起始迭代器
	/// @param last 结束迭代器/哨兵
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	template <class It, class Sentinel, std::floating_point T, detail::RandomEngine Engine>
		requires std::output_iterator<It, T> && std::sentinel_for<Sentinel, It>
	inline void RandFill(Engine& engine, It first, Sentinel last, T min, T max)
// @randx-include ../common/convenience/rand_fill_body.inc

	/// @brief 生成含 n 个随机整数的 vector
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max] 的随机整数 vector
	template <std::integral T = int>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
// @randx-include ../common/convenience/rand_vector_03_body.inc

	/// @brief 生成含 n 个随机浮点数的 vector
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max) 的随机浮点数 vector
	template <std::floating_point T = double>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
// @randx-include ../common/convenience/rand_vector_03_body.inc

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
// @randx-include ../common/convenience/rand_vector_02_body.inc

	/// @brief 生成含 n 个随机浮点数的 vector（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max) 的随机浮点数 vector
	template <std::floating_point T = double, detail::RandomEngine Engine>
	[[nodiscard]]
	inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n)
// @randx-include ../common/convenience/rand_vector_body.inc

	namespace detail
	{
		// @randx-include ../common/distributions/prepared_weights.inc
	}

	/// @brief 按权重随机选取索引
	/// @param weights 权重容器（元素为数值类型）
	/// @return 按权重概率选中的索引值
	template <class WeightContainer>
	[[nodiscard]]
	inline typename WeightContainer::size_type RandWeighted(const WeightContainer& weights)
	// @randx-include ../common/distributions/rand_weighted_01_body.inc

	/// @brief 按权重随机选取索引（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param weights 权重容器（元素为数值类型）
	/// @return 按权重概率选中的索引值
	template <detail::RandomEngine Engine, class WeightContainer>
	[[nodiscard]]
	inline typename WeightContainer::size_type RandWeighted(Engine& engine, const WeightContainer& weights)
	// @randx-include ../common/distributions/rand_weighted_02_body.inc

	/// @brief 按预构建权重分布随机选取索引（支持高频抽取复用，O(1) 复杂度）
	/// @param dist 预构建的 std::discrete_distribution 对象
	/// @return 按权重概率选中的索引值
	template <class IntType>
	[[nodiscard]]
	inline IntType RandWeighted(std::discrete_distribution<IntType>& dist)
	// @randx-include ../common/distributions/rand_weighted_03_body.inc

	/// @brief 按预构建权重分布随机选取索引（指定引擎，支持高频抽取复用，O(1) 复杂度）
	/// @param engine 自定义随机数引擎
	/// @param dist 预构建的 std::discrete_distribution 对象
	/// @return 按权重概率选中的索引值
	template <detail::RandomEngine Engine, class IntType>
	[[nodiscard]]
	inline IntType RandWeighted(Engine& engine, std::discrete_distribution<IntType>& dist)
	// @randx-include ../common/distributions/rand_weighted_04_body.inc

	/// @brief 生成 [min, max] 范围内的随机整数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界（含）
	/// @param max 上界（含）
	/// @return 均匀分布于 [min, max] 的随机整数
	template <std::integral T, detail::RandomEngine Engine>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	[[nodiscard]]
	inline T RandInt(Engine& engine, T min, T max)
	// @randx-include ../common/distributions/rand_int_03_body.inc

	/// @brief 采用无偏 Bit-Extraction 直通算法生成 [0, 1) 半开区间的随机浮点数（指定引擎重载）
	/// @tparam T 浮点数类型（float / double）
	/// @param engine 伪随机数生成引擎（自动兼容 32 位与 64 位输出引擎）
	/// @return [0, 1) 范围内的无偏伪随机浮点数
	template <std::floating_point T = double, detail::RandomEngine Engine>
	[[nodiscard]]
	inline constexpr T RandCanonical(Engine& engine)
// @randx-include ../common/distributions/rand_canonical_engine_body.inc

	/// @brief 生成 [min, max) 范围内的随机浮点数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界（含，默认 0）
	/// @param max 上界（不含，默认 1）
	/// @return 均匀分布于 [min, max) 的随机浮点数
	template <std::floating_point T = double, detail::RandomEngine Engine>
	[[nodiscard]]
	inline T RandReal(Engine& engine, T min = T{0}, T max = T{1})
	// @randx-include ../common/distributions/rand_real_02_body.inc



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
// @randx-include ../common/convenience/char_set_definition.inc

	namespace detail
	{
		// 返回预设字符集的字符串视图（零拷贝，指向静态存储）
// @randx-include ../common/convenience/char_set_string_definition.inc
	}

	/// @brief 从预设字符集随机取一个字符
	/// @param cs 预设字符集枚举
	/// @return 从字符集中均匀选取的 char（预设集均为 ASCII 范围）
	/// @throw std::invalid_argument 字符集为空时抛出
// @randx-include ../common/convenience/rand_char_definition.inc

	/// @brief 从预设字符集随机取一个字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param cs 预设字符集枚举
	/// @return 从字符集中均匀选取的 char
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline char RandChar(Engine& engine, CharSet cs)
// @randx-include ../common/convenience/rand_char_body.inc

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
		// @randx-include ../common/sampling/sample_bitmap_and_lifetime.inc

		// 内核在公开适配层展开，保留局部迭代器与静态获取器的优化信息。
#if defined(_MSC_VER)
#define RANDX_DETAIL_SAMPLE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define RANDX_DETAIL_SAMPLE_INLINE inline __attribute__((always_inline))
#else
#define RANDX_DETAIL_SAMPLE_INLINE inline
#endif
		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class GetEngine>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleRandomAccess(It& first, Diff size, Diff n, GetEngine&& getEngine)
		// @randx-include ../common/sampling/sample_random_access_body.inc


		template <class T, class Diff, SampleDistributionLifetime Lifetime = SampleDistributionLifetime::Selection, class It, class Sentinel, class GetEngine>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleReservoir(It& first, Sentinel& last, Diff n, GetEngine&& getEngine)
		// @randx-include ../common/sampling/sample_reservoir_body.inc
	}

	/// @brief 无放回抽样（随机访问迭代器版，hash-set / 索引数组双分支）
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	// 路径 1：随机访问迭代器（hash-set / 索引数组双分支）
	template <std::random_access_iterator It, std::sentinel_for<It> Sentinel>
	[[nodiscard]]
	inline std::vector<std::iter_value_t<It>>
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
	/// @param c 源容器（支持所有 random_access_range）
	/// @param n 抽取数量（若 n >= 容器大小则返回全部元素的副本）
	/// @return 含 n 个随机选取元素的 vector
	template <std::ranges::random_access_range Container>
		requires std::copy_constructible<std::ranges::range_value_t<Container>>
	[[nodiscard]]
	inline auto RandSample(const Container& c, std::size_t n)
	{
		if (n == 0) return std::vector<std::ranges::range_value_t<Container>>{};
		return RandSample(DefaultEngine(), c, n);
	}

	/// @brief 无放回抽样（输入迭代器版，reservoir sampling Algorithm R）
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	// 路径 2：输入迭代器（reservoir sampling, Algorithm R）
	template <std::input_iterator It, std::sentinel_for<It> Sentinel>
		requires (!std::random_access_iterator<It>)
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
	inline std::vector<std::iter_value_t<It>>
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
	/// @param engine 自定义随机数引擎
	/// @param first 范围起始迭代器
	/// @param last 范围结束迭代器/哨兵
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	// 引擎重载 —— 输入迭代器（reservoir）
	template <std::input_iterator It, std::sentinel_for<It> Sentinel, detail::RandomEngine Engine>
		requires (!std::random_access_iterator<It>)
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
	/// @param c 源容器（支持所有 random_access_range）
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	template <detail::RandomEngine Engine, std::ranges::random_access_range Container>
		requires std::copy_constructible<std::ranges::range_value_t<Container>>
	[[nodiscard]]
	inline auto RandSample(Engine& engine, const Container& c, std::size_t n)
	{
		if (n == 0) return std::vector<std::ranges::range_value_t<Container>>{};
		using Diff = std::ranges::range_difference_t<const Container>;
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
		if (size <= 0) return std::vector<std::ranges::range_value_t<Container>>{};
		detail::ValidateSampleSize(size);
		const auto sizeU = static_cast<std::uint64_t>(size);
		if (n > (sizeU - 1) / detail::SampleBitmapThresholdK && n <= sizeU / detail::SampleBitmapDensityDivisor)
			return detail::SampleBitmap<std::ranges::range_value_t<Container>, Diff>(engine, first, sizeU, n);
		return RandSample(engine, first, first + size, count);
	}
#undef RANDX_DETAIL_SAMPLE_INLINE

	/// @brief 生成 [0, n) 的随机排列
	/// @param n 排列长度
	/// @return 含 0 到 n-1 随机排列的 vector
	[[nodiscard]]
	inline std::vector<std::size_t> RandPermutation(std::size_t n)
	// @randx-include ../common/sampling/rand_permutation_body.inc

	/// @}

	/// @defgroup strings 字符串与 ID
	/// @brief RandString / RandUUID
	/// @{

	/// @brief 生成指定长度的随机字符串
	/// @param length 字符串长度
	/// @param charset 可用字符集（默认为字母+数字）
	/// @return 从 charset 中均匀选取字符组成的随机字符串
	/// @throw std::invalid_argument charset 为空时抛出
// @randx-include ../common/convenience/rand_string_definition.inc

	/// @brief 从预设字符集生成随机字符串
	/// @param n 字符串长度
	/// @param cs 预设字符集枚举
	/// @return 从预设字符集中均匀选取字符组成的随机字符串
// @randx-include ../common/convenience/rand_string_02_definition.inc

	/// @brief 生成指定长度的随机字符串（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 字符串长度
	/// @param charset 可用字符集
	/// @return 从 charset 中均匀选取字符组成的随机字符串
	/// @throw std::invalid_argument charset 为空时抛出
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline std::string RandString(Engine& engine, std::size_t n, std::string_view charset)
// @randx-include ../common/convenience/rand_string_body.inc

	/// @brief 从预设字符集生成随机字符串（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 字符串长度
	/// @param cs 预设字符集枚举
	/// @return 从预设字符集中均匀选取字符组成的随机字符串
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline std::string RandString(Engine& engine, std::size_t n, CharSet cs)
// @randx-include ../common/convenience/rand_string_02_body.inc

	/// @brief 生成指数分布随机数
	/// @param lambda 速率参数（默认 1，均值 = 1/lambda）
	/// @return 服从 Exp(lambda) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandExp(T lambda = T{1})
// @randx-include ../common/distributions/rand_exp_body.inc

	/// @brief 生成指数分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param lambda 速率参数（默认 1，均值 = 1/lambda）
	/// @return 服从 Exp(lambda) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandExp(Engine& engine, T lambda = T{1})
// @randx-include ../common/distributions/rand_exp_02_body.inc

	/// @brief 生成泊松分布随机数
	/// @param mean 均值参数（默认 1.0）
	/// @return 服从 Poisson(mean) 的随机整数
	template <std::integral T = int>
	[[nodiscard]]
	inline T RandPoisson(double mean = 1.0)
	// @randx-include ../common/distributions/rand_poisson_01_body.inc

	/// @brief 生成泊松分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 均值参数（默认 1.0）
	/// @return 服从 Poisson(mean) 的随机整数
	template <detail::RandomEngine Engine, std::integral T = int>
	[[nodiscard]]
	inline T RandPoisson(Engine& engine, double mean = 1.0)
	// @randx-include ../common/distributions/rand_poisson_02_body.inc

	/// @brief 生成伽马分布随机数
	/// @param alpha 形状参数（默认 1）
	/// @param beta 尺度参数（默认 1）
	/// @return 服从 Gamma(alpha, beta) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandGamma(T alpha = T{1}, T beta = T{1})
// @randx-include ../common/distributions/rand_gamma_body.inc

	/// @brief 生成伽马分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param alpha 形状参数（默认 1）
	/// @param beta 尺度参数（默认 1）
	/// @return 服从 Gamma(alpha, beta) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandGamma(Engine& engine, T alpha = T{1}, T beta = T{1})
// @randx-include ../common/distributions/rand_gamma_02_body.inc

	/// @brief 生成二项分布随机数
	/// @param t 试验次数（默认 1）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 B(t, p) 的随机整数
	template <std::integral T = int>
	[[nodiscard]]
	inline T RandBinomial(T t = 1, double p = 0.5)
	// @randx-include ../common/distributions/rand_binomial_01_body.inc

	/// @brief 生成二项分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param t 试验次数（默认 1）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 B(t, p) 的随机整数
	template <detail::RandomEngine Engine, std::integral T = int>
	[[nodiscard]]
	inline T RandBinomial(Engine& engine, T t = 1, double p = 0.5)
	// @randx-include ../common/distributions/rand_binomial_02_body.inc

	/// @brief 生成对数正态分布随机数
	/// @param mean 对数均值（默认 0）
	/// @param stddev 对数标准差（默认 1）
	/// @return 服从 LogNormal(mean, stddev) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandLogNormal(T mean = T{0}, T stddev = T{1})
// @randx-include ../common/distributions/rand_log_normal_body.inc

	/// @brief 生成对数正态分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 对数均值（默认 0）
	/// @param stddev 对数标准差（默认 1）
	/// @return 服从 LogNormal(mean, stddev) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandLogNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
// @randx-include ../common/distributions/rand_log_normal_02_body.inc

	/// @brief 生成几何分布随机数（首次成功前的失败次数）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 Geometric(p) 的随机整数
	/// @throw std::overflow_error 抽样值超出返回类型范围时抛出
	template <std::integral T = int>
	[[nodiscard]]
	inline T RandGeometric(double p = 0.5)
// @randx-include ../common/distributions/rand_geometric_body.inc

	/// @brief 生成几何分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 Geometric(p) 的随机整数
	/// @throw std::overflow_error 抽样值超出返回类型范围时抛出
	template <detail::RandomEngine Engine, std::integral T = int>
	[[nodiscard]]
	inline T RandGeometric(Engine& engine, double p = 0.5)
// @randx-include ../common/distributions/geometric_body.inc

	/// @brief 生成柯西分布随机数
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Cauchy(a, b) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandCauchy(T a = T{0}, T b = T{1})
// @randx-include ../common/distributions/rand_cauchy_body.inc

	/// @brief 生成柯西分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Cauchy(a, b) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandCauchy(Engine& engine, T a = T{0}, T b = T{1})
// @randx-include ../common/distributions/rand_cauchy_02_body.inc

	/// @brief 生成韦布尔分布随机数
	/// @param a 形状参数（默认 1）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Weibull(a, b) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandWeibull(T a = T{1}, T b = T{1})
// @randx-include ../common/distributions/rand_weibull_body.inc

	/// @brief 生成韦布尔分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 形状参数（默认 1）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Weibull(a, b) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandWeibull(Engine& engine, T a = T{1}, T b = T{1})
// @randx-include ../common/distributions/rand_weibull_02_body.inc

	/// @brief 生成极值分布（Gumbel）随机数
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 ExtremeValue(a, b) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandExtremeValue(T a = T{0}, T b = T{1})
// @randx-include ../common/distributions/rand_extreme_value_body.inc

	/// @brief 生成极值分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 ExtremeValue(a, b) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandExtremeValue(Engine& engine, T a = T{0}, T b = T{1})
// @randx-include ../common/distributions/rand_extreme_value_02_body.inc

	/// @brief 生成卡方分布随机数
	/// @param n 自由度（默认 1）
	/// @return 服从 ChiSquared(n) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandChiSquared(T n = T{1})
// @randx-include ../common/distributions/rand_chi_squared_body.inc

	/// @brief 生成卡方分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 自由度（默认 1）
	/// @return 服从 ChiSquared(n) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandChiSquared(Engine& engine, T n = T{1})
// @randx-include ../common/distributions/rand_chi_squared_02_body.inc

	/// @brief 生成学生 t 分布随机数
	/// @param n 自由度（默认 1）
	/// @return 服从 StudentT(n) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandStudentT(T n = T{1})
// @randx-include ../common/distributions/rand_student_t_body.inc

	/// @brief 生成学生 t 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 自由度（默认 1）
	/// @return 服从 StudentT(n) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandStudentT(Engine& engine, T n = T{1})
// @randx-include ../common/distributions/rand_student_t_02_body.inc

	/// @brief 生成 Fisher F 分布随机数
	/// @param m 第一自由度（默认 1）
	/// @param n 第二自由度（默认 1）
	/// @return 服从 FisherF(m, n) 的随机数
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandFisherF(T m = T{1}, T n = T{1})
// @randx-include ../common/distributions/rand_fisher_f_body.inc

	/// @brief 生成 Fisher F 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param m 第一自由度（默认 1）
	/// @param n 第二自由度（默认 1）
	/// @return 服从 FisherF(m, n) 的随机数
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandFisherF(Engine& engine, T m = T{1}, T n = T{1})
// @randx-include ../common/distributions/rand_fisher_f_02_body.inc

	namespace detail
	{
		template <class T>
		using BetaWorkType = std::conditional_t<std::is_same_v<T, float>, double, T>;

		// @randx-include ../common/distributions/beta_log_sampling.inc
	}

	/// @brief 生成 Beta 分布随机数
	/// @param a 形状参数（默认 1）
	/// @param b 形状参数（默认 1）
	/// @return 服从 Beta(a, b) 的随机数
	/// @note 无 STL 对应，自实现 Gamma(a)/(Gamma(a)+Gamma(b))
	template <std::floating_point T = double>
	[[nodiscard]]
	inline T RandBeta(T a = T{1}, T b = T{1})
	// @randx-include ../common/distributions/rand_beta_01_body.inc

	/// @brief 生成 Beta 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 形状参数（默认 1）
	/// @param b 形状参数（默认 1）
	/// @return 服从 Beta(a, b) 的随机数
	/// @note 无 STL 对应，自实现 Gamma(a)/(Gamma(a)+Gamma(b))
	template <detail::RandomEngine Engine, std::floating_point T = double>
	[[nodiscard]]
	inline T RandBeta(Engine& engine, T a = T{1}, T b = T{1})
	// @randx-include ../common/distributions/rand_beta_02_body.inc

	/// @brief 生成 N 位随机整数
	/// @tparam N 位数（1-64，且不超过 T 的位宽）
	/// @return 均匀分布于 [0, 2^N) 的随机整数
	template <int N, std::integral T = std::uint64_t>
		requires (!std::same_as<std::remove_cv_t<T>, bool>) && (N > 0) && (N <= 64) && (N <= std::numeric_limits<T>::digits)
	[[nodiscard]]
	inline T RandBits()
// @randx-include ../common/convenience/rand_bits_02_body.inc

	/// @brief 生成 N 位随机整数（指定引擎重载）
	/// @tparam N 位数（1-64，且不超过 T 的位宽）
	/// @param engine 自定义随机数引擎
	/// @return 均匀分布于 [0, 2^N) 的随机整数
	template <int N, std::integral T = std::uint64_t, detail::RandomEngine Engine>
		requires (!std::same_as<std::remove_cv_t<T>, bool>) && (N > 0) && (N <= 64) && (N <= std::numeric_limits<T>::digits)
	[[nodiscard]]
	inline T RandBits(Engine& engine)
// @randx-include ../common/convenience/rand_bits_body.inc

	/// @brief 生成随机 UUID v4 字符串
	/// @return 格式为 xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx 的 UUID 字符串
	/// @warning 使用默认 PRNG（非 CSPRNG），不适用于安全敏感标识符；
	///          安全场景请改用 ChaCha20 引擎重载或 SecureRandomBytes
	template <detail::RandomEngine Engine>
	[[nodiscard]]
	inline std::string RandUUID(Engine& engine)
// @randx-include ../common/convenience/rand_uuid_body.inc

// @randx-include ../common/convenience/rand_uuid_definition.inc
