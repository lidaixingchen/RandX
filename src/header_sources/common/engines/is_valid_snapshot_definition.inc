		template <class Engine, class State>
		[[nodiscard]]
		inline constexpr bool IsValidSnapshot(const State& state) noexcept
		{
			if constexpr (EngineStatePolicy<Engine>::AllowsZeroState)
			{
				return true;
			}
			else
			{
				return !IsAllZero(state);
			}
		}
