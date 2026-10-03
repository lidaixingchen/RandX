	// 重置默认引擎的种子（用于测试复现）
// @randx-include ../common/engines/reseed_definition.inc

	// 重置为真随机种子
// @randx-include ../common/engines/reseed_random_definition.inc

	/// @defgroup basic 基础生成
	/// @brief RandInt / RandReal / RandBool / RandChar / RandBits

	/// @brief 生成 [min, max] 范围内的随机整数
	/// @param min 下界（含）
	/// @param max 上界（含）
	/// @return 均匀分布于 [min, max] 的随机整数
	template <class T = int,
		std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(T min, T max)
	// @randx-include ../common/distributions/rand_int_01_body.inc

	/// @brief 生成 [0, max] 范围内的随机整数
	/// @param max 上界（含）
	/// @return 均匀分布于 [0, max] 的随机整数
	template <class T = int,
		std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(T max)
	// @randx-include ../common/distributions/rand_int_02_body.inc

	/// @brief 生成 [min, max) 范围内的随机浮点数
	/// @param min 下界（含，默认 0）
	/// @param max 上界（不含，默认 1）
	/// @return 均匀分布于 [min, max) 的随机浮点数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandReal(T min = T{0}, T max = T{1})
	// @randx-include ../common/distributions/rand_real_01_body.inc

	/// @brief 采用无偏 Bit-Extraction 直通算法生成 [0, 1) 半开区间的随机浮点数（默认线程引擎）
	/// @tparam T 浮点数类型（float / double）
	/// @return [0, 1) 范围内的无偏伪随机浮点数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
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
	template <class Engine,
		std::enable_if_t<detail::is_random_engine_v<Engine>>* = nullptr>
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
	template <class Engine,
		std::enable_if_t<detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline bool RandBernoulli(Engine& engine, double p = 0.5)
// @randx-include ../common/distributions/rand_bernoulli_body.inc

	/// @brief 生成 [min, max] 范围内的随机字符
	/// @param min 下界字符（含）
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [min, max] 的随机字符
	/// @note 内部用 int64_t 避免 char32_t 范围（最大 0xFFFFFFFF）溢出 int32_t
	template <class CharT,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(CharT min, CharT max)
// @randx-include ../common/convenience/rand_char_interval_body.inc

	/// @brief 生成 [CharT{}, max] 范围内的随机字符
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [CharT{}, max] 的随机字符
	template <class CharT,
		std::enable_if_t<detail::is_character_v<CharT>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(CharT max)
// @randx-include ../common/convenience/rand_char_02_body.inc

	/// @brief 生成 [min, max] 范围内的随机字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界字符（含）
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [min, max] 的随机字符
	template <class CharT, class Engine,
		std::enable_if_t<detail::is_character_v<CharT> && detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT min, CharT max)
// @randx-include ../common/convenience/rand_char_interval_engine_body.inc

	/// @brief 生成 [CharT{}, max] 范围内的随机字符（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param max 上界字符（含）
	/// @return 均匀分布于 [CharT{}, max] 的随机字符
	template <class CharT, class Engine,
		std::enable_if_t<detail::is_character_v<CharT> && detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline CharT RandChar(Engine& engine, CharT max)
// @randx-include ../common/convenience/rand_char_03_body.inc

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
		// RandSample 分支选择阈值：n·K < size 时用 hash-set（实测交叉点 n≈N/127，K=64 留 2× 裕度）
		inline constexpr std::uint64_t HashSetThresholdK = 64;

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
	template <class Engine,
		std::enable_if_t<detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline char RandChar(Engine& engine, CharSet cs)
// @randx-include ../common/convenience/rand_char_body.inc

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
// @randx-include ../common/convenience/rand_element_body.inc

	/// @brief 从容器中随机取一个元素（右值容器，按值返回以避免悬垂引用）
	/// @param c 源容器（需支持 operator[] 和 size()）
	/// @return 容器中随机选取的一个元素的副本
	/// @throw std::invalid_argument 容器为空时抛出
	template <class Container,
		std::enable_if_t<detail::is_random_access_container_v<std::decay_t<Container>>>* = nullptr>
	[[nodiscard]]
	inline typename std::iterator_traits<decltype(std::begin(std::declval<Container&>()))>::value_type RandElement(Container&& c)
