	inline constexpr Xoroshiro128StarStar::result_type Xoroshiro128StarStar::operator()() noexcept
	{
		const std::uint64_t s0 = s_[0];
		std::uint64_t s1 = s_[1];
		const std::uint64_t result = detail::RotL(s0 * 5, 7) * 9;
		s1 ^= s0;
		s_[0] = detail::RotL(s0, 24) ^ s1 ^ (s1 << 16);
		s_[1] = detail::RotL(s1, 37);
		return result;
	}
