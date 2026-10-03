#if defined(_MSC_VER)
#define RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY __declspec(noinline) inline
#elif defined(__GNUC__) || defined(__clang__)
#define RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY inline __attribute__((noinline))
#else
#define RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY inline
#endif

		// 操作内核持有分配与遍历，公开适配层仅展开边界与策略选择。
		template <class T, class Diff, class It>
		RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY std::vector<T> CopySamplePopulation(It& first, Diff size)
		{
			std::vector<T> all;
			all.reserve(static_cast<std::size_t>(size));
			for (Diff i = 0; i < size; ++i)
				all.push_back(first[i]);
			return all;
		}

		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class Engine>
		RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY std::vector<T> SampleSparseIndices(Engine& engine, It& first, std::uint64_t sizeU, std::size_t count)
		{
			// hash-set 分支：O(n) 内存，O(n) 期望时间
			std::unordered_set<std::uint64_t> selected;
			selected.reserve(count);
			std::vector<T> result;
			result.reserve(count);
			SampleFixedIndexDistribution<std::uint64_t, Lifetime> dist(0, sizeU - 1);
			while (result.size() < count)
			{
				const std::uint64_t idx = dist(engine);
				if (selected.insert(idx).second)
					result.push_back(first[static_cast<Diff>(idx)]);
			}
			return result;
		}

		template <class T, class Diff, SampleDistributionLifetime Lifetime, class It, class Engine>
		RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY std::vector<T> SampleDenseIndices(Engine& engine, It& first, Diff size, std::size_t count)
		{
			// 索引数组分支：O(N) 内存，O(N) 时间，无碰撞
			const std::size_t sz = static_cast<std::size_t>(size);
			std::vector<std::size_t> indices(sz);
			for (std::size_t i = 0; i < sz; ++i)
				indices[i] = i;

			// Fisher-Yates 前 n 步：j ∈ [i, size-1]
			for (std::size_t i = 0; i < count; ++i)
			{
				SampleFixedIndexDistribution<std::size_t, Lifetime> dist(i, sz - 1);
				const std::size_t j = dist(engine);
				std::swap(indices[i], indices[j]);
			}

			std::vector<T> result;
			result.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
				result.push_back(first[static_cast<Diff>(indices[i])]);
			return result;
		}
#undef RANDX_DETAIL_SAMPLE_OPERATION_BOUNDARY
