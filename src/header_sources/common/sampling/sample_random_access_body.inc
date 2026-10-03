		{
			if (n <= 0 || size <= 0)
				return {};
			detail::ValidateSampleSize(size);
			if (n >= size)
			{
				if constexpr (std::is_integral_v<Diff>)
				{
					if (size == static_cast<Diff>(SampleSingleValueCount))
					{
						std::vector<T> all;
						all.reserve(SampleSingleValueCount);
						all.push_back(first[Diff{0}]);
						return all;
					}
				}
				return CopySamplePopulation<T>(first, size);
			}

			auto& rng = getEngine();
			const auto sizeU = static_cast<std::uint64_t>(size);
			const auto nU = static_cast<std::uint64_t>(n);
			const auto nSample = static_cast<std::size_t>(n);
			if (nU <= (sizeU - 1) / detail::HashSetThresholdK)
			{
				if (nSample <= SampleLinearIndexCapacity)
					return SampleLinearIndices<T, Diff, Lifetime>(rng, first, sizeU, nSample);
				return SampleSparseIndices<T, Diff, Lifetime>(rng, first, sizeU, nSample);
			}
			return SampleDenseIndices<T, Diff, Lifetime>(rng, first, size, nSample);
		}
