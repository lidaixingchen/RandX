		template <typename State>
		[[nodiscard]]
		inline constexpr bool IsValidState(const State& state) noexcept
		{
			return !IsAllZero(state);
		}
