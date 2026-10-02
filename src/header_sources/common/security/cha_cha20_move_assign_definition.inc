	inline ChaCha20& ChaCha20::operator=(ChaCha20&& other) noexcept
	{
		if (this != &other)
		{
			detail::SecureWipe(m_state.data(), sizeof(m_state));
			detail::SecureWipe(m_buffer.data(), sizeof(m_buffer));

			m_state = other.m_state;
			m_buffer = other.m_buffer;
			m_bufferPos = other.m_bufferPos;
			m_bytesSinceReseed = other.m_bytesSinceReseed;
			m_autoReseed = other.m_autoReseed;
			m_counterExhausted = other.m_counterExhausted;
			m_movedFrom = other.m_movedFrom;

			detail::SecureWipe(other.m_state.data(), sizeof(other.m_state));
			detail::SecureWipe(other.m_buffer.data(), sizeof(other.m_buffer));
			other.m_movedFrom = true;
			other.m_bufferPos = 64;
			other.m_bytesSinceReseed = 0;
			other.m_counterExhausted = false;
		}
		return *this;
	}
