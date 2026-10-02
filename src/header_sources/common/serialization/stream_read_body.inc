	{
		typename std::basic_istream<CharT, Traits>::sentry ok(is);
		if (!ok) return is;

		detail::StreamFormatGuard<CharT, Traits> guard(is);

		is.setf(std::ios_base::dec, std::ios_base::basefield);
		is.setf(std::ios_base::skipws);
		is.width(0);

		typename Engine::state_type state{};
		std::size_t i = 0;
		for (; i < state.size() && (is >> state[i]); ++i)
		{
		}

		if (i == state.size() && detail::IsValidSnapshot<Engine>(state))
		{
			engine.deserialize(state);
		}
		else
		{
			is.setstate(std::ios_base::failbit);
		}

		return is;
	}
