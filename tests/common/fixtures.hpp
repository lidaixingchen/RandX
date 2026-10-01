#ifndef RANDX_TESTS_COMMON_FIXTURES_HPP
#define RANDX_TESTS_COMMON_FIXTURES_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <locale>
#include <random>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <type_traits>
#include <vector>
#include "doctest.h"

namespace RandXTest
{
namespace TestConstants
{
constexpr std::uint64_t kDefaultEngineTestSeed = 12345;
constexpr std::uint64_t kEngineContractSeed = 12345;
constexpr double kChiSquareCriticalDof99 = 148.2;
constexpr double kChiSquareCriticalDof127 = 173.6;
constexpr int kStatisticalSampleCount = 1'000'000;
constexpr int kStatisticalBins100 = 100;
constexpr int kStatisticalBins128 = 128;
constexpr int kMonteCarloTrials1000 = 1'000;
constexpr int kMonteCarloTrials10000 = 10'000;
constexpr int kConvenienceTrials100 = 100;
constexpr int kRandCanonicalSampleCount = 10'000;
constexpr double kRandCanonicalMeanLowerBound = 0.45;
constexpr double kRandCanonicalMeanUpperBound = 0.55;
constexpr double kRandBetaMeanLowerBound = 0.4;
constexpr double kRandBetaMeanUpperBound = 0.6;
constexpr double kRandBetaMeanTolerance = 0.05;
constexpr double kRandBetaMinimumVariance = 0.2;
constexpr double kRandBetaVarianceTolerance = 0.05;
constexpr double kRandBinomialMeanLowerBound = 25.0;
constexpr double kRandBinomialMeanUpperBound = 35.0;
constexpr double kRandNormalMeanAbsoluteTolerance = 0.1;
// 样本数低于自动重新播种阈值，保持 ChaCha20 确定性测试的固定序列。
constexpr int kChaCha20ChiSquareSampleCount = 100'000;
}

#if defined(__SIZEOF_INT128__)
namespace WideSampleFixtures
{
using WideDiff = __int128;

struct WideIterator
{
    using iterator_category = std::random_access_iterator_tag;
    using value_type = int;
    using difference_type = WideDiff;
    using pointer = void;
    using reference = int;

    difference_type index{0};

    reference operator*() const { return 0; }
    reference operator[](difference_type) const { return 0; }

    WideIterator& operator++() { ++index; return *this; }
    WideIterator operator++(int) { WideIterator copy = *this; ++*this; return copy; }
    WideIterator& operator--() { --index; return *this; }
    WideIterator operator--(int) { WideIterator copy = *this; --*this; return copy; }
    WideIterator& operator+=(difference_type offset) { index += offset; return *this; }
    WideIterator& operator-=(difference_type offset) { index -= offset; return *this; }

    friend WideIterator operator+(WideIterator iterator, difference_type offset)
    {
        iterator += offset;
        return iterator;
    }

    friend WideIterator operator+(difference_type offset, WideIterator iterator)
    {
        iterator += offset;
        return iterator;
    }

    friend WideIterator operator-(WideIterator iterator, difference_type offset)
    {
        iterator -= offset;
        return iterator;
    }

    friend difference_type operator-(WideIterator first, WideIterator last)
    {
        return first.index - last.index;
    }

    friend bool operator==(WideIterator first, WideIterator last)
    {
        return first.index == last.index;
    }

    friend bool operator!=(WideIterator first, WideIterator last)
    {
        return !(first == last);
    }

    friend bool operator<(WideIterator first, WideIterator last)
    {
        return first.index < last.index;
    }

    friend bool operator>(WideIterator first, WideIterator last)
    {
        return last < first;
    }

    friend bool operator<=(WideIterator first, WideIterator last)
    {
        return !(last < first);
    }

    friend bool operator>=(WideIterator first, WideIterator last)
    {
        return !(first < last);
    }
};

struct WideRange
{
    WideDiff length;

