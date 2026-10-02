	inline constexpr SFC64::result_type SFC64::operator()() noexcept
	{
		const std::uint64_t tmp = s_[0] + s_[1] + s_[3]++;
		s_[0] = s_[1] ^ (s_[1] >> 11);
		s_[1] = s_[2] + (s_[2] << 3);
		s_[2] = detail::RotL(s_[2], 24) + tmp;
		return tmp;
	}
