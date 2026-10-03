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
