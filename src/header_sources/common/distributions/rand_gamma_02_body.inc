	{
		if (!std::isfinite(alpha) || !std::isfinite(beta) || alpha <= T{0} || beta <= T{0})
			throw std::invalid_argument("RandGamma: alpha and beta must be positive");
		std::gamma_distribution<T> dist(alpha, beta);
		return dist(engine);
	}
