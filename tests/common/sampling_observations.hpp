#ifndef RANDX_TESTS_COMMON_SAMPLING_OBSERVATIONS_HPP
#define RANDX_TESTS_COMMON_SAMPLING_OBSERVATIONS_HPP

#ifdef RANDX_PARITY_CPP17
#include <RandX_Cpp17.hpp>
#else
#include <RandX.hpp>
#endif

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace SamplingObservations
{
namespace Parameters
{
inline constexpr std::uint64_t kDefaultSeed = 0x5A17C0DEULL;
inline constexpr std::uint64_t kExplicitSeed = 0x6B28D1EFULL;
inline constexpr std::uint64_t kSecondCallSeed = 0x7C39E2F0ULL;
inline constexpr std::size_t kZeroCount = 0;
inline constexpr std::size_t kOneCount = 1;
inline constexpr std::ptrdiff_t kZeroDifferenceRequest = 0;
inline constexpr std::size_t kObservationFormatVersion = 1;
inline constexpr std::size_t kHashThresholdRequest = 8;
inline constexpr std::size_t kHashPopulation =
    static_cast<std::size_t>(RandX::detail::HashSetThresholdK) * kHashThresholdRequest + kOneCount;
inline constexpr std::size_t kHashRequestAboveThreshold = kHashThresholdRequest + kOneCount;
inline constexpr std::size_t kBitmapThresholdPopulation =
    static_cast<std::size_t>(RandX::detail::SampleBitmapThresholdK) *
    static_cast<std::size_t>(RandX::detail::SampleBitmapDensityDivisor);
inline constexpr std::size_t kBitmapRequestAtThreshold =
    (kBitmapThresholdPopulation - kOneCount) /
    static_cast<std::size_t>(RandX::detail::SampleBitmapThresholdK);
inline constexpr std::size_t kBitmapRequestAboveThreshold = kBitmapRequestAtThreshold + kOneCount;
inline constexpr std::size_t kBitmapWordRequest =
	static_cast<std::size_t>(RandX::detail::SampleBitmapWordBits);
inline constexpr std::size_t kBitmapWordRequestAbove = kBitmapWordRequest + kOneCount;
inline constexpr std::size_t kInputPopulation = 12;
inline constexpr std::size_t kReservoirRequest = 3;
inline constexpr std::size_t kSmallPopulation = 6;
inline constexpr std::size_t kOversampleExtra = 2;
inline constexpr std::size_t kCopyFailureAt = 2;
inline constexpr std::size_t kAssignmentFailureAt = 1;
inline constexpr std::size_t kEngineFailureAt = 1;
inline constexpr std::size_t kNoFailure = 0;
inline constexpr std::uint64_t kMixIncrement = 0x9E3779B97F4A7C15ULL;
inline constexpr std::uint64_t kMixMultiplierOne = 0xBF58476D1CE4E5B9ULL;
inline constexpr std::uint64_t kMixMultiplierTwo = 0x94D049BB133111EBULL;
inline constexpr unsigned int kMixRightShiftOne = 30;
inline constexpr unsigned int kMixRightShiftTwo = 27;
inline constexpr unsigned int kMixRightShiftThree = 31;
inline constexpr std::uint32_t kNonzeroEngineMinimum = 10;
inline constexpr std::uint32_t kNonzeroEngineMaximum = 20;
inline constexpr std::size_t kModernRangePopulation = 10;
inline constexpr std::size_t kModernRangeRequest = 4;
inline constexpr std::size_t kWideRangeExtraLength = 5;
}

struct OperationRecord
{
	std::size_t reads{0};
	std::size_t dereferences{0};
	std::size_t advances{0};
	std::size_t comparisons{0};
	std::size_t beginCalls{0};
	std::size_t endCalls{0};
	std::size_t sizeCalls{0};
	std::size_t copyAttempts{0};
	std::size_t successfulCopies{0};
	std::size_t assignmentAttempts{0};
	std::size_t successfulAssignments{0};
	std::size_t copyFailureAt{Parameters::kNoFailure};
	std::size_t assignmentFailureAt{Parameters::kNoFailure};
};

class EngineFailure : public std::runtime_error
{
public:
	EngineFailure() : std::runtime_error("controlled engine failure") {}
};

class CopyFailure : public std::runtime_error
{
public:
	CopyFailure() : std::runtime_error("controlled copy failure") {}
};

class AssignmentFailure : public std::runtime_error
{
public:
	AssignmentFailure() : std::runtime_error("controlled assignment failure") {}
};

template <bool Copyable>
struct EngineCopyControl
{
};

template <>
struct EngineCopyControl<false>
{
	EngineCopyControl() = default;
	EngineCopyControl(const EngineCopyControl&) = delete;
	EngineCopyControl& operator=(const EngineCopyControl&) = delete;
	EngineCopyControl(EngineCopyControl&&) = default;
	EngineCopyControl& operator=(EngineCopyControl&&) = default;
};

template <class ResultType, ResultType Minimum, ResultType Maximum, bool Copyable = true>
class ControlledEngine : private EngineCopyControl<Copyable>
{
public:
	using result_type = ResultType;
	static constexpr result_type min() noexcept { return Minimum; }
	static constexpr result_type max() noexcept { return Maximum; }

	explicit ControlledEngine(
		std::uint64_t seed = Parameters::kExplicitSeed,
		std::size_t throwAtAttempt = Parameters::kNoFailure) noexcept
		: seed_(seed), state_(seed), throwAtAttempt_(throwAtAttempt)
	{
	}

	result_type operator()()
	{
		++attempts_;
		if (throwAtAttempt_ != Parameters::kNoFailure && attempts_ == throwAtAttempt_)
			throw EngineFailure{};

		state_ += Parameters::kMixIncrement;
		std::uint64_t mixed = state_;
		mixed = (mixed ^ (mixed >> Parameters::kMixRightShiftOne)) * Parameters::kMixMultiplierOne;
		mixed = (mixed ^ (mixed >> Parameters::kMixRightShiftTwo)) * Parameters::kMixMultiplierTwo;
		mixed ^= mixed >> Parameters::kMixRightShiftThree;
		++successfulCalls_;

		if constexpr (Minimum == result_type{0} &&
			Maximum == (std::numeric_limits<result_type>::max)())
		{
			return static_cast<result_type>(mixed);
		}
		else
		{
			const auto width = static_cast<std::uint64_t>(Maximum - Minimum) + Parameters::kOneCount;
			return static_cast<result_type>(Minimum + static_cast<result_type>(mixed % width));
		}
	}

	std::uint64_t seed() const noexcept { return seed_; }
	std::uint64_t state() const noexcept { return state_; }
	std::size_t attempts() const noexcept { return attempts_; }
	std::size_t successfulCalls() const noexcept { return successfulCalls_; }
	std::size_t throwAtAttempt() const noexcept { return throwAtAttempt_; }

private:
	std::uint64_t seed_;
	std::uint64_t state_;
	std::size_t attempts_{0};
	std::size_t successfulCalls_{0};
	std::size_t throwAtAttempt_;
};

using Controlled64Engine = ControlledEngine<
	std::uint64_t,
	std::uint64_t{0},
	(std::numeric_limits<std::uint64_t>::max)()>;
using Controlled32Engine = ControlledEngine<
	std::uint32_t,
	std::uint32_t{0},
	(std::numeric_limits<std::uint32_t>::max)()>;
using NonzeroMinimumEngine = ControlledEngine<
	std::uint32_t,
	Parameters::kNonzeroEngineMinimum,
	Parameters::kNonzeroEngineMaximum>;
using NoncopyableEngine = ControlledEngine<
	std::uint64_t,
	std::uint64_t{0},
	(std::numeric_limits<std::uint64_t>::max)(),
	false>;

class ZeroEngine
{
public:
	using result_type = std::uint64_t;
	static constexpr result_type min() noexcept { return 0; }
	static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }
	result_type operator()() noexcept
	{
		++calls_;
		return 0;
	}
	std::size_t calls() const noexcept { return calls_; }

private:
	std::size_t calls_{0};
};

