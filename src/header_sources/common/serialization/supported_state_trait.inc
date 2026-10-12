		template <class State>
		struct is_supported_serialization_state : std::false_type {};

		template <std::size_t N>
		struct is_supported_serialization_state<std::array<std::uint32_t, N>>
			: std::bool_constant<(N > 0)> {};

		template <std::size_t N>
		struct is_supported_serialization_state<std::array<std::uint64_t, N>>
			: std::bool_constant<(N > 0)> {};

		template <class State>
		inline constexpr bool is_supported_serialization_state_v =
			is_supported_serialization_state<State>::value;
