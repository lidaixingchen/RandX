	inline constexpr Xoroshiro128StarStar::result_type Xoroshiro128StarStar::operator()() noexcept
	{
		auto* state = s_.data();
		const std::uint64_t s0 = state[0];
		std::uint64_t s1 = state[1];
		const std::uint64_t result = detail::RotL(s0 * 5, 7) * 9;
		s1 ^= s0;
		state[0] = detail::RotL(s0, 24) ^ s1 ^ (s1 << 16);
		state[1] = detail::RotL(s1, 37);
		return result;
	}