struct ObservedValue
{
	std::size_t position;
	OperationRecord* operations;

	ObservedValue(std::size_t sourcePosition, OperationRecord* record) noexcept
		: position(sourcePosition), operations(record)
	{
	}

	ObservedValue(const ObservedValue& other)
		: position(other.position), operations(other.operations)
	{
		if (operations != nullptr)
		{
			++operations->copyAttempts;
			if (operations->copyFailureAt != Parameters::kNoFailure &&
				operations->copyAttempts == operations->copyFailureAt)
				throw CopyFailure{};
			++operations->successfulCopies;
		}
	}

	ObservedValue(ObservedValue&& other) noexcept
		: position(other.position), operations(other.operations)
	{
	}

	ObservedValue& operator=(const ObservedValue& other)
	{
		OperationRecord* record = operations != nullptr ? operations : other.operations;
		if (record != nullptr)
		{
			++record->assignmentAttempts;
			if (record->assignmentFailureAt != Parameters::kNoFailure &&
				record->assignmentAttempts == record->assignmentFailureAt)
				throw AssignmentFailure{};
			++record->successfulAssignments;
		}
		position = other.position;
		operations = other.operations;
		return *this;
	}

	ObservedValue& operator=(ObservedValue&& other) noexcept
	{
		position = other.position;
		operations = other.operations;
		return *this;
	}
};

struct StreamProgress
{
	std::size_t valuesRead{0};
};

inline StreamProgress*& ActiveStreamProgress() noexcept
{
	static StreamProgress* progress = nullptr;
	return progress;
}

struct StreamValue
{
	std::size_t value{0};

	friend std::istream& operator>>(std::istream& input, StreamValue& result)
	{
		if (input >> result.value)
		{
			if (ActiveStreamProgress() != nullptr)
				++ActiveStreamProgress()->valuesRead;
		}
		return input;
	}
};

template <class Value>
class ProgressInputIterator
{
public:
	using iterator_category = std::input_iterator_tag;
	using value_type = Value;
	using difference_type = std::ptrdiff_t;
	using pointer = Value*;
	using reference = Value&;

	ProgressInputIterator() = default;
	ProgressInputIterator(std::vector<Value>* values, OperationRecord* operations, std::size_t position) noexcept
		: values_(values), operations_(operations), position_(position)
	{
	}

	reference operator*() const
	{
		if (operations_ != nullptr)
			++operations_->dereferences;
		return (*values_)[position_];
	}

	ProgressInputIterator& operator++()
	{
		++position_;
		if (operations_ != nullptr)
			++operations_->advances;
		return *this;
	}

	ProgressInputIterator operator++(int)
	{
		ProgressInputIterator previous = *this;
		++*this;
		return previous;
	}

