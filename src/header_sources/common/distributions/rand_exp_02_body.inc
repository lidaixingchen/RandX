	{
		if (!std::isfinite(lambda) || lambda <= T{0})
			throw std::invalid_argument("RandExp: lambda must be positive");
		std::exponential_distribution<T> dist(lambda);
		return dist(engine);
	}
