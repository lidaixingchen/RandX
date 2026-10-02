	inline ChaCha20::~ChaCha20() noexcept
	{
		detail::SecureWipe(m_state.data(), sizeof(m_state));
		detail::SecureWipe(m_buffer.data(), sizeof(m_buffer));
	}