    WideIterator begin() const { return WideIterator{0}; }
    WideIterator end() const { return WideIterator{length}; }
    WideDiff size() const { return length; }
};
}
#endif

namespace EngineFixtures
{
struct CustomValidSeedSeq
{
    void generate(std::uint32_t* begin, std::uint32_t* end)
    {
        std::fill(begin, end, 0x12345678u);
    }
};

struct TypeLackingGenerate {};

template <class Engine>
void VerifyEngineSeedSeqContracts()
{
    int signedSeed = static_cast<int>(TestConstants::kEngineContractSeed);
    Engine signedSeedEngine(signedSeed);
    std::uint64_t unsignedSeed = TestConstants::kEngineContractSeed;
    Engine unsignedSeedEngine(unsignedSeed);
    const std::uint64_t constantSeed = TestConstants::kEngineContractSeed;
    Engine constantSeedEngine(constantSeed);

    const auto signedSeedValue = signedSeedEngine();
    const auto unsignedSeedValue = unsignedSeedEngine();
    const auto constantSeedValue = constantSeedEngine();
    CHECK(signedSeedValue == unsignedSeedValue);
    CHECK(unsignedSeedValue == constantSeedValue);

    Engine copiedEngine = unsignedSeedEngine;
    const auto copiedValue = copiedEngine();
    const auto nextValue = unsignedSeedEngine();
    CHECK(copiedValue == nextValue);

    typename Engine::state_type state = unsignedSeedEngine.serialize();
    Engine restoredEngine(state);
    CHECK(restoredEngine() == unsignedSeedEngine());

    std::seed_seq standardSeedSequence{1, 2, 3, 4, 5};
    Engine standardSequenceEngine(standardSeedSequence);
    CHECK(standardSequenceEngine() != typename Engine::result_type{0});

    static_assert(std::is_constructible_v<Engine, CustomValidSeedSeq&>);
    static_assert(!std::is_constructible_v<Engine, TypeLackingGenerate&>);

    CustomValidSeedSeq customSeedSequence;
    Engine customSequenceEngine(customSeedSequence);
    CHECK(customSequenceEngine() != typename Engine::result_type{0});
}
}

namespace SamplingContractFixtures
{
struct OperationTrace
{
    std::size_t dereferences{0};
    std::size_t increments{0};
    std::size_t copyAttempts{0};
    std::size_t assignmentAttempts{0};
    std::size_t throwOnCopyAttempt{(std::numeric_limits<std::size_t>::max)()};
    std::size_t throwOnAssignmentAttempt{(std::numeric_limits<std::size_t>::max)()};
    std::size_t* engineCallCount{nullptr};
    std::size_t engineCallsAtThrow{0};
};

struct NonDefaultReadOnlyCopyItem
{
    const std::size_t position;
    const int value;
    std::size_t* copyCount;

    NonDefaultReadOnlyCopyItem() = delete;
    NonDefaultReadOnlyCopyItem(std::size_t sourcePosition, int sourceValue, std::size_t& copies)
        : position(sourcePosition), value(sourceValue), copyCount(&copies) {}
    NonDefaultReadOnlyCopyItem(const NonDefaultReadOnlyCopyItem& other)
        : position(other.position), value(other.value), copyCount(other.copyCount)
    {
        ++*copyCount;
    }
    NonDefaultReadOnlyCopyItem& operator=(const NonDefaultReadOnlyCopyItem&) = delete;
};

struct ThrowingCopyItem
{
    int value;
    OperationTrace* trace;

    ThrowingCopyItem(int sourceValue, OperationTrace& operations)
        : value(sourceValue), trace(&operations) {}
    ThrowingCopyItem(const ThrowingCopyItem& other)
        : value(other.value), trace(other.trace)
    {
        if (trace == nullptr) return;
        const std::size_t attempt = ++trace->copyAttempts;
        if (attempt == trace->throwOnCopyAttempt)
        {
            if (trace->engineCallCount != nullptr)
                trace->engineCallsAtThrow = *trace->engineCallCount;
            throw std::runtime_error("sample copy construction failure");
        }
    }
    ThrowingCopyItem& operator=(const ThrowingCopyItem& other)
    {
        OperationTrace* operations = trace != nullptr ? trace : other.trace;
        if (operations != nullptr)
        {
            const std::size_t attempt = ++operations->assignmentAttempts;
            if (attempt == operations->throwOnAssignmentAttempt)
            {
                if (operations->engineCallCount != nullptr)
                    operations->engineCallsAtThrow = *operations->engineCallCount;
                throw std::runtime_error("sample copy assignment failure");
            }
        }
        value = other.value;
        trace = other.trace;
        return *this;
    }
};

struct CountingEngine
{
    using result_type = std::uint64_t;
    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }

    std::size_t callCount{0};
    result_type value{(std::numeric_limits<result_type>::max)() / 2 + 1};

    result_type operator()() noexcept
    {
        ++callCount;
        return value;
    }
};

struct ThrowingEngine
{
    using result_type = std::uint64_t;
    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }

    std::size_t callCount{0};

    result_type operator()()
    {
        ++callCount;
        throw std::runtime_error("sample engine failure");
    }
};

template <class T>
struct RandomAccessIterator
{
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    pointer current{nullptr};
    OperationTrace* trace{nullptr};

