	inline constexpr SFC64::SFC64(const std::uint64_t seed) noexcept
		: Base()
	{
		// 使用 SplitMix64 播种 + 12 轮预热
		SplitMix64 sm{ seed };
		s_[0] = sm();
		s_[1] = sm();
		s_[2] = sm();
		s_[3] = 1;
		// 全零状态会导致输出可预测，强制修正
		if ((s_[0] | s_[1] | s_[2]) == 0) s_[0] = 0x9E3779B97F4A7C15ULL;
		for (int i = 0; i < 12; ++i) { operator()(); }
	}
