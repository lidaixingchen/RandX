	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandExtremeValue: invalid a or b");
		return RandExtremeValue(DefaultEngine(), a, b);
	}
