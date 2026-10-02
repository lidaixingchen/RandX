	[[nodiscard]]
	inline std::uint64_t SecureSeed()
	{
		std::uint64_t seed;
		SecureRandomBytes(&seed, sizeof(seed));
		return seed;
	}
