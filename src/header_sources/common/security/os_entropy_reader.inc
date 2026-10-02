		// OS 熵读取的原生状态与平台适配
		enum class EntropyReadStatus
		{
			Progress,
			Interrupted,
			Failure
		};

		struct EntropyReadResult
		{
			EntropyReadStatus status;
			std::size_t bytes;
		};

#	if defined(_WIN32) && __has_include(<bcrypt.h>)
		[[nodiscard]]
		inline EntropyReadResult ConvertWindowsEntropyResult(NTSTATUS status, std::size_t requestedBytes) noexcept
		{
#		if defined(BCRYPT_SUCCESS)
			return BCRYPT_SUCCESS(status)
				? EntropyReadResult{EntropyReadStatus::Progress, requestedBytes}
				: EntropyReadResult{EntropyReadStatus::Failure, 0};
#		else
			return static_cast<NTSTATUS>(status) >= 0
				? EntropyReadResult{EntropyReadStatus::Progress, requestedBytes}
				: EntropyReadResult{EntropyReadStatus::Failure, 0};
#		endif
		}
#	elif defined(__linux__) && __has_include(<sys/random.h>)
		[[nodiscard]]
		inline EntropyReadResult ConvertLinuxEntropyResult(ssize_t result, int error) noexcept
		{
			if (result > 0)
				return {EntropyReadStatus::Progress, static_cast<std::size_t>(result)};
			if (result < 0 && error == EINTR)
				return {EntropyReadStatus::Interrupted, 0};
			return {EntropyReadStatus::Failure, 0};
		}
#	elif defined(__APPLE__) && __has_include(<Security/Security.h>)
		[[nodiscard]]
		inline EntropyReadResult ConvertAppleEntropyResult(int status, std::size_t requestedBytes) noexcept
		{
			return status == errSecSuccess
				? EntropyReadResult{EntropyReadStatus::Progress, requestedBytes}
				: EntropyReadResult{EntropyReadStatus::Failure, 0};
		}
#	endif

		struct NativeOsEntropyReader
		{
			[[nodiscard]]
			std::size_t maxRequestSize() const noexcept
			{
#	if defined(_WIN32) && __has_include(<bcrypt.h>)
				return static_cast<std::size_t>((std::numeric_limits<ULONG>::max)());
#	elif defined(__linux__) && __has_include(<sys/random.h>)
				return static_cast<std::size_t>((std::numeric_limits<ssize_t>::max)());
#	else
				return (std::numeric_limits<std::size_t>::max)();
#	endif
			}

			[[nodiscard]]
			EntropyReadResult read(std::uint8_t* buffer, std::size_t length) noexcept
			{
#	if defined(_WIN32) && __has_include(<bcrypt.h>)
				const ULONG requestSize = static_cast<ULONG>(length);
				const NTSTATUS status = ::BCryptGenRandom(
					nullptr, buffer, requestSize, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
				return ConvertWindowsEntropyResult(status, length);
#	elif defined(__linux__) && __has_include(<sys/random.h>)
				const ssize_t result = ::getrandom(buffer, length, 0);
				const int error = result < 0 ? errno : 0;
				return ConvertLinuxEntropyResult(result, error);
#	elif defined(__APPLE__) && __has_include(<Security/Security.h>)
				const int status = ::SecRandomCopyBytes(kSecRandomDefault, length, buffer);
				return ConvertAppleEntropyResult(status, length);
#	else
				(void)buffer;
				(void)length;
				return {EntropyReadStatus::Failure, 0};
#	endif
			}
		};
