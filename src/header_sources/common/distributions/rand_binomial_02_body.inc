	{
		if (t < 0 || !std::isfinite(p) || p < 0.0 || p > 1.0)
			throw std::invalid_argument("RandBinomial: invalid t or p");
		std::binomial_distribution<T> dist(t, p);
		return dist(engine);
	}
