	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandChiSquared: n must be positive");
		std::chi_squared_distribution<T> dist(n);
		return dist(DefaultEngine());
	}
