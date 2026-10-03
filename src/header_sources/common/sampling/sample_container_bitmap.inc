		detail::ValidateSampleSize(size);
		const auto sizeU = static_cast<std::uint64_t>(size);
		if (n > (sizeU - 1) / detail::SampleBitmapThresholdK && n <= sizeU / detail::SampleBitmapDensityDivisor)
			return detail::SampleBitmap<T, Diff>(engine, first, sizeU, n);
