	inline void ChaCha20::generateBlock()
	{
		if (m_counterExhausted)
		{
			throw std::overflow_error("ChaCha20: 32-bit block counter overflow");
		}

		// 构造完整 16-word 状态：常数 + key + counter + nonce
		std::array<std::uint32_t, 16> state{};
		detail::ScopedWiper stateWiper(state.data(), sizeof(state));
		state[0] = detail::ChaCha20Constants[0];
		state[1] = detail::ChaCha20Constants[1];
		state[2] = detail::ChaCha20Constants[2];
		state[3] = detail::ChaCha20Constants[3];
		for (int i = 0; i < 8; ++i) state[4 + i] = m_state[i];  // key
		state[12] = m_state[8];                                  // counter
		state[13] = m_state[9];                                  // nonce[0]
		state[14] = m_state[10];                                 // nonce[1]
		state[15] = m_state[11];                                 // nonce[2]

		std::array<std::uint32_t, 16> working = state;
		detail::ScopedWiper workingWiper(working.data(), sizeof(working));

		// 20 轮 = 10 次 double-round（列轮 + 对角轮）
		for (int i = 0; i < 10; ++i)
		{
			// 列轮 QR 顺序：(0,4,8,12) (1,5,9,13) (2,6,10,14) (3,7,11,15)
			detail::ChaCha20QuarterRound(working[0],  working[4],  working[8],  working[12]);
			detail::ChaCha20QuarterRound(working[1],  working[5],  working[9],  working[13]);
			detail::ChaCha20QuarterRound(working[2],  working[6],  working[10], working[14]);
			detail::ChaCha20QuarterRound(working[3],  working[7],  working[11], working[15]);
			// 对角轮 QR 顺序：(0,5,10,15) (1,6,11,12) (2,7,8,13) (3,4,9,14)
			detail::ChaCha20QuarterRound(working[0],  working[5],  working[10], working[15]);
			detail::ChaCha20QuarterRound(working[1],  working[6],  working[11], working[12]);
			detail::ChaCha20QuarterRound(working[2],  working[7],  working[8],  working[13]);
			detail::ChaCha20QuarterRound(working[3],  working[4],  working[9],  working[14]);
		}

		// 加初始状态后按小端序输出 64 字节到 m_buffer
		for (int i = 0; i < 16; ++i)
		{
			const std::uint32_t v = working[i] + state[i];
			m_buffer[i * 4 + 0] = static_cast<std::uint8_t>(v);
			m_buffer[i * 4 + 1] = static_cast<std::uint8_t>(v >> 8);
			m_buffer[i * 4 + 2] = static_cast<std::uint8_t>(v >> 16);
			m_buffer[i * 4 + 3] = static_cast<std::uint8_t>(v >> 24);
		}

		if (m_state[8] == 0xFFFFFFFFU)
		{
			m_counterExhausted = true;
		}
		else
		{
			++m_state[8];
		}
		m_bufferPos = 0;
	}
