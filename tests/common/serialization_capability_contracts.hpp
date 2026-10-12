#ifndef RANDX_TESTS_COMMON_SERIALIZATION_CAPABILITY_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_SERIALIZATION_CAPABILITY_CONTRACTS_HPP

#include <array>
#include <cstdint>
#include <istream>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>
#include "doctest.h"

namespace RandXTest::SerializationCapability
{
	template <class State>
	struct CustomStateEngine
	{
		using state_type = State;

		state_type state{};

		[[nodiscard]] state_type serialize() const
		{
			return state;
		}

		void deserialize(const state_type& value)
		{
			state = value;
		}
	};

	template <class State>
	struct NonVoidDeserializeEngine
	{
		using state_type = State;

		state_type state{};

		[[nodiscard]] state_type serialize() const
		{
			return state;
		}

		int deserialize(const state_type& value)
		{
			state = value;
			return 0;
		}
	};

	template <class State>
	struct RvalueOnlyDeserializeEngine
	{
		using state_type = State;

		[[nodiscard]] state_type serialize() const
		{
			return {};
		}

		void deserialize(state_type&&)
		{
		}
	};

	template <class Engine, class = void>
	struct HasStreamInputOperator : std::false_type {};

	template <class Engine>
	struct HasStreamInputOperator<Engine, std::void_t<decltype(
		RandX::operator>>(std::declval<std::istream&>(), std::declval<Engine&>()))>>
		: std::true_type {};


	using Uint32ArrayEngine = CustomStateEngine<std::array<std::uint32_t, 2>>;
	using Uint64ArrayEngine = CustomStateEngine<std::array<std::uint64_t, 2>>;
	using Uint16ArrayEngine = CustomStateEngine<std::array<std::uint16_t, 2>>;
	using EmptyArrayEngine = CustomStateEngine<std::array<std::uint32_t, 0>>;
	using VectorStateEngine = CustomStateEngine<std::vector<std::uint32_t>>;

	static_assert(HasStreamInputOperator<Uint32ArrayEngine>::value);
	static_assert(HasStreamInputOperator<Uint64ArrayEngine>::value);
	static_assert(HasStreamInputOperator<Uint32ArrayEngine>::value);
	static_assert(HasStreamInputOperator<Uint64ArrayEngine>::value);
	static_assert(!HasStreamInputOperator<Uint16ArrayEngine>::value);
	static_assert(!HasStreamInputOperator<EmptyArrayEngine>::value);
	static_assert(!HasStreamInputOperator<VectorStateEngine>::value);
	static_assert(!HasStreamInputOperator<NonVoidDeserializeEngine<std::array<std::uint32_t, 2>>>::value);
	static_assert(!HasStreamInputOperator<RvalueOnlyDeserializeEngine<std::array<std::uint32_t, 2>>>::value);
	static_assert(!HasStreamInputOperator<Uint16ArrayEngine>::value);
	static_assert(!HasStreamInputOperator<VectorStateEngine>::value);
	static_assert(!HasStreamInputOperator<NonVoidDeserializeEngine<std::array<std::uint32_t, 2>>>::value);
	static_assert(!HasStreamInputOperator<RvalueOnlyDeserializeEngine<std::array<std::uint32_t, 2>>>::value);

	static_assert(HasStreamInputOperator<RandX::Xoshiro256StarStar>::value);
	static_assert(HasStreamInputOperator<RandX::Xoroshiro128StarStar>::value);
	static_assert(HasStreamInputOperator<RandX::Xoshiro128StarStar>::value);
	static_assert(HasStreamInputOperator<RandX::Xoroshiro64StarStar>::value);
	static_assert(HasStreamInputOperator<RandX::SFC64>::value);
	static_assert(HasStreamInputOperator<RandX::RomuDuoJr>::value);
	static_assert(!HasStreamInputOperator<RandX::SplitMix64>::value);
	static_assert(!HasStreamInputOperator<RandX::ChaCha20>::value);

	static_assert(HasStreamInputOperator<RandX::Xoshiro256StarStar>::value);
}

TEST_SUITE("公共/基础/序列化能力")
{
	TEST_CASE("固定 32 位与 64 位数组状态可从流恢复")
	{
		using namespace RandXTest::SerializationCapability;

		std::istringstream uint32Input("17 29");
		Uint32ArrayEngine uint32Engine;
		const Uint32ArrayEngine::state_type expectedUint32{ 17U, 29U };
		RandX::operator>>(uint32Input, uint32Engine);
		CHECK_FALSE(uint32Input.fail());
		CHECK(uint32Engine.serialize() == expectedUint32);

		std::istringstream uint64Input("31 47");
		Uint64ArrayEngine uint64Engine;
		const Uint64ArrayEngine::state_type expectedUint64{ 31U, 47U };
		RandX::operator>>(uint64Input, uint64Engine);
		CHECK_FALSE(uint64Input.fail());
		CHECK(uint64Engine.serialize() == expectedUint64);
	}
}

#endif
