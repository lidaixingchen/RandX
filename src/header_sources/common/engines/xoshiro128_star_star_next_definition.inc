	inline constexpr Xoshiro128StarStar::result_type Xoshiro128StarStar::operator()() noexcept
	{
		auto* state = s_.data();
		const std::uint32_t result = detail::RotL(state[1] * 5, 7) * 9;
		const std::uint32_t t = state[1] << 9;
		state[2] ^= state[0];
		state[3] ^= state[1];
		state[1] ^= state[2];
		state[0] ^= state[3];
		state[2] ^= t;
		state[3] = detail::RotL(state[3], 11);
		return result;
	}
