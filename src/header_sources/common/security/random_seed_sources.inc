	namespace detail
	{
		struct NativeRandomSeedSources
		{
			[[nodiscard]]
			bool TryHardware(std::uint64_t& out) noexcept
			{
				return HardwareRand64(out);
			}

			[[nodiscard]]
			bool TryOs(std::uint64_t& out) noexcept
			{
				return GetOsEntropyBytes(&out, sizeof(out));
			}

			[[nodiscard]]
			std::uint64_t ReadRandomDevice()
			{
				std::random_device rd;
				constexpr int wordBits = std::numeric_limits<std::uint32_t>::digits;
				return (static_cast<std::uint64_t>(rd()) << wordBits) | rd();
			}

			[[nodiscard]]
			std::uint64_t Fallback() noexcept
			{
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
		};

		template <class Sources>
		[[nodiscard]]
		inline std::uint64_t RandomSeedWithSources(Sources& sources)
		{
			std::uint64_t seed;
			if (sources.TryHardware(seed))
				return seed;
			if (sources.TryOs(seed))
				return seed;
			try
			{
				return sources.ReadRandomDevice();
			}
			catch (...)
			{
				return sources.Fallback();
			}
		}
	}
