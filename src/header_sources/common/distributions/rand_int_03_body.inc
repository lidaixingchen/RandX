	{
		if (min > max)
			throw std::invalid_argument("RandInt: min > max");
		using ValueType = std::remove_cv_t<T>;
		using DistType = std::conditional_t<(sizeof(ValueType) < sizeof(short)),
			std::conditional_t<std::is_signed_v<ValueType>, int, unsigned int>,
			std::conditional_t<std::is_signed_v<ValueType>,
				std::make_signed_t<ValueType>, std::make_unsigned_t<ValueType>>>;
		std::uniform_int_distribution<DistType> dist(static_cast<DistType>(min), static_cast<DistType>(max));
		return static_cast<T>(dist(engine));
	}
