	inline constexpr RomuDuoJr::result_type RomuDuoJr::operator()() noexcept
	{
		const std::uint64_t xp = s_[0];
		s_[0] = 15241094284759029579ULL * s_[1];
		s_[1] = detail::RotL(s_[1] - xp, 27);
		return xp;
	}
