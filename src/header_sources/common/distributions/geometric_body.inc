	{
		if (!std::isfinite(p) || p <= 0.0 || p > 1.0)
			throw std::invalid_argument("RandGeometric: p must be in (0, 1]");
		if (p == 1.0)
			return T{0};
		constexpr double maxT = static_cast<double>((std::numeric_limits<T>::max)());
		if (p < 1.0 / (maxT + 1.0))
			throw std::invalid_argument("RandGeometric: p is too small for return type");
		const double denom = -std::log1p(-p);
		int exponent = 0;
		(void)std::frexp(denom, &exponent);
		constexpr int MaxBlockBits = std::numeric_limits<std::uint64_t>::digits - 1;
		const int blockBits = (std::min)(MaxBlockBits, (std::max)(0, -exponent));
		const std::uint64_t blockSize = std::uint64_t{1} << blockBits;
		// q=1-p，X=B*G+R：P(G=g)=(1-q^B)q^(Bg)，P(R=r)=p*q^r/(1-q^B)。
		// B 取二次幂，使块内拒绝采样的接受概率有界，整数余数保留全部低位。
		std::exponential_distribution<double> exp_dist(1.0);
		const double blocks = exp_dist(engine) / (denom * static_cast<double>(blockSize));
		std::uint64_t remainder = 0;
		if (blockSize > 1)
		{
			std::uniform_int_distribution<std::uint64_t> offsets(0, blockSize - 1);
			do
			{
				remainder = offsets(engine);
			} while (exp_dist(engine) < denom * static_cast<double>(remainder));
		}
		const double upperBlocks = std::ldexp(1.0, std::numeric_limits<T>::digits - blockBits);
		if (!std::isfinite(blocks) || blocks >= upperBlocks)
			throw std::overflow_error("RandGeometric: generated value exceeds return type range");
		return static_cast<T>((static_cast<std::uint64_t>(blocks) << blockBits) | remainder);
	}
