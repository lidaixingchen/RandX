	inline constexpr Xoshiro256StarStar::result_type Xoshiro256StarStar::operator()() noexcept
	{
		auto* state = s_.data();
		const std::uint64_t result = detail::RotL(state[1] * 5, 7) * 9;
		const std::uint64_t t = state[1] << 17;
		state[2] ^= state[0];
		state[3] ^= state[1];
		state[1] ^= state[2];
		state[0] ^= state[3];
		state[2] ^= t;
		state[3] = detail::RotL(state[3], 45);
		return result;
	}
