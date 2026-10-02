	inline void ChaCha20::discard(const unsigned long long n)
	{
		if (m_movedFrom)
			throw std::logic_error("ChaCha20: generator is in moved-from state");
		for (unsigned long long i = 0; i < n; ++i) operator()();
	}
