	{
		detail::ValidatePoissonMean<T>(mean);
		if (mean == 0.0) return T{0};
		std::poisson_distribution<T> dist(mean);
		return dist(DefaultEngine());
	}
