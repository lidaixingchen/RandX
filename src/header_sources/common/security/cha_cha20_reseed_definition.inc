	inline void ChaCha20::reseed()
	{
		std::array<std::uint8_t, 44> seed{};  // 32(key) + 12(nonce)
		detail::ScopedWiper wiper(seed.data(), seed.size());
		SecureRandomBytes(seed.data(), seed.size());
		// key → m_state[0..7]（小端序）
		for (int i = 0; i < 8; ++i)
		{
			m_state[i] = static_cast<std::uint32_t>(seed[i * 4])
			           | (static_cast<std::uint32_t>(seed[i * 4 + 1]) << 8)
			           | (static_cast<std::uint32_t>(seed[i * 4 + 2]) << 16)
			           | (static_cast<std::uint32_t>(seed[i * 4 + 3]) << 24);
		}
		// nonce → m_state[9..11]（小端序）
		for (int i = 0; i < 3; ++i)
		{
			m_state[9 + i] = static_cast<std::uint32_t>(seed[32 + i * 4])
			               | (static_cast<std::uint32_t>(seed[32 + i * 4 + 1]) << 8)
			               | (static_cast<std::uint32_t>(seed[32 + i * 4 + 2]) << 16)
			               | (static_cast<std::uint32_t>(seed[32 + i * 4 + 3]) << 24);
		}
		m_state[8] = 0;            // counter 重置
		m_bufferPos = 64;          // 强制下次 operator() 触发新 block
		m_bytesSinceReseed = 0;
		m_counterExhausted = false;
		m_movedFrom = false;
		detail::SecureWipe(m_buffer.data(), m_buffer.size()); // 擦除旧 keystream
	}
