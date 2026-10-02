	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandExtremeValue: invalid a or b");
		std::extreme_value_distribution<T> dist(a, b);
		return dist(engine);
	}