    reference operator*() const
    {
        if (trace != nullptr) ++trace->dereferences;
        return *current;
    }
    reference operator[](difference_type offset) const
    {
        if (trace != nullptr) ++trace->dereferences;
        return current[offset];
    }
    RandomAccessIterator& operator++()
    {
        if (trace != nullptr) ++trace->increments;
        ++current;
        return *this;
    }
    RandomAccessIterator operator++(int)
    {
        RandomAccessIterator previous = *this;
        ++*this;
        return previous;
    }
    RandomAccessIterator& operator--()
    {
        if (trace != nullptr) ++trace->increments;
        --current;
        return *this;
    }
    RandomAccessIterator operator--(int)
    {
        RandomAccessIterator previous = *this;
        --*this;
        return previous;
    }
    RandomAccessIterator& operator+=(difference_type offset)
    {
        current += offset;
        return *this;
    }
    RandomAccessIterator& operator-=(difference_type offset)
    {
        current -= offset;
        return *this;
    }

    friend RandomAccessIterator operator+(RandomAccessIterator iterator, difference_type offset)
    {
        iterator += offset;
        return iterator;
    }
    friend RandomAccessIterator operator+(difference_type offset, RandomAccessIterator iterator)
    {
        iterator += offset;
        return iterator;
    }
    friend RandomAccessIterator operator-(RandomAccessIterator iterator, difference_type offset)
    {
        iterator -= offset;
        return iterator;
    }
    friend difference_type operator-(RandomAccessIterator first, RandomAccessIterator last)
    {
        return first.current - last.current;
    }
    friend bool operator==(RandomAccessIterator first, RandomAccessIterator last)
    {
        return first.current == last.current;
    }
    friend bool operator!=(RandomAccessIterator first, RandomAccessIterator last)
    {
        return !(first == last);
    }
    friend bool operator<(RandomAccessIterator first, RandomAccessIterator last)
    {
        return first.current < last.current;
    }
    friend bool operator>(RandomAccessIterator first, RandomAccessIterator last)
    {
        return last < first;
    }
    friend bool operator<=(RandomAccessIterator first, RandomAccessIterator last)
    {
        return !(last < first);
    }
    friend bool operator>=(RandomAccessIterator first, RandomAccessIterator last)
    {
        return !(first < last);
    }
};

template <class T>
struct RandomAccessContainer
{
    T* values{nullptr};
    std::size_t count{0};
    OperationTrace* trace{nullptr};

    RandomAccessIterator<T> begin() const { return RandomAccessIterator<T>{values, trace}; }
    RandomAccessIterator<T> end() const { return RandomAccessIterator<T>{values + count, trace}; }
    std::size_t size() const { return count; }
};

template <class T>
struct InputIterator
{
    using iterator_category = std::input_iterator_tag;
    using iterator_concept = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;

    pointer current{nullptr};
    OperationTrace* trace{nullptr};

    reference operator*() const
    {
        if (trace != nullptr) ++trace->dereferences;
        return *current;
    }
    InputIterator& operator++()
    {
        if (trace != nullptr) ++trace->increments;
        ++current;
        return *this;
    }
    InputIterator operator++(int)
    {
        InputIterator previous = *this;
        ++*this;
        return previous;
    }
    friend bool operator==(InputIterator first, InputIterator last)
    {
        return first.current == last.current;
    }
    friend bool operator!=(InputIterator first, InputIterator last)
    {
        return !(first == last);
    }
};
}

namespace ExtendedFixtures
{
struct Synthetic32BitWideType
{
    using result_type = std::uint64_t;
    static constexpr std::uint64_t min() { return 0; }
    static constexpr std::uint64_t max() { return 0xFFFFFFFFULL; }
    std::uint64_t val = 0x12345678ULL;
    std::uint64_t operator()()
    {
        val = (val * 1664525ULL + 1013904223ULL) & 0xFFFFFFFFULL;
        return val;
    }
};

struct SyntheticNonZeroMinEngine
{
    using result_type = std::uint32_t;
    static constexpr std::uint32_t min() { return 10; }
    static constexpr std::uint32_t max() { return 20; }
    std::uint32_t val = 10;
    std::uint32_t operator()()
    {
        val = 10 + (val - 10 + 1) % 11;
        return val;
    }
};

struct ThrowingEngine
{
    using result_type = std::uint64_t;
    static constexpr std::uint64_t min() { return 0; }
    static constexpr std::uint64_t max() { return UINT64_MAX; }
    std::uint64_t operator()()
    {
        throw std::runtime_error("simulated engine failure");
    }
};

struct Scripted64BitEngine
{
    using result_type = std::uint64_t;
    static constexpr std::uint64_t min() { return 0; }
    static constexpr std::uint64_t max() { return UINT64_MAX; }
    std::vector<std::uint64_t> seq;
    std::size_t idx = 0;
    std::uint64_t operator()()
    {
        if (idx < seq.size()) return seq[idx++];
        return 0;
    }
};

struct Scripted32BitEngine
{
    using result_type = std::uint32_t;
    static constexpr std::uint32_t min() { return 0; }
    static constexpr std::uint32_t max() { return 0xFFFFFFFFU; }
    std::vector<std::uint32_t> seq;
    std::size_t idx = 0;
    std::uint32_t operator()()
    {
        if (idx < seq.size()) return seq[idx++];
        return 0;
    }
};

template <class ResultType, ResultType MinVal, ResultType MaxVal>
struct CallCountingEngine
{
    using result_type = ResultType;
    static constexpr result_type min() { return MinVal; }
    static constexpr result_type max() { return MaxVal; }
    std::size_t call_count = 0;
    ResultType value = static_cast<ResultType>(0x12345678);
    result_type operator()()
    {
        ++call_count;
        return value++;
    }
};
}

