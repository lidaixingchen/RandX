		enum class SecureWipeBackend
		{
			Portable,
			Windows,
			Glibc,
			Apple
		};

#	define RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MAJOR 2
#	define RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MINOR 25
#	define RANDX_DETAIL_MEMSET_S_MACOS_MINIMUM_VERSION 1090
#	define RANDX_DETAIL_MEMSET_S_IOS_MINIMUM_VERSION 70000

#	if defined(RANDX_USE_PORTABLE_SECURE_WIPE) && RANDX_USE_PORTABLE_SECURE_WIPE
#		define RANDX_DETAIL_SECURE_WIPE_BACKEND_PORTABLE
#	elif defined(RANDX_USE_APPLE_MEMSET_S) && RANDX_USE_APPLE_MEMSET_S && !defined(__APPLE__)
#		error RANDX_USE_APPLE_MEMSET_S requires an Apple target
#	elif defined(RANDX_DETAIL_WINDOWS_SDK_HEADERS_AVAILABLE)
#		define RANDX_DETAIL_SECURE_WIPE_BACKEND_WINDOWS
#	elif defined(__APPLE__) && defined(RANDX_USE_APPLE_MEMSET_S) && RANDX_USE_APPLE_MEMSET_S
#		if !defined(__STDC_WANT_LIB_EXT1__) || __STDC_WANT_LIB_EXT1__ != 1
#			error RANDX_USE_APPLE_MEMSET_S requires __STDC_WANT_LIB_EXT1__=1 before system headers
#		endif
#		if !defined(RSIZE_MAX)
#			error RANDX_USE_APPLE_MEMSET_S requires RSIZE_MAX from the target SDK
#		endif
#		if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#			if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < RANDX_DETAIL_MEMSET_S_MACOS_MINIMUM_VERSION
#				error memset_s is unavailable below the macOS 10.9 deployment target
#			endif
#		elif defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)
#			if __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < RANDX_DETAIL_MEMSET_S_IOS_MINIMUM_VERSION
#				error memset_s is unavailable below the iOS 7.0 deployment target
#			endif
#		else
#			error RANDX_USE_APPLE_MEMSET_S requires a validated macOS or iOS deployment target
#		endif
#		define RANDX_DETAIL_SECURE_WIPE_BACKEND_APPLE
#	elif defined(__linux__) && defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#		if __GLIBC_PREREQ(RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MAJOR, RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MINOR) && defined(__USE_MISC)
#			define RANDX_DETAIL_SECURE_WIPE_BACKEND_GLIBC
#		else
#			define RANDX_DETAIL_SECURE_WIPE_BACKEND_PORTABLE
#		endif
#	else
#		define RANDX_DETAIL_SECURE_WIPE_BACKEND_PORTABLE
#	endif

#	if defined(RANDX_DETAIL_SECURE_WIPE_BACKEND_WINDOWS)
		inline constexpr SecureWipeBackend kSecureWipeBackend = SecureWipeBackend::Windows;
		inline void SecureWipeWithSelectedBackend(void* ptr, std::size_t len) noexcept
		{
			::SecureZeroMemory(ptr, len);
		}
#	elif defined(RANDX_DETAIL_SECURE_WIPE_BACKEND_GLIBC)
		inline constexpr SecureWipeBackend kSecureWipeBackend = SecureWipeBackend::Glibc;
		inline void SecureWipeWithSelectedBackend(void* ptr, std::size_t len) noexcept
		{
			::explicit_bzero(ptr, len);
		}
#	elif defined(RANDX_DETAIL_SECURE_WIPE_BACKEND_APPLE)
		inline constexpr SecureWipeBackend kSecureWipeBackend = SecureWipeBackend::Apple;
		inline void SecureWipeWithSelectedBackend(void* ptr, std::size_t len) noexcept
		{
			static_assert(RSIZE_MAX > 0, "RSIZE_MAX must be positive");
			auto* destination = static_cast<unsigned char*>(ptr);
			const std::size_t maximumChunk = static_cast<std::size_t>(RSIZE_MAX);
			while (len > 0)
			{
				const std::size_t chunk = len < maximumChunk ? len : maximumChunk;
				const auto status = ::memset_s(destination,
				                               static_cast<rsize_t>(chunk),
				                               0,
				                               static_cast<rsize_t>(chunk));
				if (status != 0)
					SecureWipePortable(destination, chunk);
				destination += chunk;
				len -= chunk;
			}
		}
#	else
		inline constexpr SecureWipeBackend kSecureWipeBackend = SecureWipeBackend::Portable;
		inline void SecureWipeWithSelectedBackend(void* ptr, std::size_t len) noexcept
		{
			SecureWipePortable(ptr, len);
		}
#	endif

		inline constexpr const char* SecureWipeBackendName() noexcept
		{
			switch (kSecureWipeBackend)
			{
			case SecureWipeBackend::Windows: return "windows";
			case SecureWipeBackend::Glibc: return "glibc";
			case SecureWipeBackend::Apple: return "apple";
			case SecureWipeBackend::Portable: return "portable";
			}
			return "portable";
		}

		inline void SecureWipe(void* ptr, std::size_t len) noexcept
		{
			if (len == 0) return;
			SecureWipeWithSelectedBackend(ptr, len);
		}

#	undef RANDX_DETAIL_SECURE_WIPE_BACKEND_PORTABLE
#	undef RANDX_DETAIL_SECURE_WIPE_BACKEND_WINDOWS
#	undef RANDX_DETAIL_SECURE_WIPE_BACKEND_GLIBC
#	undef RANDX_DETAIL_SECURE_WIPE_BACKEND_APPLE
#	undef RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MAJOR
#	undef RANDX_DETAIL_GLIBC_EXPLICIT_BZERO_MINIMUM_MINOR
#	undef RANDX_DETAIL_MEMSET_S_MACOS_MINIMUM_VERSION
#	undef RANDX_DETAIL_MEMSET_S_IOS_MINIMUM_VERSION
#	undef RANDX_DETAIL_WINDOWS_SDK_HEADERS_AVAILABLE
