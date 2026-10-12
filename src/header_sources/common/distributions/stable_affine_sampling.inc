	template <class T>
	[[nodiscard]]
	inline T StableAffineScaleThreshold() noexcept
	{
			// 平衡两个乘法因子的指数范围，提前归一化大尺度的尾部计算。
			return std::sqrt((std::numeric_limits<T>::max)());
	}

	template <class T>
	[[nodiscard]]
	inline T StableAffineTransform(T location, T scale, T unitSample) noexcept
	{
		if (!std::isfinite(unitSample))
			return std::fma(scale, unitSample, location);

		int scaleExponent = 0;
		int sampleExponent = 0;
		int locationExponent = 0;
		const T scaleFraction = std::frexp(scale, &scaleExponent);
		const T sampleFraction = std::frexp(unitSample, &sampleExponent);
		const T locationFraction = std::frexp(location, &locationExponent);
		const int productExponent = scaleExponent + sampleExponent;
		const int resultExponent = productExponent > locationExponent ? productExponent : locationExponent;
			// 先将融合运算的操作数归一到共同指数，再恢复结果尺度。
		const T scaledScale = std::ldexp(scaleFraction, productExponent - resultExponent);
		const T scaledLocation = std::ldexp(locationFraction, locationExponent - resultExponent);
		const T scaledResult = std::fma(scaledScale, sampleFraction, scaledLocation);
		return std::ldexp(scaledResult, resultExponent);
	}
