#ifndef RANDX_TESTS_COMMON_TYPE_CAPABILITY_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_TYPE_CAPABILITY_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "doctest.h"
#include "fixtures.hpp"

namespace RandXTest
{
namespace TypeCapabilityFixtures
{
inline constexpr std::size_t kFillOutputCount = 4;
inline constexpr std::size_t kShuffleOutputCount = 6;
inline constexpr std::size_t kCopyOnlyPopulationSize = 5;
inline constexpr std::size_t kCopyOnlySampleSize = 2;
inline constexpr int kCharacterIntegerUpperBound = 15;

struct CopyOnlySampleValue
{
	int value{};

	CopyOnlySampleValue() = default;
	explicit CopyOnlySampleValue(int initialValue) : value(initialValue) {}
	CopyOnlySampleValue(const CopyOnlySampleValue&) = default;
	CopyOnlySampleValue(CopyOnlySampleValue&&) = delete;
	CopyOnlySampleValue& operator=(const CopyOnlySampleValue&) = default;
};

struct MoveOnlyShuffleValue
{
	int value{};

	explicit MoveOnlyShuffleValue(int initialValue) : value(initialValue) {}
	MoveOnlyShuffleValue(const MoveOnlyShuffleValue&) = delete;
	MoveOnlyShuffleValue& operator=(const MoveOnlyShuffleValue&) = delete;
	MoveOnlyShuffleValue(MoveOnlyShuffleValue&&) = default;
	MoveOnlyShuffleValue& operator=(MoveOnlyShuffleValue&&) = default;
};

template <class Iterator, class T, class = void>
struct HasDefaultRandFill : std::false_type {};

template <class Iterator, class T>
struct HasDefaultRandFill<Iterator, T, std::void_t<decltype(
	RandX::RandFill(std::declval<Iterator>(), std::declval<Iterator>(),
		std::declval<T>(), std::declval<T>()))>> : std::true_type {};

template <class Iterator, class T, class = void>
struct HasExplicitRandFill : std::false_type {};

template <class Iterator, class T>
struct HasExplicitRandFill<Iterator, T, std::void_t<decltype(
	RandX::RandFill(std::declval<RandX::Xoshiro256StarStar&>(),
		std::declval<Iterator>(), std::declval<Iterator>(),
		std::declval<T>(), std::declval<T>()))>> : std::true_type {};

template <class T, class = void>
struct HasDefaultRandVector : std::false_type {};

template <class T>
struct HasDefaultRandVector<T, std::void_t<decltype(
	RandX::RandVector<T>(std::declval<T>(), std::declval<T>(), std::declval<std::size_t>()))>>
	: std::true_type {};

template <class T, class = void>
struct HasExplicitRandVector : std::false_type {};

template <class T>
struct HasExplicitRandVector<T, std::void_t<decltype(
	RandX::RandVector<T>(std::declval<RandX::Xoshiro256StarStar&>(),
		std::declval<T>(), std::declval<T>(), std::declval<std::size_t>()))>>
	: std::true_type {};

template <class Container, class = void>
struct HasRandShuffle : std::false_type {};

template <class Container>
struct HasRandShuffle<Container, std::void_t<decltype(
	RandX::RandShuffle(std::declval<Container&>()))>> : std::true_type {};

template <class T>
void CheckCharacterIntegerGeneration()
{
	constexpr T lowerBound{};
	constexpr T upperBound = static_cast<T>(kCharacterIntegerUpperBound);
	std::array<T, kFillOutputCount> filled{};
	RandX::RandFill(filled.begin(), filled.end(), lowerBound, upperBound);
	for (const T value : filled)
		CHECK((value >= lowerBound && value <= upperBound));

	RandX::Xoshiro256StarStar engine{ RandXTest::TestConstants::kDefaultEngineTestSeed };
	RandX::RandFill(engine, filled.begin(), filled.end(), lowerBound, upperBound);
	for (const T value : filled)
		CHECK((value >= lowerBound && value <= upperBound));

	const auto defaultValues = RandX::RandVector<T>(lowerBound, upperBound, kFillOutputCount);
	const auto explicitValues = RandX::RandVector<T>(engine, lowerBound, upperBound, kFillOutputCount);
	for (const T value : defaultValues)
		CHECK((value >= lowerBound && value <= upperBound));
	for (const T value : explicitValues)
		CHECK((value >= lowerBound && value <= upperBound));
}

template <class Range>
void CheckCopyOnlySampleValues(const Range& values)
{
	CHECK(values.size() == kCopyOnlySampleSize);
	for (const auto& value : values)
		CHECK((value.value >= 0
			&& static_cast<std::size_t>(value.value) < kCopyOnlyPopulationSize));
}
}

static_assert(std::is_copy_constructible_v<TypeCapabilityFixtures::CopyOnlySampleValue>);
static_assert(!std::is_move_constructible_v<TypeCapabilityFixtures::CopyOnlySampleValue>);
static_assert(!TypeCapabilityFixtures::HasDefaultRandFill<std::vector<int>::iterator, bool>::value);
static_assert(!TypeCapabilityFixtures::HasExplicitRandFill<std::vector<int>::iterator, bool>::value);
static_assert(!TypeCapabilityFixtures::HasDefaultRandFill<std::array<bool,
	TypeCapabilityFixtures::kFillOutputCount>::iterator, bool>::value);
static_assert(!TypeCapabilityFixtures::HasExplicitRandFill<std::array<bool,
	TypeCapabilityFixtures::kFillOutputCount>::iterator, bool>::value);
static_assert(TypeCapabilityFixtures::HasDefaultRandFill<std::vector<bool>::iterator, bool>::value == false);
static_assert(TypeCapabilityFixtures::HasExplicitRandFill<std::vector<bool>::iterator, bool>::value == false);
static_assert(!TypeCapabilityFixtures::HasDefaultRandVector<bool>::value);
static_assert(!TypeCapabilityFixtures::HasExplicitRandVector<bool>::value);
static_assert(!TypeCapabilityFixtures::HasRandShuffle<std::string_view>::value);
static_assert(!TypeCapabilityFixtures::HasRandShuffle<const std::vector<int>>::value);
static_assert(TypeCapabilityFixtures::HasRandShuffle<std::vector<bool>>::value);
static_assert(TypeCapabilityFixtures::HasRandShuffle<int[TypeCapabilityFixtures::kShuffleOutputCount]>::value);
static_assert(TypeCapabilityFixtures::HasRandShuffle<
	std::vector<TypeCapabilityFixtures::MoveOnlyShuffleValue>>::value);

static_assert(TypeCapabilityFixtures::HasDefaultRandFill<std::array<char16_t,
	TypeCapabilityFixtures::kFillOutputCount>::iterator, char16_t>::value);
static_assert(TypeCapabilityFixtures::HasExplicitRandFill<std::array<char16_t,
	TypeCapabilityFixtures::kFillOutputCount>::iterator, char16_t>::value);
static_assert(TypeCapabilityFixtures::HasDefaultRandVector<char16_t>::value);
static_assert(TypeCapabilityFixtures::HasExplicitRandVector<char16_t>::value);

TEST_SUITE("公共/基础/类型能力")
{
	TEST_CASE("RandFill 与 RandVector 支持字符整数类型")
	{
		using namespace TypeCapabilityFixtures;
		CheckCharacterIntegerGeneration<char16_t>();
		CheckCharacterIntegerGeneration<char32_t>();
		CheckCharacterIntegerGeneration<wchar_t>();
	}

	TEST_CASE("RandShuffle 保留代理引用与仅移动元素")
	{
		using namespace TypeCapabilityFixtures;

		const std::array<bool, kShuffleOutputCount> initialProxyValues{
			true, false, true, true, false, false };
		std::vector<bool> proxyValues(initialProxyValues.begin(), initialProxyValues.end());
		int rawValues[kShuffleOutputCount]{};
		RandX::RandShuffle(rawValues);
		CHECK(std::all_of(std::begin(rawValues), std::end(rawValues), [](int value) { return value == 0; }));
		const auto trueCount = std::count(proxyValues.begin(), proxyValues.end(), true);
		RandX::RandShuffle(proxyValues);
		CHECK(std::count(proxyValues.begin(), proxyValues.end(), true) == trueCount);

		std::vector<MoveOnlyShuffleValue> moveOnlyValues;
		moveOnlyValues.reserve(kShuffleOutputCount);
		for (std::size_t index = 0; index < kShuffleOutputCount; ++index)
			moveOnlyValues.emplace_back(static_cast<int>(index));
		RandX::RandShuffle(moveOnlyValues);

		std::vector<int> observedValues;
		observedValues.reserve(moveOnlyValues.size());
		for (const auto& value : moveOnlyValues)
			observedValues.push_back(value.value);
		std::sort(observedValues.begin(), observedValues.end());
		for (std::size_t index = 0; index < observedValues.size(); ++index)
			CHECK(observedValues[index] == static_cast<int>(index));
	}

	TEST_CASE("RandSample 与 RandElement 支持显式删除移动的可复制元素")
	{
		using namespace TypeCapabilityFixtures;
		std::list<CopyOnlySampleValue> inputValues;
		std::array<CopyOnlySampleValue, kCopyOnlyPopulationSize> containerValues{};
		for (std::size_t index = 0; index < kCopyOnlyPopulationSize; ++index)
		{
			const int value = static_cast<int>(index);
			inputValues.emplace_back(value);
			containerValues[index].value = value;
		}

		RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
		const auto defaultElement = RandX::RandElement(inputValues.begin(), inputValues.end());
		CHECK(defaultElement.value >= 0);
// MSVC STL 的 vector 增长会实例化元素移动构造。
#if !defined(_MSC_VER)
		const auto defaultInputSample = RandX::RandSample(
			inputValues.begin(), inputValues.end(), kCopyOnlySampleSize);
		CheckCopyOnlySampleValues(defaultInputSample);
		const auto defaultContainerSample = RandX::RandSample(containerValues, kCopyOnlySampleSize);
		CheckCopyOnlySampleValues(defaultContainerSample);
#endif

		RandX::Xoshiro256StarStar engine{ RandXTest::TestConstants::kDefaultEngineTestSeed };
		const auto explicitElement = RandX::RandElement(engine, inputValues.begin(), inputValues.end());
		CHECK(explicitElement.value >= 0);
#if !defined(_MSC_VER)
		const auto explicitInputSample = RandX::RandSample(
			engine, inputValues.begin(), inputValues.end(), kCopyOnlySampleSize);
		CheckCopyOnlySampleValues(explicitInputSample);
		const auto explicitContainerSample = RandX::RandSample(engine, containerValues, kCopyOnlySampleSize);
		CheckCopyOnlySampleValues(explicitContainerSample);
#endif
	}
}

}

#endif