// @randx-include ../common/convenience/rand_element_body.inc

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
		std::enable_if_t<detail::is_random_access_iterator_v<It> && detail::is_random_engine_v<Engine>>* = nullptr>
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
			&& !detail::is_random_access_iterator_v<It>
			&& detail::is_random_engine_v<Engine>>* = nullptr>
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
// @randx-include ../common/distributions/rand_normal_body.inc

	/// @brief 生成正态分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 均值（默认 0）
	/// @param stddev 标准差（默认 1）
	/// @return 服从 N(mean, stddev) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
// @randx-include ../common/distributions/rand_normal_02_body.inc

	/// @brief 随机打乱容器
	/// @param c 待打乱的容器
	template <class Container,
		std::enable_if_t<detail::is_random_access_container_v<Container>
			&& !std::is_const_v<std::remove_reference_t<Container>>>* = nullptr>
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
		std::enable_if_t<detail::is_rand_fillable_v<It, T> && std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>>* = nullptr>
	inline void RandFill(It first, It last, T min, T max)
// @randx-include ../common/convenience/rand_fill_02_body.inc

	namespace detail
	{
		// @randx-include ../common/distributions/real_interval_kernel.inc
	}

	/// @brief 用 [min, max) 范围的随机浮点数填充迭代器区间
	/// @param first 起始迭代器
	/// @param last 结束迭代器
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	template <class It, class T,
		std::enable_if_t<detail::is_rand_fillable_v<It, T> && std::is_floating_point_v<T>>* = nullptr>
	inline void RandFill(It first, It last, T min, T max)
// @randx-include ../common/convenience/rand_fill_04_body.inc

	/// @brief 用 [min, max] 范围的随机整数填充迭代器区间（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param first 起始迭代器
	/// @param last 结束迭代器
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	template <class It, class T, class Engine,
		std::enable_if_t<detail::is_rand_fillable_v<It, T> && std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> && detail::is_random_engine_v<Engine>>* = nullptr>
	inline void RandFill(Engine& engine, It first, It last, T min, T max)
// @randx-include ../common/convenience/rand_fill_03_body.inc

	/// @brief 用 [min, max) 范围的随机浮点数填充迭代器区间（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param first 起始迭代器
	/// @param last 结束迭代器
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	template <class It, class T, class Engine,
		std::enable_if_t<detail::is_rand_fillable_v<It, T> && std::is_floating_point_v<T> && detail::is_random_engine_v<Engine>>* = nullptr>
	inline void RandFill(Engine& engine, It first, It last, T min, T max)
// @randx-include ../common/convenience/rand_fill_body.inc

	/// @brief 生成含 n 个随机整数的 vector
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max] 的随机整数 vector
	template <class T = int,
		std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
// @randx-include ../common/convenience/rand_vector_03_body.inc

	/// @brief 生成含 n 个随机浮点数的 vector
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max) 的随机浮点数 vector
	template <class T = double,
		std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(T min, T max, std::size_t n)
// @randx-include ../common/convenience/rand_vector_03_body.inc

	/// @brief 生成含 n 个随机整数的 vector（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max] 的随机整数 vector
	template <class T, class Engine,
		std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> && detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline std::vector<T> RandVector(Engine& engine, T min, T max, std::size_t n)