	friend bool operator==(const ProgressInputIterator& left, const ProgressInputIterator& right)
	{
		if (left.operations_ != nullptr)
			++left.operations_->comparisons;
		return left.values_ == right.values_ && left.position_ == right.position_;
	}

	friend bool operator!=(const ProgressInputIterator& left, const ProgressInputIterator& right)
	{
		return !(left == right);
	}

private:
	std::vector<Value>* values_{nullptr};
	OperationRecord* operations_{nullptr};
	std::size_t position_{0};
};

class TrackedRandomAccessIterator
{
public:
	using iterator_category = std::random_access_iterator_tag;
	using iterator_concept = std::random_access_iterator_tag;
	using value_type = int;
	using difference_type = std::ptrdiff_t;
	using pointer = const int*;
	using reference = const int&;

	TrackedRandomAccessIterator() = default;
	TrackedRandomAccessIterator(const int* data, OperationRecord* operations) noexcept
		: data_(data), operations_(operations)
	{
	}

	reference operator*() const
	{
		if (operations_ != nullptr)
			++operations_->dereferences;
		return *data_;
	}

	reference operator[](difference_type offset) const
	{
		if (operations_ != nullptr)
			++operations_->reads;
		return data_[offset];
	}

	TrackedRandomAccessIterator& operator++() { ++data_; return *this; }
	TrackedRandomAccessIterator operator++(int)
	{
		TrackedRandomAccessIterator previous = *this;
		++*this;
		return previous;
	}
	TrackedRandomAccessIterator& operator--() { --data_; return *this; }
	TrackedRandomAccessIterator operator--(int)
	{
		TrackedRandomAccessIterator previous = *this;
		--*this;
		return previous;
	}
	TrackedRandomAccessIterator& operator+=(difference_type offset) { data_ += offset; return *this; }
	TrackedRandomAccessIterator& operator-=(difference_type offset) { data_ -= offset; return *this; }

	friend TrackedRandomAccessIterator operator+(TrackedRandomAccessIterator iterator, difference_type offset)
	{
		iterator += offset;
		return iterator;
	}
	friend TrackedRandomAccessIterator operator+(difference_type offset, TrackedRandomAccessIterator iterator)
	{
		iterator += offset;
		return iterator;
	}
	friend TrackedRandomAccessIterator operator-(TrackedRandomAccessIterator iterator, difference_type offset)
	{
		iterator -= offset;
		return iterator;
	}
	friend difference_type operator-(TrackedRandomAccessIterator left, TrackedRandomAccessIterator right)
	{
		if (left.data_ == right.data_)
			return static_cast<difference_type>(Parameters::kZeroCount);
		return left.data_ - right.data_;
	}
	friend bool operator==(TrackedRandomAccessIterator left, TrackedRandomAccessIterator right)
	{
		return left.data_ == right.data_;
	}
	friend bool operator!=(TrackedRandomAccessIterator left, TrackedRandomAccessIterator right)
	{
		return !(left == right);
	}
	friend bool operator<(TrackedRandomAccessIterator left, TrackedRandomAccessIterator right)
	{
		return left.data_ < right.data_;
	}
	friend bool operator>(TrackedRandomAccessIterator left, TrackedRandomAccessIterator right)
	{
		return right < left;
	}
	friend bool operator<=(TrackedRandomAccessIterator left, TrackedRandomAccessIterator right)
	{
		return !(right < left);
	}
	friend bool operator>=(TrackedRandomAccessIterator left, TrackedRandomAccessIterator right)
	{
		return !(left < right);
	}

private:
	const int* data_{nullptr};
	OperationRecord* operations_{nullptr};
};

class TrackedContainer
{
public:
	TrackedContainer(std::size_t length, OperationRecord* operations)
		: values_(length), operations_(operations)
	{
		for (std::size_t index = 0; index < length; ++index)
			values_[index] = static_cast<int>(index);
	}

	TrackedRandomAccessIterator begin() const
	{
		if (operations_ != nullptr)
			++operations_->beginCalls;
		return TrackedRandomAccessIterator(values_.data(), operations_);
	}

	TrackedRandomAccessIterator end() const
	{
		if (operations_ != nullptr)
			++operations_->endCalls;
		const int* end = values_.data();
		if (!values_.empty())
			end += values_.size();
		return TrackedRandomAccessIterator(end, operations_);
	}

	std::size_t size() const noexcept
	{
		if (operations_ != nullptr)
			++operations_->sizeCalls;
		return values_.size();
	}

private:
	std::vector<int> values_;
	OperationRecord* operations_;
};

inline std::vector<int> MakePopulation(std::size_t length)
{
	std::vector<int> values(length);
	for (std::size_t index = 0; index < length; ++index)
		values[index] = static_cast<int>(index);
	return values;
}

inline std::vector<ObservedValue> MakeObservedPopulation(std::size_t length, OperationRecord* operations)
{
	std::vector<ObservedValue> values;
	values.reserve(length);
	for (std::size_t index = 0; index < length; ++index)
		values.emplace_back(index, operations);
	return values;
}

