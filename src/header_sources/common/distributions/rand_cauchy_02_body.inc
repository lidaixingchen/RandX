	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandCauchy: invalid a or b");
		if (b >= detail::StableAffineScaleThreshold<T>())
		{
			std::cauchy_distribution<T> unitDistribution(T{0}, T{1});
			return detail::StableAffineTransform(a, b, unitDistribution(engine));
		}
		std::cauchy_distribution<T> dist(a, b);
		return dist(engine);
	}