// @randx-include ../common/convenience/rand_vector_02_body.inc

	/// @brief 生成含 n 个随机浮点数的 vector（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 随机数下界（含）
	/// @param max 随机数上界（不含）
	/// @param n 生成数量
	/// @return 含 n 个均匀分布于 [min, max) 的随机浮点数 vector
	template <class T, class Engine,
		std::enable_if_t<std::is_floating_point_v<T> && detail::is_random_engine_v<Engine>>* = nullptr>
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
	template <class Engine, class WeightContainer,
		std::enable_if_t<detail::is_random_engine_v<Engine>>* = nullptr>
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
	template <class Engine, class IntType,
		std::enable_if_t<detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline IntType RandWeighted(Engine& engine, std::discrete_distribution<IntType>& dist)
	// @randx-include ../common/distributions/rand_weighted_04_body.inc

	/// @brief 生成 [min, max] 范围内的随机整数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界（含）
	/// @param max 上界（含）
	/// @return 均匀分布于 [min, max] 的随机整数
	template <class T, class Engine,
		std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> && detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline T RandInt(Engine& engine, T min, T max)
	// @randx-include ../common/distributions/rand_int_03_body.inc

	/// @brief 采用无偏 Bit-Extraction 直通算法生成 [0, 1) 半开区间的随机浮点数（指定引擎重载）
	/// @tparam T 浮点数类型（float / double）
	/// @param engine 伪随机数生成引擎（自动兼容 32 位与 64 位输出引擎）
	/// @return [0, 1) 范围内的无偏伪随机浮点数
	template <typename T = double, class Engine,
	          typename std::enable_if_t<std::is_floating_point_v<T> && detail::is_random_engine_v<Engine>, int> = 0>
	[[nodiscard]] inline constexpr T RandCanonical(Engine& engine)
// @randx-include ../common/distributions/rand_canonical_engine_body.inc

	/// @brief 生成 [min, max) 范围内的随机浮点数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param min 下界（含，默认 0）
	/// @param max 上界（不含，默认 1）
	/// @return 均匀分布于 [min, max) 的随机浮点数
	template <class T = double, class Engine,
		std::enable_if_t<std::is_floating_point_v<T> && detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline T RandReal(Engine& engine, T min = T{0}, T max = T{1})
	// @randx-include ../common/distributions/rand_real_02_body.inc

	////////////////////////////////////////////////////////////////
	//
	//	扩展便捷 API
	//
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

		// @randx-include ../common/sampling/sample_container_definition.inc
	}

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
		// 整型差值使用空请求快路径；其他差值类型由内核判定。
		if constexpr (std::is_integral_v<Diff>)
			if (n <= 0 || size <= 0) return {};
		return detail::SampleRandomAccess<T, Diff, detail::SampleDistributionLifetime::Selection>(
			first, size, n, []() -> Xoshiro256StarStar& { return DefaultEngine(); });
	}

	// 路径 2：输入迭代器（reservoir sampling, Algorithm R）
	template <class It,
		std::enable_if_t<detail::is_input_iterator_v<It>
			&& !detail::is_random_access_iterator_v<It>>* = nullptr>
	[[nodiscard]]
	inline std::vector<typename std::iterator_traits<It>::value_type>
	RandSample(It first, It last, typename std::iterator_traits<It>::difference_type n)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		using T = typename std::iterator_traits<It>::value_type;
		return detail::SampleReservoir<T, Diff>(first, last, n, []() -> Xoshiro256StarStar& { return DefaultEngine(); });
	}

	// 引擎重载 —— 随机访问迭代器
	template <class It, class Engine,
		std::enable_if_t<detail::is_random_access_iterator_v<It> && detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline std::vector<typename std::iterator_traits<It>::value_type>
	RandSample(Engine& engine, It first, It last, typename std::iterator_traits<It>::difference_type n)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		using T = typename std::iterator_traits<It>::value_type;
		const Diff size = std::distance(first, last);
		// 整型差值使用空请求快路径；其他差值类型由内核判定。
		if constexpr (std::is_integral_v<Diff>)
			if (n <= 0 || size <= 0) return {};
		return detail::SampleRandomAccess<T, Diff, detail::SampleDistributionLifetime::Draw>(
			first, size, n, [&engine]() -> Engine& { return engine; });
	}

	// 引擎重载 —— 输入迭代器（reservoir）
	template <class It, class Engine,
		std::enable_if_t<detail::is_input_iterator_v<It>
			&& !detail::is_random_access_iterator_v<It>
			&& detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline std::vector<typename std::iterator_traits<It>::value_type>
	RandSample(Engine& engine, It first, It last, typename std::iterator_traits<It>::difference_type n)
	{
		using Diff = typename std::iterator_traits<It>::difference_type;
		using T = typename std::iterator_traits<It>::value_type;
		return detail::SampleReservoir<T, Diff, detail::SampleDistributionLifetime::Draw>(first, last, n, [&engine]() -> Engine& { return engine; });
	}

	/// @brief 无放回抽样：按索引从容器中随机抽取 n 个元素
	/// @param c 源容器
	/// @param n 抽取数量（若 n >= 容器大小则返回全部元素的副本）
	/// @return 含 n 个随机选取元素的 vector
	template <class Container,
		std::enable_if_t<detail::is_random_access_container_v<Container>>* = nullptr>
	[[nodiscard]]
	inline auto RandSample(const Container& c, std::size_t n)
	{
		using T = typename std::iterator_traits<decltype(std::begin(c))>::value_type;
		using Diff = typename std::iterator_traits<decltype(std::begin(c))>::difference_type;
		if (n == 0) return std::vector<T>{};
		const auto first = std::begin(c);
		const auto last = std::end(c);
		const Diff size = std::distance(first, last);
		return detail::SampleContainer<T, Diff>(first, size, n, []() -> Xoshiro256StarStar& { return DefaultEngine(); });
	}

	/// @brief 无放回抽样：从容器中随机抽取 n 个元素（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param c 源容器
	/// @param n 抽取数量
	/// @return 含 n 个随机选取元素的 vector
	template <class Engine, class Container,
		std::enable_if_t<detail::is_random_engine_v<Engine> && detail::is_random_access_container_v<Container>>* = nullptr>
	[[nodiscard]]
	inline auto RandSample(Engine& engine, const Container& c, std::size_t n)
	{
		using T = typename std::iterator_traits<decltype(std::begin(c))>::value_type;
		using Diff = typename std::iterator_traits<decltype(std::begin(c))>::difference_type;
		if (n == 0) return std::vector<T>{};
		const auto first = std::begin(c);
		const auto last = std::end(c);
		const Diff size = std::distance(first, last);
		return detail::SampleContainer<T, Diff>(first, size, n, [&engine]() -> Engine& { return engine; });
	}
