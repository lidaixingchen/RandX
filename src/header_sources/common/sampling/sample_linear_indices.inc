		inline constexpr std::size_t SampleLinearIndexCapacity = 2;
		inline constexpr std::size_t SampleSingleValueCount = 1;

		// 小样本分支保持独立调用边界，限制通用抽样内核的展开规模。
		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class Engine>
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		inline std::vector<T> SampleLinearIndices(Engine& engine, It& first, std::uint64_t size, std::size_t count)
		{
			// 单个索引和索引对使用定长存储，每次重抽至多比较一个已选索引。
			std::array<std::uint64_t, SampleLinearIndexCapacity> selected{};
			std::vector<T> result;
			result.reserve(count);
			SampleFixedIndexDistribution<std::uint64_t, Lifetime> dist(0, size - 1);
			while (result.size() < count)
			{
				const std::uint64_t index = dist(engine);
				const auto selectedEnd = selected.begin() + result.size();
				if (std::find(selected.begin(), selectedEnd, index) == selectedEnd)
				{
					selected[result.size()] = index;
					result.push_back(first[static_cast<Diff>(index)]);
				}
			}
			return result;
		}
