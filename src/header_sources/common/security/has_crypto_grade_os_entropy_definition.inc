		// 编译期特性检测：检测目标平台与编译器环境是否支持 OS 密码学熵源 API
		// （Windows BCryptGenRandom / Linux getrandom / macOS SecRandomCopyBytes）。
		// 返回 true 表示目标平台支持真 OS 密码学 API；返回 false 表示当前平台缺少 OS 密码学支持。
		// 密码学安全组件（ChaCha20 / SecureRandomBytes）在熵源不可用或获取失败时直接抛异常，绝不降级。
		[[nodiscard]]
		inline constexpr bool HasCryptoGradeOsEntropy() noexcept
		{
#	if (defined(_WIN32) && __has_include(<bcrypt.h>)) || (defined(__linux__) && __has_include(<sys/random.h>)) || (defined(__APPLE__) && __has_include(<Security/Security.h>))
			return true;
#	else
			return false;
#	endif
		}
