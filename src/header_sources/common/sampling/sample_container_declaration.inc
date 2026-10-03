		struct DefaultSampleEngineGetter
		{
			Xoshiro256StarStar& operator()() const
			{
				return DefaultEngine();
			}
		};

		template <class It>
		struct SampleEndGetter
		{
			const It& last;
			auto operator()() const
			{
				return last;
			}
		};

		template <class It, class Diff>
		struct NormalizedSampleEndGetter
		{
			const It& first;
			Diff size;
			auto operator()() const
			{
				return first + size;
			}
		};

		template <class T, class Diff, class It, class GetEngine, class GetLast>
		RANDX_DETAIL_SAMPLE_INLINE std::vector<T> SampleContainer(
			const It& first, Diff size, std::size_t n, GetEngine&& getEngine, GetLast&& getLast);