inline const char* ExceptionCategory(std::exception_ptr error = std::current_exception())
{
	if (!error)
		return "none";
	try
	{
		std::rethrow_exception(error);
	}
	catch (const EngineFailure&)
	{
		return "engine_failure";
	}
	catch (const CopyFailure&)
	{
		return "copy_failure";
	}
	catch (const AssignmentFailure&)
	{
		return "assignment_failure";
	}
	catch (const std::length_error&)
	{
		return "length_error";
	}
	catch (const std::bad_alloc&)
	{
		return "bad_alloc";
	}
	catch (const std::invalid_argument&)
	{
		return "invalid_argument";
	}
	catch (const std::exception&)
	{
		return "std_exception";
	}
	catch (...)
	{
		return "unknown_exception";
	}
}

inline void PrintOperations(const OperationRecord& operations)
{
	std::cout << "source_progress reads=" << operations.reads
		<< " dereferences=" << operations.dereferences
		<< " advances=" << operations.advances
		<< " comparisons=" << operations.comparisons
		<< " begin=" << operations.beginCalls
		<< " end=" << operations.endCalls
		<< " size=" << operations.sizeCalls
		<< " copy_attempts=" << operations.copyAttempts
		<< " copies=" << operations.successfulCopies
		<< " assignment_attempts=" << operations.assignmentAttempts
		<< " assignments=" << operations.successfulAssignments << '\n';
}

template <class Engine>
inline void PrintControlledState(const Engine& engine)
{
	std::cout << "engine_state seed=" << engine.seed()
		<< " state=" << engine.state()
		<< " attempts=" << engine.attempts()
		<< " successful_calls=" << engine.successfulCalls()
		<< " throw_at=" << engine.throwAtAttempt() << '\n';
}

inline void PrintControlledState(const ZeroEngine& engine)
{
	std::cout << "engine_state kind=zero calls=" << engine.calls() << '\n';
}

template <class Engine>
inline void PrintSerializedState(const char* label, const Engine& engine)
{
	const auto state = engine.serialize();
	std::cout << "engine_state label=" << label << " serialized=";
	bool first = true;
	for (const auto word : state)
	{
		if (!first)
			std::cout << ',';
		first = false;
		std::cout << static_cast<unsigned long long>(word);
	}
	std::cout << '\n';
}

template <class Value>
inline std::size_t SamplePosition(const Value& value)
{
	return static_cast<std::size_t>(value);
}

inline std::size_t SamplePosition(const ObservedValue& value)
{
	return value.position;
}

inline std::size_t SamplePosition(const StreamValue& value)
{
	return value.value;
}

template <class Sample>
inline void PrintSample(const Sample& sample)
{
	std::cout << "sample count=" << sample.size() << " values=";
	for (std::size_t index = 0; index < sample.size(); ++index)
	{
		if (index != 0)
			std::cout << ',';
		std::cout << SamplePosition(sample[index]);
	}
	std::cout << '\n';
}

template <class Engine, class Call>
inline void RunControlledCase(
	const char* name,
	std::size_t populationSize,
	std::size_t request,
	Engine& engine,
	OperationRecord* operations,
	Call&& call)
{
	std::cout << "[case] " << name << " seed=" << engine.seed()
		<< " population=" << populationSize << " request=" << request << '\n';
	try
	{
		const auto sample = call();
		PrintSample(sample);
		std::cout << "exception=none\n";
	}
	catch (...)
	{
		std::cout << "exception=" << ExceptionCategory() << '\n';
	}
	if (operations != nullptr)
		PrintOperations(*operations);
	PrintControlledState(engine);
}

template <class Call>
inline void RunZeroEngineCase(
	const char* name,
	std::size_t populationSize,
	std::size_t request,
	ZeroEngine& engine,
	OperationRecord* operations,
	Call&& call)
{
	std::cout << "[case] " << name << " population=" << populationSize
		<< " request=" << request << '\n';
	try
	{
		const auto sample = call();
		PrintSample(sample);
		std::cout << "exception=none\n";
	}
	catch (...)
	{
		std::cout << "exception=" << ExceptionCategory() << '\n';
	}
	if (operations != nullptr)
		PrintOperations(*operations);
	PrintControlledState(engine);
}

inline void RunDefaultIteratorCase(const char* name, std::size_t populationSize, std::ptrdiff_t request)
{
	OperationRecord operations;
	TrackedContainer source(populationSize, &operations);
	RandX::Reseed(Parameters::kDefaultSeed);
	std::cout << "[case] " << name << " seed=" << Parameters::kDefaultSeed
		<< " population=" << populationSize << " request=" << request << '\n';
	try
	{
		const auto sample = RandX::RandSample(source.begin(), source.end(), request);
		PrintSample(sample);
		std::cout << "exception=none\n";
	}
	catch (...)
	{
		std::cout << "exception=" << ExceptionCategory() << '\n';
	}
	PrintOperations(operations);
	PrintSerializedState("default", RandX::DefaultEngine());
}

inline void RunDefaultContainerCase(const char* name, std::size_t populationSize, std::size_t request)
{
	OperationRecord operations;
	TrackedContainer source(populationSize, &operations);
	RandX::Reseed(Parameters::kDefaultSeed);
	std::cout << "[case] " << name << " seed=" << Parameters::kDefaultSeed
		<< " population=" << populationSize << " request=" << request << '\n';
	try
	{
		const auto sample = RandX::RandSample(source, request);
		PrintSample(sample);
		std::cout << "exception=none\n";
	}
	catch (...)
	{
		std::cout << "exception=" << ExceptionCategory() << '\n';
	}
	PrintOperations(operations);
	PrintSerializedState("default", RandX::DefaultEngine());
}

