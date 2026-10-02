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
// @randx-include ../common/engines/is_all_zero_definition.inc

// @randx-include ../common/engines/is_all_zero_02_definition.inc

// @randx-include ../common/engines/is_valid_state_definition.inc

// @randx-include ../common/engines/engine_state_policy_definition.inc

// @randx-include ../common/engines/state_policy_definition.inc

// @randx-include ../common/engines/is_valid_snapshot_definition.inc

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
// @randx-include ../common/engines/splitmix64_seed_seq_constructor_body.inc

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

// @randx-include ../common/engines/engine_base_min_definition.inc

// @randx-include ../common/engines/engine_base_max_definition.inc

// @randx-include ../common/engines/engine_base_discard_definition.inc

// @randx-include ../common/engines/engine_base_serialize_definition.inc

// @randx-include ../common/engines/engine_base_deserialize_definition.inc

			// C++23: defaulted 三路比较（保留 ==, !=, <, >, <=, >= 全套）
			friend auto operator<=>(const EngineBase&, const EngineBase&) = default;

		protected:

			static constexpr int Bits = static_cast<int>(sizeof(ResultType) * 8);

			EngineBase() = default;

			// State 构造（用户直接传入，包含 Release/Debug 全零状态静默修正）
// @randx-include ../common/engines/engine_base_engine_base_definition.inc

			// SeedSeq 构造（零状态修正，Release 安全）
			template <detail::SeedSequence SeedSeq>
				requires (!std::same_as<std::remove_cvref_t<SeedSeq>, state_type>
					&& !std::same_as<std::remove_cvref_t<SeedSeq>, Derived>)
			explicit constexpr EngineBase(SeedSeq& seq)
// @randx-include ../common/engines/engine_base_engine_base_body.inc

			// 单值播种（SplitMix64 扩展，等价于 generateSeedSequence<N>）
// @randx-include ../common/engines/engine_base_engine_base_02_definition.inc

			// jump 多项式通用实现（constexpr，供 MakeStreamEngine 编译期调用）
// @randx-include ../common/engines/engine_base_jump_poly_definition.inc

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
// @randx-include ../common/engines/sfc64_seed_seq_constructor_body.inc

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
