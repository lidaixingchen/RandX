	[[nodiscard]]
	inline bool RandBool(double p = 0.5)
	{
		if (!std::isfinite(p) || p < 0.0 || p > 1.0)
			throw std::invalid_argument("RandBool: invalid probability p");
		std::bernoulli_distribution dist(p);
		return dist(DefaultEngine());
	}
