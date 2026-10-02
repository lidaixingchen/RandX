	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandCauchy: invalid a or b");
		std::cauchy_distribution<T> dist(a, b);
		return dist(engine);
	}
