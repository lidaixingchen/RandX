	{
		if (!std::isfinite(mean) || mean < 0.0)
			throw std::invalid_argument("RandPoisson: mean must be non-negative");
		if (mean > static_cast<double>((std::numeric_limits<T>::max)()))
			throw std::invalid_argument("RandPoisson: mean exceeds maximum value of return type");
		if (mean == 0.0) return T{0};
		std::poisson_distribution<T> dist(mean);
		return dist(engine);
	}
