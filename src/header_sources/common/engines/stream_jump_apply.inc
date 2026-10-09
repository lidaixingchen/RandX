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
