#if defined(RANDX_SECURITY_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

bool RandXSecureWipeCrossTranslationUnitCheck(
	RandX::detail::SecureWipeBackend* backend);

namespace
{
	using WipeFunction = void (*)(void*, std::size_t) noexcept;

	constexpr std::size_t kBufferSize = 19;
	constexpr std::size_t kWipeOffset = 4;
	constexpr std::size_t kWipeLength = 11;
	constexpr std::size_t kZeroLength = 0;
	constexpr std::uint8_t kZeroByte = 0;
	constexpr std::uint8_t kSentinelByte = 0xC7;

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

int main()
{
#if defined(RANDX_USE_PORTABLE_SECURE_WIPE) && RANDX_USE_PORTABLE_SECURE_WIPE
	constexpr auto expectedBackend = RandX::detail::SecureWipeBackend::Portable;
#elif defined(__APPLE__)
#	if defined(RANDX_USE_APPLE_MEMSET_S) && RANDX_USE_APPLE_MEMSET_S
	constexpr auto expectedBackend = RandX::detail::SecureWipeBackend::Apple;
#	else
	constexpr auto expectedBackend = RandX::detail::SecureWipeBackend::Portable;
#	endif
#elif defined(_WIN32) && __has_include(<bcrypt.h>)
	constexpr auto expectedBackend = RandX::detail::SecureWipeBackend::Windows;
#elif defined(__linux__) && defined(__GLIBC__) && defined(__GLIBC_PREREQ) && defined(__USE_MISC)
#	define RANDX_TEST_GLIBC_WIPE_MINIMUM_MAJOR 2
#	define RANDX_TEST_GLIBC_WIPE_MINIMUM_MINOR 25
#	if __GLIBC_PREREQ(RANDX_TEST_GLIBC_WIPE_MINIMUM_MAJOR, RANDX_TEST_GLIBC_WIPE_MINIMUM_MINOR)
	constexpr auto expectedBackend = RandX::detail::SecureWipeBackend::Glibc;
#	else
	constexpr auto expectedBackend = RandX::detail::SecureWipeBackend::Portable;
#	endif
#	undef RANDX_TEST_GLIBC_WIPE_MINIMUM_MAJOR
#	undef RANDX_TEST_GLIBC_WIPE_MINIMUM_MINOR
#else
	constexpr auto expectedBackend = RandX::detail::SecureWipeBackend::Portable;
#endif
	static_assert(RandX::detail::kSecureWipeBackend == expectedBackend);
	constexpr int kSuccessExitCode = 0;
	constexpr int kFailureExitCode = 1;
	RandX::detail::SecureWipeBackend crossTranslationUnitBackend =
		RandX::detail::SecureWipeBackend::Portable;

	if (!CheckExactRange(&RandX::detail::SecureWipe) ||
	    !CheckExactRange(&RandX::detail::SecureWipePortable) ||
	    !RandXSecureWipeCrossTranslationUnitCheck(&crossTranslationUnitBackend) ||
	    crossTranslationUnitBackend != RandX::detail::kSecureWipeBackend)
	{
		std::cerr << "secure wipe capability check failed\n";
		return kFailureExitCode;
	}

	std::cout << "secure wipe backend: " << RandX::detail::SecureWipeBackendName() << '\n';
	std::cout << "portable compatibility path: passed\n";
	std::cout << "cross-translation-unit path: passed\n";
	return kSuccessExitCode;
}
