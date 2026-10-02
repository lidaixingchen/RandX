	{
		typename std::basic_ostream<CharT, Traits>::sentry ok(os);
		if (!ok) return os;

		detail::StreamFormatGuard<CharT, Traits> guard(os);

		os.setf(std::ios_base::dec, std::ios_base::basefield);
		os.setf(std::ios_base::left, std::ios_base::adjustfield);
		os.fill(os.widen(' '));
		os.width(0);

		const auto space = os.widen(' ');
		const auto state = engine.serialize();
		for (std::size_t i = 0; i < state.size(); ++i)
		{
			if (i != 0) os << space;
			os << state[i];
		}

		return os;
	}
