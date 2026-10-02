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
