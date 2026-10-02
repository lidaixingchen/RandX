	[[nodiscard]]
	inline std::uint64_t RandomSeed()
	{
		detail::NativeRandomSeedSources sources;
		return detail::RandomSeedWithSources(sources);
	}
