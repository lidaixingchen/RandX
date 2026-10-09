		{
			if (n <= 0)
				return {};

			std::vector<T> reservoir;
			using Count = std::uint64_t;
			constexpr Count maxCount = (std::numeric_limits<Count>::max)();
			Count requestedCount;
			if constexpr (std::numeric_limits<Diff>::digits <= std::numeric_limits<Count>::digits)
			{
				requestedCount = static_cast<Count>(n);
			}
			else
			{
				requestedCount = n > static_cast<Diff>(maxCount) ? maxCount : static_cast<Count>(n);
			}

			Count count = 0;
			while (reservoir.size() < requestedCount && first != last)
			{
				if (count == maxCount)
					throw std::length_error("SampleReservoir: range exceeds count capacity");
				reservoir.push_back(*first);
				++first;
				++count;
			}

			if (first == last)
				return reservoir;  // 元素不足 n，返回全部
			if (count == maxCount)
				throw std::length_error("SampleReservoir: range exceeds count capacity");

			// Algorithm R：第 i 个元素（i >= n，0-indexed）以 n/(i+1) 概率替换蓄水池随机位置
			// 关键：j ∈ [0, i]（闭区间），uniform_int_distribution(0, i) 正好是 [0, i] 闭区间
			auto& rng = getEngine();
			while (first != last)
			{
				if (count == maxCount)
					throw std::length_error("SampleReservoir: range exceeds count capacity");
				SampleFixedIndexDistribution<Count, Lifetime> dist(Count{0}, count);
				const auto j = DrawReservoirSampleIndex(dist, rng);
				if (j < static_cast<Count>(reservoir.size()))
					reservoir[static_cast<std::size_t>(j)] = *first;
				++first;
				++count;
			}
			return reservoir;
		}
