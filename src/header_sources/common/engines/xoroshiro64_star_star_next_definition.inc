	inline constexpr Xoroshiro64StarStar::result_type Xoroshiro64StarStar::operator()() noexcept
	{
		const std::uint32_t s0 = s_[0];
		std::uint32_t s1 = s_[1];

		const std::uint32_t result = detail::RotL(s0 * 0x9E3779BB, 5) * 5;

		s1 ^= s0;
		s_[0] = detail::RotL(s0, 26) ^ s1 ^ (s1 << 9);
		s_[1] = detail::RotL(s1, 13);

		return result;
	}
