		{
			if (n <= 0)
				return {};

			std::vector<T> reservoir;
			reservoir.reserve(static_cast<std::size_t>(n));

			// 填满蓄水池
			Diff i = 0;
			for (; i < n && first != last; ++i, ++first)
				reservoir.push_back(*first);

			if (first == last)
				return reservoir;  // 元素不足 n，返回全部

			// Algorithm R：第 i 个元素（i >= n，0-indexed）以 n/(i+1) 概率替换蓄水池随机位置
			// 关键：j ∈ [0, i]（闭区间），uniform_int_distribution(0, i) 正好是 [0, i] 闭区间
			auto& rng = getEngine();
			for (; first != last; ++i, ++first)
			{
				SampleFixedIndexDistribution<std::uint64_t, Lifetime> dist(0, static_cast<std::uint64_t>(i));
				const auto j = DrawReservoirSampleIndex(dist, rng);
				if (j < static_cast<std::uint64_t>(n))
					reservoir[static_cast<std::size_t>(j)] = *first;
			}
			return reservoir;
		}
