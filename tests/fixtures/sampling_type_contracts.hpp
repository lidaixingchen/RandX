#ifndef RANDX_TESTS_FIXTURES_SAMPLING_TYPE_CONTRACTS_HPP
#define RANDX_TESTS_FIXTURES_SAMPLING_TYPE_CONTRACTS_HPP

#include <array>
#include <cstddef>
#include <vector>

namespace RandXTest
{
namespace SamplingTypeFixtures
{
inline constexpr std::size_t kSizedIndexedValueCount{3};
inline constexpr std::size_t kEmptySizedIndexedValueCount{0};
inline constexpr std::size_t kShortReservoirValueCount{2};
inline constexpr int kFirstSizedIndexedValue{17};
inline constexpr int kSecondSizedIndexedValue{29};
inline constexpr int kThirdSizedIndexedValue{43};
inline constexpr int kFirstReservoirValue{59};
inline constexpr int kSecondReservoirValue{61};

class SizedIndexedContainer
{
public:
	using size_type = std::size_t;

	explicit SizedIndexedContainer(size_type count = kSizedIndexedValueCount) noexcept
		: count_(count) {}

	[[nodiscard]] size_type size() const noexcept { return count_; }
	[[nodiscard]] int* begin() noexcept { return values_.data(); }
	[[nodiscard]] const int* begin() const noexcept { return values_.data(); }
	[[nodiscard]] int* end() noexcept { return values_.data() + count_; }
	[[nodiscard]] const int* end() const noexcept { return values_.data() + count_; }
	[[nodiscard]] int& operator[](size_type index) noexcept { return values_[index]; }
	[[nodiscard]] const int& operator[](size_type index) const noexcept { return values_[index]; }

private:
	std::array<int, kSizedIndexedValueCount> values_{
		kFirstSizedIndexedValue,
		kSecondSizedIndexedValue,
		kThirdSizedIndexedValue};
	size_type count_;
};

class MutableOnlyRandomAccessRange
{
public:
	using iterator = std::vector<int>::iterator;

	MutableOnlyRandomAccessRange()
		: values_{
			kFirstSizedIndexedValue,
			kSecondSizedIndexedValue,
			kThirdSizedIndexedValue} {}

	[[nodiscard]] iterator begin() noexcept { return values_.begin(); }
	[[nodiscard]] iterator end() noexcept { return values_.end(); }
	[[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

private:
	std::vector<int> values_;
};
}
}

#endif
