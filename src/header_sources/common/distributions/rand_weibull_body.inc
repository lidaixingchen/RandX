	{
		if (!std::isfinite(a) || !std::isfinite(b) || a <= T{0} || b <= T{0})
			throw std::invalid_argument("RandWeibull: invalid a or b");
		if (a < T{1} && b > T{1})
			return detail::SampleScaledWeibull(DefaultEngine(), a, b);
		std::weibull_distribution<T> dist(a, b);
		return dist(DefaultEngine());
	}
