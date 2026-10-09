	{
		if (!std::isfinite(m) || !std::isfinite(n) || m <= T{0} || n <= T{0})
			throw std::invalid_argument("RandFisherF: invalid m or n");
		if (detail::RequiresStableFisherF(m, n))
			return detail::SampleStableFisherF(DefaultEngine(), m, n);
		std::fisher_f_distribution<T> dist(m, n);
		return dist(DefaultEngine());
	}
