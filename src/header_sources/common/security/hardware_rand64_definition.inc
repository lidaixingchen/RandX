		// 尝试使用 RDRAND 获取 64 位硬件随机数
		[[nodiscard]]
		inline bool HardwareRand64(std::uint64_t& out) noexcept
		{
#if defined(__x86_64__) || defined(_M_X64)
	#if defined(__RDRND__)
			unsigned long long result;
			if (__builtin_ia32_rdrand64_step(&result))
			{
				out = result;
				return true;
			}
	#elif defined(_MSC_VER)
			int cpuInfo[4] = {0};
			__cpuid(cpuInfo, 1);
			if ((cpuInfo[2] & (1 << 30)) != 0)
			{
				unsigned long long result = 0;
				if (_rdrand64_step(&result))
				{
					out = result;
					return true;
				}
			}
	#endif
#endif
			(void)out;
			return false;
		}
