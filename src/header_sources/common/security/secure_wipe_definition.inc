		static void SecureWipe(void* ptr, std::size_t len) noexcept
		{
			volatile auto* p = static_cast<volatile std::uint8_t*>(ptr);
			while (len--) *p++ = 0;
		}
