		inline void SecureWipeCompilerBarrier() noexcept
		{
#	if defined(_MSC_VER)
			_ReadWriteBarrier();
#	elif defined(__GNUC__) || defined(__clang__)
			__asm__ __volatile__("" ::: "memory");
#	endif
		}

		inline void SecureWipePortable(void* ptr, std::size_t len) noexcept
		{
			if (len == 0) return;

			volatile auto* destination = static_cast<volatile std::uint8_t*>(ptr);
			for (std::size_t index = 0; index < len; ++index)
				destination[index] = 0;
			SecureWipeCompilerBarrier();
		}
