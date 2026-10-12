	{
		if (!std::isfinite(a) || !std::isfinite(b) || b <= T{0})
			throw std::invalid_argument("RandCauchy: invalid a or b");
		return RandCauchy(DefaultEngine(), a, b);
	}
