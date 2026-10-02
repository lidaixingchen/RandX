	{
		if (min > max)
			throw std::invalid_argument("RandChar: min > max");
		using IntT = std::int64_t;
		std::uniform_int_distribution<IntT> dist(
			static_cast<IntT>(min), static_cast<IntT>(max));
		return static_cast<CharT>(dist(DefaultEngine()));
	}
