	{
		if (min > max)
			throw std::invalid_argument("RandVector: min > max");
		std::vector<T> v;
		v.reserve(n);
		using DistType = std::conditional_t<(sizeof(T) < sizeof(short)),
			std::conditional_t<std::is_signed_v<T>, int, unsigned int>, T>;
		std::uniform_int_distribution<DistType> dist(static_cast<DistType>(min), static_cast<DistType>(max));
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(static_cast<T>(dist(engine)));
		return v;
	}
