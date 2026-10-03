		template <class T, class Diff, class It, class GetEngine>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleContainer(It first, Diff size, std::size_t n, GetEngine&& getEngine)
		{
			if (size <= 0) return {};
			auto& engine = getEngine();
			ValidateSampleSize(size);
			Diff count;
			if constexpr (std::numeric_limits<Diff>::digits < std::numeric_limits<std::size_t>::digits)
			{
				constexpr auto maxCount = static_cast<std::size_t>((std::numeric_limits<Diff>::max)());
				count = static_cast<Diff>((std::min)(n, maxCount));
			}
			else
			{
				count = static_cast<Diff>(n);
			}
			const auto sizeU = static_cast<std::uint64_t>(size);
			if (n > (sizeU - 1) / SampleBitmapThresholdK && n <= sizeU / SampleBitmapDensityDivisor)
				return SampleBitmap<T, Diff>(engine, first, sizeU, n);
			return SampleRandomAccess<T, Diff, SampleDistributionLifetime::Draw>(
				first, size, count, [&engine]() -> auto& { return engine; });
		}
