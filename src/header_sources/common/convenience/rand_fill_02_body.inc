	{
		if (min > max)
			throw std::invalid_argument("RandFill: min > max");
		using DistType = std::conditional_t<(sizeof(T) < sizeof(short)),
			std::conditional_t<std::is_signed_v<T>, int, unsigned int>, T>;
		std::uniform_int_distribution<DistType> dist(static_cast<DistType>(min), static_cast<DistType>(max));
		for (; first != last; ++first)
			*first = static_cast<T>(dist(DefaultEngine()));
	}
