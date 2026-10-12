	{
		if (!std::isfinite(mean) || !std::isfinite(stddev) || stddev <= T{0})
			throw std::invalid_argument("RandNormal: invalid mean or stddev");
		return RandNormal(DefaultEngine(), mean, stddev);
	}
