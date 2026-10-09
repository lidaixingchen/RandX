	{
		if (!std::isfinite(alpha) || !std::isfinite(beta) || alpha <= T{0} || beta <= T{0})
			throw std::invalid_argument("RandGamma: alpha and beta must be positive");
		constexpr int GammaShapeSplitCount = 2;
		// 同尺度 Gamma 的形状可相加；半形状采样保持形状翻倍的中间量可表示。
		constexpr T GammaShapeSplitThreshold =
			(std::numeric_limits<T>::max)() / static_cast<T>(GammaShapeSplitCount);
		if (alpha > GammaShapeSplitThreshold)
		{
			const T splitShape = alpha / static_cast<T>(GammaShapeSplitCount);
			std::gamma_distribution<T> splitDistribution(splitShape, beta);
			const T firstSample = splitDistribution(DefaultEngine());
			const T secondSample = splitDistribution(DefaultEngine());
			return firstSample + secondSample;
		}
		std::gamma_distribution<T> dist(alpha, beta);
		return dist(DefaultEngine());
	}
