#if defined(RANDX_SECURITY_PREINCLUDE_SYSTEM_HEADERS)
#	if defined(_WIN32)
#		include <windows.h>
#	else
#		include <string.h>
#	endif
#endif

#if defined(RANDX_SECURITY_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
	using WipeFunction = void (*)(void*, std::size_t) noexcept;

	constexpr std::size_t kBufferSize = 19;
	constexpr std::size_t kWipeOffset = 4;
	constexpr std::size_t kWipeLength = 11;
	constexpr std::size_t kZeroLength = 0;
	constexpr std::uint8_t kZeroByte = 0;
	constexpr std::uint8_t kSentinelByte = 0xC7;
	constexpr std::uint64_t kFirstLifecycleSeed = 0x4D595DF4D0F33173ULL;
	constexpr std::uint64_t kSecondLifecycleSeed = 0x2B992DDFA23249D6ULL;

	bool CheckExactRange(WipeFunction wipe) noexcept
	{
		std::array<std::uint8_t, kBufferSize> bytes{};
		bytes.fill(kSentinelByte);
		wipe(nullptr, kZeroLength);
		wipe(bytes.data() + kWipeOffset, kWipeLength);

		for (std::size_t index = 0; index < bytes.size(); ++index)
		{
			const bool isWiped = index >= kWipeOffset && index < kWipeOffset + kWipeLength;
			if (bytes[index] != (isWiped ? kZeroByte : kSentinelByte))
				return false;
		}
		return true;
	}
}

bool RandXSecureWipeCrossTranslationUnitCheck(
	RandX::detail::SecureWipeBackend* backend)
{
	if (backend == nullptr || !CheckExactRange(&RandX::detail::SecureWipe))
		return false;
	*backend = RandX::detail::kSecureWipeBackend;

	RandX::ChaCha20 source(kFirstLifecycleSeed);
	static_cast<void>(source());
	RandX::ChaCha20 moved(std::move(source));
	RandX::ChaCha20 destination(kSecondLifecycleSeed);
	destination = std::move(moved);
	static_cast<void>(destination());
	return true;
}
