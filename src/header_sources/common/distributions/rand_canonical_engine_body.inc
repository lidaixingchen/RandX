	{
		if constexpr (std::is_same_v<T, double>)
		{
			if constexpr (detail::IsFull64BitEngine<Engine>)
			{
				const std::uint64_t r = static_cast<std::uint64_t>(engine());
				return static_cast<double>(r >> 11) * 0x1.0p-53;
			}
			else if constexpr (detail::IsFull32BitEngine<Engine>)
			{
				const std::uint64_t high = static_cast<std::uint64_t>(engine());
				const std::uint64_t low  = static_cast<std::uint64_t>(engine());
				const std::uint64_t r = (high << 32) | low;
				return static_cast<double>(r >> 11) * 0x1.0p-53;
			}
			else
			{
				return std::generate_canonical<double, 53>(engine);
			}
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			if constexpr (detail::IsFull64BitEngine<Engine>)
			{
				const std::uint64_t r = static_cast<std::uint64_t>(engine());
				return static_cast<float>(r >> 40) * 0x1.0p-24f;
			}
			else if constexpr (detail::IsFull32BitEngine<Engine>)
			{
				const std::uint32_t r = static_cast<std::uint32_t>(engine());
				return static_cast<float>(r >> 8) * 0x1.0p-24f;
			}
			else
			{
				return std::generate_canonical<float, 24>(engine);
			}
		}
		else
		{
			return std::generate_canonical<T, std::numeric_limits<T>::digits>(engine);
		}
	}
