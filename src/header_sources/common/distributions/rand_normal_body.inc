	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandNormal: invalid mean or stddev");
		std::normal_distribution<T> dist(mean, stddev);
		return dist(DefaultEngine());
	}
