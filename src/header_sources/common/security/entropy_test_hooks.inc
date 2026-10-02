#	if defined(RANDX_ENABLE_ENTROPY_TEST_HOOKS)
		struct EntropyTestHook
		{
			bool (*fill)(void* context, void* buffer, std::size_t length) noexcept;
			void* context;
		};

		inline thread_local EntropyTestHook entropyTestHook{};
#	endif
