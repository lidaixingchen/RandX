	[[nodiscard]]
	inline constexpr bool IsOsCryptoEntropyAvailable() noexcept
	{
		return detail::HasCryptoGradeOsEntropy();
	}
