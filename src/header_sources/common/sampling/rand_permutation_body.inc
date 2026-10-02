	{
		std::vector<std::size_t> perm(n);
		for (std::size_t i = 0; i < n; ++i) perm[i] = i;
		if (n < 2) return perm;
		auto& rng = DefaultEngine();
		for (std::size_t i = n - 1; i > 0; --i)
		{
			std::uniform_int_distribution<std::size_t> dist(0, i);
			const std::size_t j = dist(rng);
			std::swap(perm[i], perm[j]);
		}
		return perm;
	}
