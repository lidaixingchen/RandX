		// ── A3 跨平台 OS 密码学熵源 ──
		// 用 OS 密码学 API 填充 [buf, buf+n) 字节；成功返回 true，失败或不支持返回 false。
		// 平台支持：Windows BCryptGenRandom、Linux getrandom、macOS SecRandomCopyBytes。
		// 注：密码学安全组件在 OS 熵源不可用或失败时直接抛异常，绝不隐式降级至 std::random_device 或非安全源；
		// getrandom 可能短读，内部循环直至填满；BCryptGenRandom/SecRandomCopyBytes 一次填满。
		[[nodiscard]]
		inline bool GetOsEntropyBytes(void* buf, std::size_t n) noexcept
		{
			if (n == 0) return true;
			auto* p = static_cast<std::uint8_t*>(buf);

#	if defined(_WIN32) && __has_include(<bcrypt.h>)
			// Windows: BCryptGenRandom（分块处理 >4GB 时的 ULONG 截断）
			// NTSTATUS >= 0 即 NT_SUCCESS（使用 BCRYPT_SUCCESS 宏或强转 NTSTATUS 判定）
			std::size_t filled = 0;
			while (filled < n)
			{
				const ULONG chunkSize = static_cast<ULONG>((std::min)(n - filled, static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())));
				const auto status = ::BCryptGenRandom(nullptr, p + filled, chunkSize, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#	if defined(BCRYPT_SUCCESS)
				if (!BCRYPT_SUCCESS(status)) return false;
#	else
				if (static_cast<NTSTATUS>(status) < 0) return false;
#	endif
				filled += chunkSize;
			}
			return true;

#	elif defined(__linux__) && __has_include(<sys/random.h>)
			// Linux: getrandom（循环处理短读与 EINTR）
			std::size_t filled = 0;
			while (filled < n)
			{
				const ssize_t ret = ::getrandom(p + filled, n - filled, 0);
				if (ret < 0)
				{
					if (errno == EINTR) continue;  // 被信号打断，重试
					return false;                   // ENOSYS/EFAULT 等不可恢复错误
				}
				if (ret == 0) return false;
				filled += static_cast<std::size_t>(ret);
			}
			return true;

#	elif defined(__APPLE__) && __has_include(<Security/Security.h>)
			// macOS: SecRandomCopyBytes（一次调用填满）
			return (::SecRandomCopyBytes(kSecRandomDefault, n, p) == errSecSuccess);

#	else
			// 无可用 OS 密码学熵源 → 返回 false，SecureRandomBytes 将抛出异常
			// 非安全场景的播种请使用 RandomSeed()（含 random_device → 时间戳回退链）
			(void)p; (void)n;
			return false;
#	endif
		}
