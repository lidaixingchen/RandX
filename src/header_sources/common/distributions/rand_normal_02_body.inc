	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandNormal: invalid mean or stddev");
		if (stddev >= detail::StableAffineScaleThreshold<T>())
		{
			std::normal_distribution<T> unitDistribution(T{0}, T{1});
			return detail::StableAffineTransform(mean, stddev, unitDistribution(engine));
		}
		std::normal_distribution<T> dist(mean, stddev);
		return dist(engine);
	}
