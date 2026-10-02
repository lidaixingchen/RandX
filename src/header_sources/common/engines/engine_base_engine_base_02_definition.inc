			explicit constexpr EngineBase(std::uint64_t seed) noexcept
			{
				SplitMix64 sm{ seed };
				for (std::size_t i = 0; i < N; ++i)
					s_[i] = static_cast<result_type>(sm());
				if (IsAllZero(s_)) s_[0] = 1;
			}
