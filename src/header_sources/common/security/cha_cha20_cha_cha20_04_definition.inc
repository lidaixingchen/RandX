	inline ChaCha20::ChaCha20()
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(true),
		  m_counterExhausted(false), m_movedFrom(false)
	{
		reseed();  // 从 OS 熵获取 key + nonce，重置 counter
	}