namespace OverloadFixtures
{
struct ThrowingEngine
{
    using result_type = std::uint32_t;
    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return 1000; }
    result_type operator()()
    {
        throw std::runtime_error("ThrowingEngine invoked");
    }
};

struct MoveOnlyEngine
{
    using result_type = std::uint64_t;
    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return UINT64_MAX; }
    MoveOnlyEngine() = default;
    MoveOnlyEngine(const MoveOnlyEngine&) = delete;
    MoveOnlyEngine& operator=(const MoveOnlyEngine&) = delete;
    MoveOnlyEngine(MoveOnlyEngine&&) = default;
    MoveOnlyEngine& operator=(MoveOnlyEngine&&) = default;
    result_type operator()() noexcept { return 42; }
};

struct NotAnEngine { int val{0}; };

struct IncompleteEngineNoMinMax
{
    using result_type = std::uint32_t;
    result_type operator()() { return 0; }
};

struct IncompleteEngineSignedResult
{
    using result_type = int;
    static constexpr int min() { return 0; }
    static constexpr int max() { return 10; }
    int operator()() { return 0; }
};

struct IncompleteEngineBoolResult
{
    using result_type = bool;
    static constexpr bool min() { return false; }
    static constexpr bool max() { return true; }
    bool operator()() { return false; }
};
}

namespace WeightedFixtures
{
struct CountingEngine
{
    using result_type = std::uint64_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return UINT64_MAX; }
    std::size_t call_count{0};
    result_type operator()() noexcept
    {
        ++call_count;
        return 42ULL;
    }
};
}

namespace RealIntervalFixtures
{
struct IntervalCountingEngine
{
    using result_type = std::uint64_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return UINT64_MAX; }
    std::size_t call_count{0};
    result_type operator()() noexcept
    {
        ++call_count;
        return 1234567890ULL;
    }
};

template <typename T, typename Engine>
void RunAdjacentIntervalTests(Engine& engine)
{
    const T positiveMin = T{1};
    const T positiveMax = std::nextafter(positiveMin, T{2});
    for (int index = 0; index < 100; ++index)
        CHECK(RandX::RandReal(engine, positiveMin, positiveMax) == positiveMin);

    const T negativeMin = T{-2};
    const T negativeMax = std::nextafter(negativeMin, T{-1});
    for (int index = 0; index < 100; ++index)
        CHECK(RandX::RandReal(engine, negativeMin, negativeMax) == negativeMin);

    const T zeroMin = T{0};
    const T zeroMax = std::nextafter(zeroMin, T{1});
    for (int index = 0; index < 100; ++index)
        CHECK(RandX::RandReal(engine, zeroMin, zeroMax) == zeroMin);

    const T subnormalMin = std::numeric_limits<T>::denorm_min();
    if (subnormalMin > T{0})
    {
        const T subnormalMax = std::nextafter(subnormalMin, T{1});
        for (int index = 0; index < 100; ++index)
            CHECK(RandX::RandReal(engine, subnormalMin, subnormalMax) == subnormalMin);
    }

    std::vector<T> buffer(50);
    RandX::RandFill(engine, buffer.begin(), buffer.end(), positiveMin, positiveMax);
    for (const auto value : buffer)
        CHECK(value == positiveMin);

    const auto values = RandX::RandVector(engine, positiveMin, positiveMax, 50);
    for (const auto value : values)
        CHECK(value == positiveMin);
}
}

namespace StreamFormatFixtures
{
struct FailingBuffer : public std::streambuf
{
protected:
    int_type overflow(int_type) override
    {
        return traits_type::eof();
    }
};

#if defined(__GLIBCXX__)
struct ThrowingWidenFacet : public std::ctype<wchar_t>
{
protected:
    wchar_t do_widen(char) const override
    {
        throw std::runtime_error("ctype widen failure");
    }
};

struct ThrowingFillStream : public std::basic_ios<wchar_t>
{
    explicit ThrowingFillStream(const std::locale& locale)
    {
        this->init(nullptr);
        this->imbue(locale);
        this->_M_fill_init = false;
    }
};
#endif
}
}

#endif
