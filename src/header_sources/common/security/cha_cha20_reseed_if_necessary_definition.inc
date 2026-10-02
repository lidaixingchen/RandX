	inline void ChaCha20::reseedIfNecessary()
	{
		if (m_autoReseed && m_bytesSinceReseed >= detail::ChaCha20ReseedThreshold)
			reseed();
	}
