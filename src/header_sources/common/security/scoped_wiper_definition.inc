		struct ScopedWiper
		{
			void* ptr;
			std::size_t len;
			explicit ScopedWiper(void* p, std::size_t l) noexcept : ptr(p), len(l) {}
			~ScopedWiper() noexcept { SecureWipe(ptr, len); }
			ScopedWiper(const ScopedWiper&) = delete;
			ScopedWiper& operator=(const ScopedWiper&) = delete;
		};
