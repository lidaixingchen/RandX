	{
		if (!std::isfinite(m) || !std::isfinite(n) || m <= T{0} || n <= T{0})
			throw std::invalid_argument("RandFisherF: invalid m or n");
		std::fisher_f_distribution<T> dist(m, n);
		return dist(engine);
	}
