	inline void SecureRandomBytes(void* buf, std::size_t n)
	{
		if (n == 0) return;
		if (!detail::GetOsEntropyBytes(buf, n))
			throw std::runtime_error("SecureRandomBytes: OS entropy source failed");
	}
