	inline ChaCha20::ChaCha20(const std::uint8_t* key, std::size_t keyLen,
	                          const std::uint8_t* nonce, std::size_t nonceLen,
	                          const std::uint32_t counter)
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(false),
		  m_counterExhausted(false), m_movedFrom(false)
	{
		if (!key || keyLen != 32 || !nonce || nonceLen != 12)
			throw std::invalid_argument("ChaCha20: invalid key or nonce");
		// key → m_state[0..7]（小端序）
		for (int i = 0; i < 8; ++i)
		{
			m_state[i] = static_cast<std::uint32_t>(key[i * 4])
			           | (static_cast<std::uint32_t>(key[i * 4 + 1]) << 8)
			           | (static_cast<std::uint32_t>(key[i * 4 + 2]) << 16)
			           | (static_cast<std::uint32_t>(key[i * 4 + 3]) << 24);
		}
		// nonce → m_state[9..11]（小端序）
		for (int i = 0; i < 3; ++i)
		{
			m_state[9 + i] = static_cast<std::uint32_t>(nonce[i * 4])
			               | (static_cast<std::uint32_t>(nonce[i * 4 + 1]) << 8)
			               | (static_cast<std::uint32_t>(nonce[i * 4 + 2]) << 16)
			               | (static_cast<std::uint32_t>(nonce[i * 4 + 3]) << 24);
		}
		m_state[8] = counter;  // counter
	}
