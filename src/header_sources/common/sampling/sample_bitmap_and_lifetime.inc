		template <class Diff>
		inline void ValidateSampleSize(Diff size)
		{
			using IndexLimit = std::conditional_t<
				(std::numeric_limits<std::size_t>::digits < std::numeric_limits<std::uint64_t>::digits),
				std::size_t, std::uint64_t>;
			if constexpr (std::numeric_limits<Diff>::digits > std::numeric_limits<IndexLimit>::digits)
			{
				if (size > static_cast<Diff>((std::numeric_limits<IndexLimit>::max)()))
					throw std::length_error("RandSample: range exceeds supported index size");
			}
		}

		inline constexpr std::uint64_t SampleBitmapWordBits = std::numeric_limits<std::uint64_t>::digits;
		inline constexpr std::uint64_t SampleBitmapThresholdK = HashSetThresholdK * SampleBitmapWordBits;
		inline constexpr std::uint64_t SampleBitmapDensityDivisor = 2;

		template <class T, class Diff, class It, class Engine>
		inline std::vector<T> SampleBitmap(Engine& engine, It first, std::uint64_t size, std::size_t n)
		{
			const std::size_t wordCount = static_cast<std::size_t>(
				size / SampleBitmapWordBits + (size % SampleBitmapWordBits != 0));
			std::vector<std::uint64_t> selected(wordCount, 0);
			std::vector<T> result;
			result.reserve(n);
			std::uniform_int_distribution<std::uint64_t> indices(0, size - 1);
			// 每个索引占一位；重复索引重抽，接受顺序仍是均匀的无放回抽样。
			while (result.size() < n)
			{
				const std::uint64_t index = indices(engine);
				auto& word = selected[static_cast<std::size_t>(index / SampleBitmapWordBits)];
				const std::uint64_t mask = std::uint64_t{1} << (index % SampleBitmapWordBits);
				if ((word & mask) == 0)
				{
					word |= mask;
					result.push_back(first[static_cast<Diff>(index)]);
				}
			}
			return result;
		}

		// 固定区间分布的生命周期由入口策略选择，碰撞重试也属于一次抽取。
		enum class SampleDistributionLifetime { Selection, Draw };

		template <class UInt, SampleDistributionLifetime Lifetime>
		class SampleFixedIndexDistribution;

		template <class UInt>
		class SampleFixedIndexDistribution<UInt, SampleDistributionLifetime::Selection>
		{
			std::uniform_int_distribution<UInt> distribution;
		public:
			SampleFixedIndexDistribution(UInt lower, UInt upper) : distribution(lower, upper) {}
			template <class Engine>
			UInt operator()(Engine& engine) { return distribution(engine); }
		};

		template <class UInt>
		class SampleFixedIndexDistribution<UInt, SampleDistributionLifetime::Draw>
		{
			UInt lower;
			UInt upper;
		public:
			SampleFixedIndexDistribution(UInt lowerBound, UInt upperBound) : lower(lowerBound), upper(upperBound) {}
			template <class Engine>
			UInt operator()(Engine& engine)
			{
				return RandInt<UInt>(engine, lower, upper);
			}
		};

		// @randx-include sample_linear_indices.inc
