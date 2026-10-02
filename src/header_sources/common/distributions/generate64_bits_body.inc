		{
			if constexpr (IsFull64BitEngine<Engine>)
			{
				return static_cast<std::uint64_t>(engine());
			}
			else if constexpr (IsFull32BitEngine<Engine>)
			{
				const std::uint64_t lo = static_cast<std::uint64_t>(engine());
				const std::uint64_t hi = static_cast<std::uint64_t>(engine());
				return (hi << 32) | lo;
			}
			else
			{
				std::uniform_int_distribution<std::uint64_t> dist(0, (std::numeric_limits<std::uint64_t>::max)());
				return dist(engine);
			}
		}
