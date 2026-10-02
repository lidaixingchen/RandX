	[[nodiscard]]
	// 平台调用与异常回退独立编译，限制线程局部初始化对常用随机接口的展开规模。
#if defined(_MSC_VER)
	__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
	__attribute__((noinline))
#endif
	inline std::uint64_t RandomSeed()
	{
		detail::NativeRandomSeedSources sources;
		return detail::RandomSeedWithSources(sources);
	}
