		// 使用原生 OS reader 完整填充请求缓冲区
		[[nodiscard]]
		inline bool GetOsEntropyBytes(void* buf, std::size_t n) noexcept
		{
			if (n == 0) return true;
#	if defined(RANDX_ENABLE_ENTROPY_TEST_HOOKS)
			if (entropyTestHook.fill != nullptr)
				return entropyTestHook.fill(entropyTestHook.context, buf, n);
#	endif
			NativeOsEntropyReader reader;
			return FillOsEntropy(reader, buf, n);
		}