inline void RunDefaultReservoirCase(const char* name, std::size_t populationSize, std::ptrdiff_t request)
{
	OperationRecord operations;
	auto values = MakePopulation(populationSize);
	ProgressInputIterator<int> first(&values, &operations, Parameters::kZeroCount);
	ProgressInputIterator<int> last(&values, &operations, values.size());
	RandX::Reseed(Parameters::kDefaultSeed);
	std::cout << "[case] " << name << " seed=" << Parameters::kDefaultSeed
		<< " population=" << populationSize << " request=" << request << '\n';
	try
	{
		const auto sample = RandX::RandSample(first, last, request);
		PrintSample(sample);
		std::cout << "exception=none\n";
	}
	catch (...)
	{
		std::cout << "exception=" << ExceptionCategory() << '\n';
	}
	PrintOperations(operations);
	PrintSerializedState("default", RandX::DefaultEngine());
}

inline void RunDefaultCases()
{
	std::cout << "[sampling-default-parity]\n";
	RunDefaultIteratorCase("iterator-zero", Parameters::kSmallPopulation, Parameters::kZeroDifferenceRequest);
	RunDefaultIteratorCase("iterator-empty", Parameters::kZeroCount,
		static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
	RunDefaultIteratorCase("iterator-full", Parameters::kSmallPopulation,
		static_cast<std::ptrdiff_t>(Parameters::kSmallPopulation));
	RunDefaultIteratorCase("iterator-oversample", Parameters::kSmallPopulation,
		static_cast<std::ptrdiff_t>(Parameters::kSmallPopulation + Parameters::kOversampleExtra));
	RunDefaultIteratorCase("iterator-hash-threshold", Parameters::kHashPopulation,
		static_cast<std::ptrdiff_t>(Parameters::kHashThresholdRequest));
	RunDefaultIteratorCase("iterator-index-threshold", Parameters::kHashPopulation,
		static_cast<std::ptrdiff_t>(Parameters::kHashRequestAboveThreshold));
	RunDefaultContainerCase("container-bitmap-at-threshold", Parameters::kBitmapThresholdPopulation,
		Parameters::kBitmapRequestAtThreshold);
	RunDefaultContainerCase("container-bitmap-above-threshold", Parameters::kBitmapThresholdPopulation,
		Parameters::kBitmapRequestAboveThreshold);
	RunDefaultReservoirCase("reservoir-default", Parameters::kInputPopulation,
		static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));

	OperationRecord operations;
	TrackedContainer source(Parameters::kHashPopulation, &operations);
	RandX::Reseed(Parameters::kSecondCallSeed);
	std::cout << "[case] default-iterator-consecutive seed=" << Parameters::kSecondCallSeed
		<< " population=" << Parameters::kHashPopulation
		<< " requests=" << Parameters::kHashThresholdRequest << ','
		<< Parameters::kHashRequestAboveThreshold << '\n';
	const auto first = RandX::RandSample(source.begin(), source.end(),
		static_cast<std::ptrdiff_t>(Parameters::kHashThresholdRequest));
	PrintSample(first);
	PrintOperations(operations);
	PrintSerializedState("default-call-1", RandX::DefaultEngine());
	operations = OperationRecord{};
	const auto second = RandX::RandSample(source.begin(), source.end(),
		static_cast<std::ptrdiff_t>(Parameters::kHashRequestAboveThreshold));
	PrintSample(second);
	PrintOperations(operations);
	PrintSerializedState("default-call-2", RandX::DefaultEngine());
}

