	inline ChaCha20::ChaCha20(const std::uint64_t seed)
		: m_state{}, m_buffer{}, m_bufferPos(64), m_bytesSinceReseed(0), m_autoReseed(false),
		  m_counterExhausted(false), m_movedFrom(false)
	{
		SplitMix64 sm{ seed };
		// key: 前 4 次 SplitMix64 输出，每次 8 字节按小端序拆为 2 个 uint32
		for (int i = 0; i < 4; ++i)
		{
			const std::uint64_t v = sm();
			m_state[i * 2]     = static_cast<std::uint32_t>(v);
			m_state[i * 2 + 1] = static_cast<std::uint32_t>(v >> 32);
		}
		// nonce: 第 5 次输出（8 字节）+ 第 6 次输出低 4 字节（丢弃高 4 字节）
		{
			const std::uint64_t v5 = sm();
			m_state[9]  = static_cast<std::uint32_t>(v5);
			m_state[10] = static_cast<std::uint32_t>(v5 >> 32);
		}
		m_state[11] = static_cast<std::uint32_t>(sm());
		m_state[8]  = 0;  // counter 初值 = 0
	}
