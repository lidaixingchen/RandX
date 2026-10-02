	////////////////////////////////////////////////////////////////
	//
	//	静态断言：确认引擎满足 UniformRandomBitGenerator 要求
	//

	static_assert(std::is_same_v<SplitMix64::result_type, std::uint64_t>);
	static_assert(std::is_same_v<Xoshiro256StarStar::result_type, std::uint64_t>);
	static_assert(std::is_same_v<Xoroshiro128StarStar::result_type, std::uint64_t>);
	static_assert(std::is_same_v<Xoshiro128StarStar::result_type, std::uint32_t>);
	static_assert(std::is_same_v<Xoroshiro64StarStar::result_type, std::uint32_t>);
	static_assert(std::is_same_v<SFC64::result_type, std::uint64_t>);
	static_assert(std::is_same_v<RomuDuoJr::result_type, std::uint64_t>);
	static_assert(std::is_same_v<ChaCha20::result_type, std::uint64_t>);
	static_assert(SplitMix64::min() < SplitMix64::max());
	static_assert(Xoshiro256StarStar::min() < Xoshiro256StarStar::max());
	static_assert(Xoroshiro128StarStar::min() < Xoroshiro128StarStar::max());
	static_assert(Xoshiro128StarStar::min() < Xoshiro128StarStar::max());
	static_assert(Xoroshiro64StarStar::min() < Xoroshiro64StarStar::max());
	static_assert(SFC64::min() < SFC64::max());
	static_assert(RomuDuoJr::min() < RomuDuoJr::max());
	static_assert(ChaCha20::min() < ChaCha20::max());

	namespace detail
	{
// @randx-include ../common/serialization/stream_format_guard_definition.inc
	}

	// ========================================================================
	// 流式运算符 operator<< / operator>>
	// 仅对 state_type 为可索引容器类的引擎生效（is_serializable_engine_v）
	// SplitMix64（state_type = uint64_t 标量）不支持，由 is_indexable_state_v 排除
	// 格式兼容 std::random_engine：空格分隔的十进制数序列
	// ========================================================================

	// 流式输出引擎状态
	template <class CharT, class Traits, class Engine,
		std::enable_if_t<detail::is_serializable_engine_v<Engine>>* = nullptr>
	std::basic_ostream<CharT, Traits>&
	operator<<(std::basic_ostream<CharT, Traits>& os, const Engine& engine)
// @randx-include ../common/serialization/stream_write_body.inc

	// 流式恢复引擎状态
	// 若解析失败（读取不足、流错误或状态非法/全零），setstate(failbit) 且引擎状态保持不变
	// （与 std::random_engine 一致：先读取到临时 state，全部成功才 deserialize）
	template <class CharT, class Traits, class Engine,
		std::enable_if_t<detail::is_serializable_engine_v<Engine>>* = nullptr>
	std::basic_istream<CharT, Traits>&
	operator>>(std::basic_istream<CharT, Traits>& is, Engine& engine)
// @randx-include ../common/serialization/stream_read_body.inc
