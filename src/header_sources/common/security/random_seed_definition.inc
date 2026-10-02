	[[nodiscard]]
	inline std::uint64_t RandomSeed()
	{
		std::uint64_t hw;
		if (detail::HardwareRand64(hw))
			return hw;
		if (detail::GetOsEntropyBytes(&hw, sizeof(hw)))
			return hw;
		try
		{
			std::random_device rd;
			return (static_cast<std::uint64_t>(rd()) << 32) | rd();
		}
		catch (...)
		{
			// 最终兜底：多维熵源（非密码学，仅保证 RandomSeed 永不抛异常，且防止 MSVC 15.6ms 时钟窗口下并发种子碰撞）
			const auto t1 = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			const auto t2 = std::chrono::steady_clock::now().time_since_epoch().count();
			const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
			static std::atomic<std::uint64_t> counter{0};
			std::uint64_t stackVar = 0;
			const std::uint64_t addr = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&stackVar));

			const std::uint64_t rawSeed = static_cast<std::uint64_t>(t1) ^ static_cast<std::uint64_t>(t2)
			                              ^ threadId ^ addr ^ counter.fetch_add(1, std::memory_order_relaxed);
			SplitMix64 sm{ rawSeed };
			return sm();
		}
	}