#undef RANDX_DETAIL_SAMPLE_INLINE

	/// @brief 生成 [0, n) 的随机排列
	/// @param n 排列长度
	/// @return 含 [0, n) 随机排列的 vector
	[[nodiscard]]
	inline std::vector<std::size_t> RandPermutation(std::size_t n)
	// @randx-include ../common/sampling/rand_permutation_body.inc

	/// @defgroup strings 字符串与 ID
	/// @brief RandString / RandUUID

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
	template <class Engine,
		std::enable_if_t<detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline std::string RandString(Engine& engine, std::size_t n, std::string_view charset)
// @randx-include ../common/convenience/rand_string_body.inc

	/// @brief 从预设字符集生成随机字符串（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 字符串长度
	/// @param cs 预设字符集枚举
	/// @return 从预设字符集中均匀选取字符组成的随机字符串
	template <class Engine,
		std::enable_if_t<detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline std::string RandString(Engine& engine, std::size_t n, CharSet cs)
// @randx-include ../common/convenience/rand_string_02_body.inc

	/// @brief 生成指数分布随机数
	/// @param lambda 速率参数（默认 1，均值 = 1/lambda）
	/// @return 服从 Exp(lambda) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExp(T lambda = T{1})
// @randx-include ../common/distributions/rand_exp_body.inc

	/// @brief 生成指数分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param lambda 速率参数（默认 1，均值 = 1/lambda）
	/// @return 服从 Exp(lambda) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExp(Engine& engine, T lambda = T{1})
