	inline ChaCha20::result_type ChaCha20::operator()()
	{
		if (m_movedFrom)
			throw std::logic_error("ChaCha20: generator is in moved-from state");
		reseedIfNecessary();
		if (m_bufferPos == 64)
		{
			if (m_counterExhausted)
				throw std::overflow_error("ChaCha20: 32-bit block counter overflow");
			generateBlock();
		}
		// 从缓存取 8 字节，小端序组装为 uint64_t
		std::uint64_t result = 0;
		for (int i = 0; i < 8; ++i)
			result |= static_cast<std::uint64_t>(m_buffer[m_bufferPos + i]) << (8 * i);
		m_bufferPos += 8;
		m_bytesSinceReseed += 8;
		return result;
	}
