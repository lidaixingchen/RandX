		// ── A4 ChaCha20 常数与辅助 ──
		// ChaCha20 常数 "expand 32-byte k"（RFC 8439 §2.3）
		inline constexpr std::uint32_t ChaCha20Constants[4] = {
			0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u
		};
		// 参考 NIST SP 800-90A reseed_interval 概念（SP 800-90A 涵盖 Hash/HMAC/CTR_DRBG，不含 ChaCha20；
		// 此处借用其"周期性强制 reseed 提供前向安全"思想，取保守阈值）
		inline constexpr std::uint64_t ChaCha20ReseedThreshold = 1ULL << 20;  // 1 MB

		// ChaCha20 quarter-round（仅 add/xor/rotl，常时间友好）
		static void ChaCha20QuarterRound(std::uint32_t& a, std::uint32_t& b,
		                                 std::uint32_t& c, std::uint32_t& d) noexcept
		{
			a += b; d ^= a; d = RotL(d, 16);
			c += d; b ^= c; b = RotL(b, 12);
			a += b; d ^= a; d = RotL(d, 8);
			c += d; b ^= c; b = RotL(b, 7);
		}
