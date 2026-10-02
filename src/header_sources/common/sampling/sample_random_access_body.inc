		{
			if (n <= 0 || size <= 0)
				return {};
			detail::ValidateSampleSize(size);
			if (n >= size)
			{
				std::vector<T> all;
				all.reserve(static_cast<std::size_t>(size));
				for (Diff i = 0; i < size; ++i)
					all.push_back(first[i]);
				return all;
			}

			auto& rng = getEngine();

			const auto sizeU = static_cast<std::uint64_t>(size);
			const auto nU = static_cast<std::uint64_t>(n);
			const auto nSample = static_cast<std::size_t>(n);

			// 分支选择：n·K < size 时 hash-set 内存优（O(n)）；否则索引数组常数优（O(N)）
			if (nU <= (sizeU - 1) / detail::HashSetThresholdK)
			{
				// hash-set 分支：O(n) 内存，O(n) 期望时间
				std::unordered_set<std::uint64_t> selected;
				selected.reserve(nSample);
				std::vector<T> result;
				result.reserve(nSample);
				SampleFixedIndexDistribution<std::uint64_t, Lifetime> dist(0, sizeU - 1);
				while (result.size() < nSample)
				{
					const std::uint64_t idx = dist(rng);
					if (selected.insert(idx).second)
						result.push_back(first[static_cast<Diff>(idx)]);
				}
				return result;
			}

			// 索引数组分支：O(N) 内存，O(N) 时间，无碰撞
			const std::size_t sz = static_cast<std::size_t>(size);
			std::vector<std::size_t> indices(sz);
			for (std::size_t i = 0; i < sz; ++i)
				indices[i] = i;

			// Fisher-Yates 前 n 步：j ∈ [i, size-1]
			for (std::size_t i = 0; i < nSample; ++i)
			{
				SampleFixedIndexDistribution<std::size_t, Lifetime> dist(i, sz - 1);
				const std::size_t j = dist(rng);
				std::swap(indices[i], indices[j]);
			}

			std::vector<T> result;
			result.reserve(nSample);
			for (std::size_t i = 0; i < nSample; ++i)
				result.push_back(first[static_cast<Diff>(indices[i])]);
			return result;
		}