inline void RunExplicitCases()
{
	std::cout << "[sampling-explicit]\n";
	const std::size_t hashPopulation = Parameters::kHashPopulation;
	{
		OperationRecord operations;
		TrackedContainer source(hashPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-explicit-hash-threshold", hashPopulation,
			Parameters::kHashThresholdRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kHashThresholdRequest));
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(hashPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-explicit-index-threshold", hashPopulation,
			Parameters::kHashRequestAboveThreshold, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kHashRequestAboveThreshold));
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kSmallPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-explicit-zero", Parameters::kSmallPopulation,
			Parameters::kZeroCount, engine, &operations, [&]() {
			return RandX::RandSample(engine, source.begin(), source.end(), Parameters::kZeroDifferenceRequest);
		});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kZeroCount, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-explicit-empty", Parameters::kZeroCount,
			Parameters::kReservoirRequest, engine, &operations, [&]() {
			return RandX::RandSample(engine, source.begin(), source.end(),
				static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
		});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kSmallPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-explicit-full", Parameters::kSmallPopulation,
			Parameters::kSmallPopulation, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kSmallPopulation));
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kSmallPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		const std::size_t request = Parameters::kSmallPopulation + Parameters::kOversampleExtra;
		RunControlledCase("iterator-explicit-oversample", Parameters::kSmallPopulation,
			request, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(), static_cast<std::ptrdiff_t>(request));
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kBitmapThresholdPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("container-explicit-bitmap-threshold", Parameters::kBitmapThresholdPopulation,
			Parameters::kBitmapRequestAtThreshold, engine, &operations, [&]() {
				return RandX::RandSample(engine, source, Parameters::kBitmapRequestAtThreshold);
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kBitmapThresholdPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("container-explicit-bitmap-above-threshold", Parameters::kBitmapThresholdPopulation,
			Parameters::kBitmapRequestAboveThreshold, engine, &operations, [&]() {
				return RandX::RandSample(engine, source, Parameters::kBitmapRequestAboveThreshold);
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kBitmapThresholdPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("container-explicit-bitmap-word", Parameters::kBitmapThresholdPopulation,
			Parameters::kBitmapWordRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, source, Parameters::kBitmapWordRequest);
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kBitmapThresholdPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("container-explicit-bitmap-word-above", Parameters::kBitmapThresholdPopulation,
			Parameters::kBitmapWordRequestAbove, engine, &operations, [&]() {
				return RandX::RandSample(engine, source, Parameters::kBitmapWordRequestAbove);
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kSmallPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("container-explicit-zero", Parameters::kSmallPopulation,
			Parameters::kZeroCount, engine, &operations, [&]() {
			return RandX::RandSample(engine, source, Parameters::kZeroCount);
		});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kZeroCount, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("container-explicit-empty", Parameters::kZeroCount,
			Parameters::kReservoirRequest, engine, &operations, [&]() {
			return RandX::RandSample(engine, source, Parameters::kReservoirRequest);
		});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kSmallPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("container-explicit-full", Parameters::kSmallPopulation,
			Parameters::kSmallPopulation, engine, &operations, [&]() {
				return RandX::RandSample(engine, source, Parameters::kSmallPopulation);
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kSmallPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		const std::size_t request = (std::numeric_limits<std::size_t>::max)();
		RunControlledCase("container-explicit-oversample", Parameters::kSmallPopulation,
			request, engine, &operations, [&]() {
				return RandX::RandSample(engine, source, request);
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kHashPopulation, &operations);
		Controlled32Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-explicit-32-bit", Parameters::kHashPopulation,
			Parameters::kHashThresholdRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kHashThresholdRequest));
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kSmallPopulation, &operations);
		NonzeroMinimumEngine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-explicit-nonzero-minimum", Parameters::kSmallPopulation,
			Parameters::kReservoirRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kSmallPopulation, &operations);
		NoncopyableEngine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-explicit-noncopyable-engine", Parameters::kSmallPopulation,
			Parameters::kReservoirRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(hashPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed, Parameters::kEngineFailureAt);
		RunControlledCase("iterator-engine-failure", hashPopulation,
			Parameters::kHashThresholdRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kHashThresholdRequest));
			});
	}
	{
		OperationRecord operations;
		operations.copyFailureAt = Parameters::kCopyFailureAt;
		auto source = MakeObservedPopulation(hashPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-hash-copy-failure", hashPopulation,
			Parameters::kHashThresholdRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kHashThresholdRequest));
			});
	}
	{
		OperationRecord operations;
		operations.copyFailureAt = Parameters::kCopyFailureAt;
		auto source = MakeObservedPopulation(hashPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("iterator-index-copy-failure", hashPopulation,
			Parameters::kHashRequestAboveThreshold, engine, &operations, [&]() {
				return RandX::RandSample(engine, source.begin(), source.end(),
					static_cast<std::ptrdiff_t>(Parameters::kHashRequestAboveThreshold));
			});
	}
	{
		OperationRecord operations;
		operations.copyFailureAt = Parameters::kCopyFailureAt;
		auto source = MakeObservedPopulation(Parameters::kBitmapThresholdPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("container-bitmap-copy-failure", Parameters::kBitmapThresholdPopulation,
			Parameters::kBitmapRequestAboveThreshold, engine, &operations, [&]() {
				return RandX::RandSample(engine, source, Parameters::kBitmapRequestAboveThreshold);
			});
	}
	{
		OperationRecord operations;
		operations.copyFailureAt = Parameters::kCopyFailureAt;
		auto source = MakeObservedPopulation(Parameters::kSmallPopulation, &operations);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		const std::size_t request = (std::numeric_limits<std::size_t>::max)();
		RunControlledCase("container-full-copy-failure", Parameters::kSmallPopulation,
			request, engine, &operations, [&]() {
				return RandX::RandSample(engine, source, request);
			});
	}
	{
		OperationRecord operations;
		operations.copyFailureAt = Parameters::kCopyFailureAt;
		auto values = MakeObservedPopulation(Parameters::kInputPopulation, &operations);
		ProgressInputIterator<ObservedValue> first(&values, &operations, 0);
		ProgressInputIterator<ObservedValue> last(&values, &operations, values.size());
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("reservoir-initial-copy-failure", values.size(),
			Parameters::kReservoirRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, first, last,
					static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
			});
	}
	{
		OperationRecord operations;
		auto values = MakePopulation(Parameters::kInputPopulation);
		ProgressInputIterator<int> first(&values, &operations, Parameters::kZeroCount);
		ProgressInputIterator<int> last(&values, &operations, values.size());
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("reservoir-explicit-zero", values.size(), Parameters::kZeroCount,
			engine, &operations, [&]() {
				return RandX::RandSample(engine, first, last, Parameters::kZeroDifferenceRequest);
			});
	}
	{
		OperationRecord operations;
		std::vector<int> values;
		ProgressInputIterator<int> first(&values, &operations, Parameters::kZeroCount);
		ProgressInputIterator<int> last(&values, &operations, values.size());
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("reservoir-explicit-empty", values.size(), Parameters::kReservoirRequest,
			engine, &operations, [&]() {
				return RandX::RandSample(engine, first, last,
					static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
			});
	}
	{
		OperationRecord operations;
		auto values = MakePopulation(Parameters::kReservoirRequest);
		ProgressInputIterator<int> first(&values, &operations, Parameters::kZeroCount);
		ProgressInputIterator<int> last(&values, &operations, values.size());
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("reservoir-explicit-full", values.size(), Parameters::kReservoirRequest,
			engine, &operations, [&]() {
				return RandX::RandSample(engine, first, last,
					static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
			});
	}
	{
		OperationRecord operations;
		auto values = MakePopulation(Parameters::kReservoirRequest);
		ProgressInputIterator<int> first(&values, &operations, Parameters::kZeroCount);
		ProgressInputIterator<int> last(&values, &operations, values.size());
		Controlled64Engine engine(Parameters::kExplicitSeed);
		const std::size_t request = Parameters::kReservoirRequest + Parameters::kOversampleExtra;
		RunControlledCase("reservoir-explicit-oversample", values.size(), request,
			engine, &operations, [&]() {
				return RandX::RandSample(engine, first, last, static_cast<std::ptrdiff_t>(request));
			});
	}
	{
		OperationRecord operations;
		operations.assignmentFailureAt = Parameters::kAssignmentFailureAt;
		auto values = MakeObservedPopulation(Parameters::kInputPopulation, &operations);
		ProgressInputIterator<ObservedValue> first(&values, &operations, 0);
		ProgressInputIterator<ObservedValue> last(&values, &operations, values.size());
		ZeroEngine engine;
		RunZeroEngineCase("reservoir-replacement-assignment-failure", values.size(),
			Parameters::kReservoirRequest, engine, &operations, [&]() {
				return RandX::RandSample(engine, first, last,
					static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
			});
	}
	{
		OperationRecord operations;
		auto values = MakeObservedPopulation(Parameters::kInputPopulation, &operations);
		ProgressInputIterator<ObservedValue> first(&values, &operations, 0);
		ProgressInputIterator<ObservedValue> last(&values, &operations, values.size());
		Controlled64Engine engine(Parameters::kExplicitSeed);
		RunControlledCase("reservoir-explicit", values.size(), Parameters::kReservoirRequest,
			engine, &operations, [&]() {
				return RandX::RandSample(engine, first, last,
					static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
			});
	}
	{
		OperationRecord operations;
		TrackedContainer source(Parameters::kHashPopulation, &operations);
		Controlled64Engine engine(Parameters::kSecondCallSeed);
		std::cout << "[case] explicit-iterator-consecutive seed=" << engine.seed()
			<< " population=" << Parameters::kHashPopulation
			<< " requests=" << Parameters::kHashThresholdRequest << ','
			<< Parameters::kHashRequestAboveThreshold << '\n';
		const auto first = RandX::RandSample(engine, source.begin(), source.end(),
			static_cast<std::ptrdiff_t>(Parameters::kHashThresholdRequest));
		PrintSample(first);
		std::cout << "exception=none\n";
		PrintOperations(operations);
		PrintControlledState(engine);
		operations = OperationRecord{};
		const auto second = RandX::RandSample(engine, source.begin(), source.end(),
			static_cast<std::ptrdiff_t>(Parameters::kHashRequestAboveThreshold));
		PrintSample(second);
		std::cout << "exception=none\n";
		PrintOperations(operations);
		PrintControlledState(engine);
	}
}

#if !defined(RANDX_PARITY_CPP17) && __cplusplus > 202002L && \
	defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 201911L
inline void RunModernSentinelCase()
{
	OperationRecord operations;
	auto population = MakePopulation(Parameters::kModernRangePopulation);
	TrackedRandomAccessIterator begin(population.data(), &operations);
	std::counted_iterator<TrackedRandomAccessIterator> first(
		begin, static_cast<std::ptrdiff_t>(population.size()));
	std::default_sentinel_t last;
	Controlled64Engine engine(Parameters::kExplicitSeed);
	RunControlledCase("cpp23-counted-random-access-sentinel", population.size(),
		Parameters::kModernRangeRequest, engine, &operations, [&]() {
			return RandX::RandSample(engine, first, last,
				static_cast<std::ptrdiff_t>(Parameters::kModernRangeRequest));
		});
}

inline void RunMoveOnlyIstreamCases()
{
	std::ostringstream sourceText;
	for (std::size_t index = 0; index < Parameters::kInputPopulation; ++index)
		sourceText << index << ' ';

	{
		std::istringstream input(sourceText.str());
		std::ranges::istream_view<StreamValue> view(input);
		StreamProgress progress;
		ActiveStreamProgress() = &progress;
		RandX::Reseed(Parameters::kDefaultSeed);
		std::cout << "[case] cpp23-move-only-istream-range seed=" << Parameters::kDefaultSeed
			<< " population=" << Parameters::kInputPopulation
			<< " request=" << Parameters::kReservoirRequest << '\n';
		const auto sample = RandX::ranges::RandSample(view,
			static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
		PrintSample(sample);
		std::cout << "exception=none\n";
		std::cout << "source_progress values_read=" << progress.valuesRead
			<< " eof=" << (input.eof() ? "true" : "false") << '\n';
		PrintSerializedState("default", RandX::DefaultEngine());
		ActiveStreamProgress() = nullptr;
	}
	{
		std::istringstream input(sourceText.str());
		std::ranges::istream_view<StreamValue> view(input);
		StreamProgress progress;
		ActiveStreamProgress() = &progress;
		auto first = std::ranges::begin(view);
		auto last = std::ranges::end(view);
		Controlled64Engine engine(Parameters::kExplicitSeed);
		std::cout << "[case] cpp23-move-only-istream-explicit seed=" << engine.seed()
			<< " population=" << Parameters::kInputPopulation
			<< " request=" << Parameters::kReservoirRequest << '\n';
		try
		{
			const auto sample = RandX::RandSample(engine, std::move(first), last,
				static_cast<std::ptrdiff_t>(Parameters::kReservoirRequest));
			PrintSample(sample);
			std::cout << "exception=none\n";
		}
		catch (...)
		{
			std::cout << "exception=" << ExceptionCategory() << '\n';
		}
		std::cout << "source_progress values_read=" << progress.valuesRead
			<< " eof=" << (input.eof() ? "true" : "false") << '\n';
		PrintControlledState(engine);
		ActiveStreamProgress() = nullptr;
	}
}

#if defined(__SIZEOF_INT128__)
using WideDifference = __int128;

struct WideIterator
{
	using iterator_category = std::random_access_iterator_tag;
	using iterator_concept = std::random_access_iterator_tag;
	using value_type = int;
	using difference_type = WideDifference;
	using pointer = void;
	using reference = int;
	difference_type position{0};
	int operator*() const { return static_cast<int>(position); }
	int operator[](difference_type offset) const { return static_cast<int>(position + offset); }
	WideIterator& operator++() { ++position; return *this; }
	WideIterator operator++(int) { WideIterator old = *this; ++*this; return old; }
	WideIterator& operator--() { --position; return *this; }
	WideIterator operator--(int) { WideIterator old = *this; --*this; return old; }
	WideIterator& operator+=(difference_type offset) { position += offset; return *this; }
	WideIterator& operator-=(difference_type offset) { position -= offset; return *this; }
	friend WideIterator operator+(WideIterator iterator, difference_type offset) { iterator += offset; return iterator; }
	friend WideIterator operator+(difference_type offset, WideIterator iterator) { iterator += offset; return iterator; }
	friend WideIterator operator-(WideIterator iterator, difference_type offset) { iterator -= offset; return iterator; }
	friend difference_type operator-(WideIterator left, WideIterator right) { return left.position - right.position; }
	friend bool operator==(WideIterator left, WideIterator right) { return left.position == right.position; }
	friend bool operator!=(WideIterator left, WideIterator right) { return !(left == right); }
	friend bool operator<(WideIterator left, WideIterator right) { return left.position < right.position; }
	friend bool operator>(WideIterator left, WideIterator right) { return right < left; }
	friend bool operator<=(WideIterator left, WideIterator right) { return !(right < left); }
	friend bool operator>=(WideIterator left, WideIterator right) { return !(left < right); }
};

inline void RunWideDifferenceCases()
{
	constexpr WideDifference wideLength =
		(WideDifference{1} << std::numeric_limits<std::uint64_t>::digits) +
		static_cast<WideDifference>(Parameters::kWideRangeExtraLength);
	const WideIterator first{0};
	const WideIterator last{wideLength};
	{
		RandX::Reseed(Parameters::kDefaultSeed);
		std::cout << "[case] cpp23-wide-difference-zero seed=" << Parameters::kDefaultSeed
			<< " request=" << Parameters::kZeroCount << " length_over_uint64=true\n";
		try
		{
			const auto sample = RandX::RandSample(first, last, Parameters::kZeroDifferenceRequest);
			PrintSample(sample);
			std::cout << "exception=none\n";
		}
		catch (...)
		{
			std::cout << "exception=" << ExceptionCategory() << '\n';
		}
		PrintSerializedState("default", RandX::DefaultEngine());
		std::cout << "source_progress reads=0\n";
	}
	{
		Controlled64Engine engine(Parameters::kExplicitSeed);
		std::cout << "[case] cpp23-wide-difference-length-error seed=" << engine.seed()
			<< " request=" << Parameters::kOneCount << " length_over_uint64=true\n";
		try
		{
			const auto sample = RandX::RandSample(engine, first, last,
				static_cast<WideDifference>(Parameters::kOneCount));
			PrintSample(sample);
			std::cout << "exception=none\n";
		}
		catch (...)
		{
			std::cout << "exception=" << ExceptionCategory() << '\n';
		}
		PrintControlledState(engine);
		std::cout << "source_progress reads=0\n";
	}
}
#endif
#endif

inline void RunSamplingOnly()
{
	static_assert(!std::is_copy_constructible<NoncopyableEngine>::value,
		"the noncopyable engine case must stay noncopyable");
	std::cout << "[sampling-observations] version=" << Parameters::kObservationFormatVersion << '\n';
	std::cout << "thresholds hash=" << RandX::detail::HashSetThresholdK
		<< " bitmap=" << RandX::detail::SampleBitmapThresholdK
		<< " bitmap_word_bits=" << RandX::detail::SampleBitmapWordBits
		<< " bitmap_density_divisor=" << RandX::detail::SampleBitmapDensityDivisor << '\n';
	RunDefaultCases();
	RunExplicitCases();
#if !defined(RANDX_PARITY_CPP17) && __cplusplus > 202002L && \
	defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 201911L
	std::cout << "[sampling-cpp23-only]\n";
	RunModernSentinelCase();
	RunMoveOnlyIstreamCases();
#if defined(__SIZEOF_INT128__)
	RunWideDifferenceCases();
#endif
#endif
}
}

#endif