// @randx-include ../common/distributions/rand_exp_02_body.inc

	/// @brief 生成泊松分布随机数
	/// @param mean 均值参数（默认 1.0）
	/// @return 服从 Poisson(mean) 的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandPoisson(double mean = 1.0)
	// @randx-include ../common/distributions/rand_poisson_01_body.inc

	/// @brief 生成泊松分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 均值参数（默认 1.0）
	/// @return 服从 Poisson(mean) 的随机整数
	template <class Engine, class T = int,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandPoisson(Engine& engine, double mean = 1.0)
	// @randx-include ../common/distributions/rand_poisson_02_body.inc

	/// @brief 生成伽马分布随机数
	/// @param alpha 形状参数（默认 1）
	/// @param beta 尺度参数（默认 1）
	/// @return 服从 Gamma(alpha, beta) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGamma(T alpha = T{1}, T beta = T{1})
// @randx-include ../common/distributions/rand_gamma_body.inc

	/// @brief 生成伽马分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param alpha 形状参数（默认 1）
	/// @param beta 尺度参数（默认 1）
	/// @return 服从 Gamma(alpha, beta) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGamma(Engine& engine, T alpha = T{1}, T beta = T{1})
// @randx-include ../common/distributions/rand_gamma_02_body.inc

	/// @brief 生成二项分布随机数
	/// @param t 试验次数（默认 1）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 B(t, p) 的随机整数
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBinomial(T t = 1, double p = 0.5)
	// @randx-include ../common/distributions/rand_binomial_01_body.inc

	/// @brief 生成二项分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param t 试验次数（默认 1）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 B(t, p) 的随机整数
	template <class Engine, class T = int,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBinomial(Engine& engine, T t = 1, double p = 0.5)
	// @randx-include ../common/distributions/rand_binomial_02_body.inc

	/// @brief 生成对数正态分布随机数
	/// @param mean 对数均值（默认 0）
	/// @param stddev 对数标准差（默认 1）
	/// @return 服从 LogNormal(mean, stddev) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandLogNormal(T mean = T{0}, T stddev = T{1})
// @randx-include ../common/distributions/rand_log_normal_body.inc

	/// @brief 生成对数正态分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param mean 对数均值（默认 0）
	/// @param stddev 对数标准差（默认 1）
	/// @return 服从 LogNormal(mean, stddev) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandLogNormal(Engine& engine, T mean = T{0}, T stddev = T{1})
// @randx-include ../common/distributions/rand_log_normal_02_body.inc

	/// @brief 生成几何分布随机数（首次成功前的失败次数）
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 Geometric(p) 的随机整数
	/// @throw std::overflow_error 抽样值超出返回类型范围时抛出
	template <class T = int, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGeometric(double p = 0.5)
// @randx-include ../common/distributions/rand_geometric_body.inc

	/// @brief 生成几何分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param p 每次成功概率（默认 0.5）
	/// @return 服从 Geometric(p) 的随机整数
	/// @throw std::overflow_error 抽样值超出返回类型范围时抛出
	template <class Engine, class T = int,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_integral_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandGeometric(Engine& engine, double p = 0.5)
// @randx-include ../common/distributions/geometric_body.inc

	/// @brief 生成柯西分布随机数
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Cauchy(a, b) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandCauchy(T a = T{0}, T b = T{1})
// @randx-include ../common/distributions/rand_cauchy_body.inc

	/// @brief 生成柯西分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Cauchy(a, b) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandCauchy(Engine& engine, T a = T{0}, T b = T{1})
// @randx-include ../common/distributions/rand_cauchy_02_body.inc

	/// @brief 生成韦布尔分布随机数
	/// @param a 形状参数（默认 1）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Weibull(a, b) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandWeibull(T a = T{1}, T b = T{1})
// @randx-include ../common/distributions/rand_weibull_body.inc

	/// @brief 生成韦布尔分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 形状参数（默认 1）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 Weibull(a, b) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandWeibull(Engine& engine, T a = T{1}, T b = T{1})
// @randx-include ../common/distributions/rand_weibull_02_body.inc

	/// @brief 生成极值分布（Gumbel）随机数
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 ExtremeValue(a, b) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExtremeValue(T a = T{0}, T b = T{1})
// @randx-include ../common/distributions/rand_extreme_value_body.inc

	/// @brief 生成极值分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 位置参数（默认 0）
	/// @param b 尺度参数（默认 1）
	/// @return 服从 ExtremeValue(a, b) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandExtremeValue(Engine& engine, T a = T{0}, T b = T{1})
