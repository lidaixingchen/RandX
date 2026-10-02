	{
		const auto prepared = detail::PrepareWeights(weights);
		std::discrete_distribution<std::size_t> dist(prepared.normalized_weights.begin(), prepared.normalized_weights.end());
		return prepared.active_indices[dist(engine)];
	}
