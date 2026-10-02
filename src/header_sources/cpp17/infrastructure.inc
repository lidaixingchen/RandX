	template <class Uint32, std::enable_if_t<std::is_same_v<Uint32, std::uint32_t>>*>
	inline constexpr float FloatFromBits(const Uint32 i) noexcept
// @randx-include ../common/convenience/float_from_bits_body.inc

	template <class Uint64, std::enable_if_t<std::is_same_v<Uint64, std::uint64_t>>*>
	inline constexpr double DoubleFromBits(const Uint64 i) noexcept
// @randx-include ../common/convenience/double_from_bits_body.inc

	namespace detail
	{
		// 安全擦除内存（volatile 防止编译器死存储消除）
// @randx-include ../common/security/secure_wipe_definition.inc

		// RAII 敏感内存擦除守卫（覆盖异常展开路径）
// @randx-include ../common/security/scoped_wiper_definition.inc

// @randx-include ../common/security/hardware_rand64_definition.inc

// @randx-include ../common/security/get_os_entropy_bytes_definition.inc

// @randx-include ../common/security/has_crypto_grade_os_entropy_definition.inc

// @randx-include ../common/security/chacha20_constants_and_quarter_round.inc
	}
