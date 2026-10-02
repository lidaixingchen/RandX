	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandLogNormal: invalid mean or stddev");
		std::lognormal_distribution<T> dist(mean, stddev);
		return dist(DefaultEngine());
	}