// @randx-include ../common/distributions/rand_extreme_value_02_body.inc

	/// @brief 生成卡方分布随机数
	/// @param n 自由度（默认 1）
	/// @return 服从 ChiSquared(n) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandChiSquared(T n = T{1})
// @randx-include ../common/distributions/rand_chi_squared_body.inc

	/// @brief 生成卡方分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 自由度（默认 1）
	/// @return 服从 ChiSquared(n) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandChiSquared(Engine& engine, T n = T{1})
// @randx-include ../common/distributions/rand_chi_squared_02_body.inc

	/// @brief 生成学生 t 分布随机数
	/// @param n 自由度（默认 1）
	/// @return 服从 StudentT(n) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandStudentT(T n = T{1})
// @randx-include ../common/distributions/rand_student_t_body.inc

	/// @brief 生成学生 t 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param n 自由度（默认 1）
	/// @return 服从 StudentT(n) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandStudentT(Engine& engine, T n = T{1})
// @randx-include ../common/distributions/rand_student_t_02_body.inc

	/// @brief 生成 Fisher F 分布随机数
	/// @param m 第一自由度（默认 1）
	/// @param n 第二自由度（默认 1）
	/// @return 服从 FisherF(m, n) 的随机数
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandFisherF(T m = T{1}, T n = T{1})
// @randx-include ../common/distributions/rand_fisher_f_body.inc

	/// @brief 生成 Fisher F 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param m 第一自由度（默认 1）
	/// @param n 第二自由度（默认 1）
	/// @return 服从 FisherF(m, n) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
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
	template <class T = double, std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBeta(T a = T{1}, T b = T{1})
	// @randx-include ../common/distributions/rand_beta_01_body.inc

	/// @brief 生成 Beta 分布随机数（指定引擎重载）
	/// @param engine 自定义随机数引擎
	/// @param a 形状参数（默认 1）
	/// @param b 形状参数（默认 1）
	/// @return 服从 Beta(a, b) 的随机数
	template <class Engine, class T = double,
		std::enable_if_t<detail::is_random_engine_v<Engine> && std::is_floating_point_v<T>>* = nullptr>
	[[nodiscard]]
	inline T RandBeta(Engine& engine, T a = T{1}, T b = T{1})
	// @randx-include ../common/distributions/rand_beta_02_body.inc

	/// @brief 生成 N 位随机整数
	/// @tparam N 位数（1-64，且不超过 T 的位宽）
	/// @return 均匀分布于 [0, 2^N) 的随机整数
	template <int N, class T = std::uint64_t,
		std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> && (N > 0) && (N <= 64) && (N <= std::numeric_limits<T>::digits)>* = nullptr>
	[[nodiscard]]
	inline T RandBits()
// @randx-include ../common/convenience/rand_bits_02_body.inc

	/// @brief 生成 N 位随机整数（指定引擎重载）
	/// @tparam N 位数（1-64，且不超过 T 的位宽）
	/// @param engine 自定义随机数引擎
	/// @return 均匀分布于 [0, 2^N) 的随机整数
	template <int N, class T = std::uint64_t, class Engine,
		std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> && (N > 0) && (N <= 64) && (N <= std::numeric_limits<T>::digits) && detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline T RandBits(Engine& engine)
// @randx-include ../common/convenience/rand_bits_body.inc

	/// @brief 生成随机 UUID v4 字符串
	/// @return 格式为 xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx 的 UUID 字符串
	/// @warning 使用默认 PRNG（非 CSPRNG），不适用于安全敏感标识符；
	///          安全场景请改用 ChaCha20 引擎重载或 SecureRandomBytes
	template <class Engine,
		std::enable_if_t<detail::is_random_engine_v<Engine>>* = nullptr>
	[[nodiscard]]
	inline std::string RandUUID(Engine& engine)
// @randx-include ../common/convenience/rand_uuid_body.inc

// @randx-include ../common/convenience/rand_uuid_definition.inc
