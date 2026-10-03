#pragma once

#include <benchmark/benchmark.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

namespace randx_default_api_benchmarks
{
constexpr std::int64_t kBatchLength = 1024;
constexpr std::int64_t kWarmupCallCount = 1024;
constexpr std::uint64_t kDefaultEngineSeed = 0xD3FA017ULL;
constexpr std::uint32_t kPowerOfTwoRangeWidth = 256;
constexpr int kDiceMinimum = 1;
constexpr int kDiceMaximum = 6;
constexpr std::int64_t kRuntimeSignedMinimum = -1234567;
constexpr std::int64_t kRuntimeSignedMaximum = 2345678;
constexpr std::int64_t kRuntimeUnsignedMinimum = 1234567;
constexpr std::int64_t kRuntimeUnsignedMaximum = 2345678;
constexpr std::size_t kShuffleElementCount = 256;
constexpr std::size_t kSampleRangeSize = 256;
constexpr std::size_t kSampleCount = 16;
constexpr std::int64_t kColdStartObservationCount = 32;

enum class IntervalCase
{
    Dice,
    PowerOfTwo,
    Runtime,
    FullRange
};

template <typename T, IntervalCase Case>
static std::pair<T, T> GetBounds(const benchmark::State& state)
{
    if constexpr (Case == IntervalCase::Dice)
    {
        return {static_cast<T>(kDiceMinimum), static_cast<T>(kDiceMaximum)};
    }
    else if constexpr (Case == IntervalCase::PowerOfTwo)
    {
        return {T{0}, static_cast<T>(kPowerOfTwoRangeWidth - 1)};
    }
    else if constexpr (Case == IntervalCase::Runtime)
    {
        return {static_cast<T>(state.range(0)), static_cast<T>(state.range(1))};
    }
    else
    {
        return {(std::numeric_limits<T>::lowest)(), (std::numeric_limits<T>::max)()};
    }
}

static void PrepareDefaultEngine()
{
    RandX::Reseed(kDefaultEngineSeed);
}

template <typename Next>
static void WarmIntegerCalls(Next&& next)
{
    std::uint64_t accumulator = 0;
    for (std::int64_t call = 0; call < kWarmupCallCount; ++call)
        accumulator += static_cast<std::uint64_t>(next());
    benchmark::DoNotOptimize(accumulator);
}

template <typename Next>
static void WarmFloatingCalls(Next&& next)
{
    double accumulator = 0.0;
    for (std::int64_t call = 0; call < kWarmupCallCount; ++call)
        accumulator += static_cast<double>(next());
    benchmark::DoNotOptimize(accumulator);
}

template <typename Accumulator, typename Next>
static void MeasureBatches(benchmark::State& state, Next&& next)
{
    for (auto _ : state)
    {
        Accumulator accumulator{};
        for (std::int64_t call = 0; call < kBatchLength; ++call)
            accumulator += static_cast<Accumulator>(next());
        benchmark::DoNotOptimize(accumulator);
    }
    state.SetItemsProcessed(state.iterations() * kBatchLength);
}

template <typename T, IntervalCase Case>
static void BM_DefaultDirectInteger(benchmark::State& state)
{
    const auto bounds = GetBounds<T, Case>(state);
    const T minimum = bounds.first;
    const T maximum = bounds.second;
    PrepareDefaultEngine();
    WarmIntegerCalls([&]() { return RandX::RandInt<T>(minimum, maximum); });

    MeasureBatches<std::uint64_t>(state, [&]() {
        return RandX::RandInt<T>(minimum, maximum);
    });
}

template <typename T, IntervalCase Case, T (*Wrapper)(T, T)>
static void BM_DefaultWrappedInteger(benchmark::State& state)
{
    const auto bounds = GetBounds<T, Case>(state);
    const T minimum = bounds.first;
    const T maximum = bounds.second;
    PrepareDefaultEngine();
    WarmIntegerCalls([&]() { return Wrapper(minimum, maximum); });

    MeasureBatches<std::uint64_t>(state, [&]() {
        return Wrapper(minimum, maximum);
    });
}

template <typename T, IntervalCase Case>
static void BM_DefaultCachedEngineInteger(benchmark::State& state)
{
    const auto bounds = GetBounds<T, Case>(state);
    const T minimum = bounds.first;
    const T maximum = bounds.second;
    PrepareDefaultEngine();
    auto& engine = RandX::DefaultEngine();
    WarmIntegerCalls([&]() { return RandX::RandInt(engine, minimum, maximum); });

    MeasureBatches<std::uint64_t>(state, [&]() {
        return RandX::RandInt(engine, minimum, maximum);
    });
}

template <typename T, IntervalCase Case>
static void BM_DefaultExplicitEngineInteger(benchmark::State& state)
{
    const auto bounds = GetBounds<T, Case>(state);
    const T minimum = bounds.first;
    const T maximum = bounds.second;
    RandX::Xoshiro256StarStar engine{kDefaultEngineSeed};
    WarmIntegerCalls([&]() { return RandX::RandInt(engine, minimum, maximum); });

    MeasureBatches<std::uint64_t>(state, [&]() {
        return RandX::RandInt(engine, minimum, maximum);
    });
}

int DefaultIntWrapper(int minimum, int maximum)
{
    return RandX::RandInt(minimum, maximum);
}

std::uint32_t DefaultUint32Wrapper(std::uint32_t minimum, std::uint32_t maximum)
{
    return RandX::RandInt(minimum, maximum);
}

std::uint64_t DefaultUint64Wrapper(std::uint64_t minimum, std::uint64_t maximum)
{
    return RandX::RandInt(minimum, maximum);
}

int Dice()
{
    return RandX::RandInt(kDiceMinimum, kDiceMaximum);
}

static void BM_DefaultWrappedDice(benchmark::State& state)
{
    PrepareDefaultEngine();
    WarmIntegerCalls([]() { return Dice(); });

    MeasureBatches<std::uint64_t>(state, []() { return Dice(); });
}

static void BM_DefaultRandReal(benchmark::State& state)
{
    PrepareDefaultEngine();
    WarmFloatingCalls([]() { return RandX::RandReal(); });

    MeasureBatches<double>(state, []() { return RandX::RandReal(); });
}

static void BM_DefaultRandNormal(benchmark::State& state)
{
    PrepareDefaultEngine();
    WarmFloatingCalls([]() { return RandX::RandNormal(); });

    MeasureBatches<double>(state, []() { return RandX::RandNormal(); });
}

static void BM_DefaultRandShuffle(benchmark::State& state)
{
    std::vector<int> values(kShuffleElementCount);
    std::iota(values.begin(), values.end(), 0);
    PrepareDefaultEngine();
    std::uint64_t warmupAccumulator = 0;
    for (std::int64_t call = 0; call < kWarmupCallCount; ++call)
    {
        RandX::RandShuffle(values);
        warmupAccumulator += static_cast<std::uint64_t>(values.front());
    }
    benchmark::DoNotOptimize(warmupAccumulator);
    std::iota(values.begin(), values.end(), 0);

    for (auto _ : state)
    {
        std::uint64_t accumulator = 0;
        for (std::int64_t call = 0; call < kBatchLength; ++call)
        {
            RandX::RandShuffle(values);
            accumulator += static_cast<std::uint64_t>(values.front());
        }
        benchmark::DoNotOptimize(accumulator);
    }
    state.SetItemsProcessed(state.iterations() * kBatchLength);
}

static void BM_DefaultRandSample(benchmark::State& state)
{
    std::vector<int> values(kSampleRangeSize);
    std::iota(values.begin(), values.end(), 0);
    PrepareDefaultEngine();
    std::uint64_t warmupAccumulator = 0;
    for (std::int64_t call = 0; call < kWarmupCallCount; ++call)
    {
        const auto sample = RandX::RandSample(values, kSampleCount);
        warmupAccumulator += static_cast<std::uint64_t>(sample.front());
    }
    benchmark::DoNotOptimize(warmupAccumulator);

    for (auto _ : state)
    {
        std::uint64_t accumulator = 0;
        for (std::int64_t call = 0; call < kBatchLength; ++call)
        {
            const auto sample = RandX::RandSample(values, kSampleCount);
            accumulator += static_cast<std::uint64_t>(sample.front());
        }
        benchmark::DoNotOptimize(accumulator);
    }
    state.SetItemsProcessed(state.iterations() * kBatchLength);
}

static void BM_DefaultColdStart(benchmark::State& state)
{
    using Clock = std::chrono::steady_clock;
    double totalThreadStartupNanoseconds = 0.0;
    double totalThreadGateNanoseconds = 0.0;

    for (auto _ : state)
    {
        std::mutex mutex;
        std::condition_variable condition;
        bool ready = false;
        bool startAccess = false;
        bool complete = false;
        Clock::time_point startSignal;
        std::chrono::nanoseconds accessDuration{};
        std::chrono::nanoseconds gateDuration{};
        const auto threadCreationStart = Clock::now();

        std::thread worker([&]() {
            std::unique_lock<std::mutex> lock(mutex);
            ready = true;
            condition.notify_one();
            condition.wait(lock, [&]() { return startAccess; });
            lock.unlock();
            const auto accessStart = Clock::now();
            gateDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(accessStart - startSignal);

            benchmark::DoNotOptimize(RandX::DefaultEngine());
            accessDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - accessStart);

            lock.lock();
            complete = true;
            condition.notify_one();
        });

        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&]() { return ready; });
            totalThreadStartupNanoseconds += static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - threadCreationStart).count());
            startSignal = Clock::now();
            startAccess = true;
        }
        condition.notify_one();

        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&]() { return complete; });
        }
        worker.join();

        totalThreadGateNanoseconds += static_cast<double>(gateDuration.count());
        state.SetIterationTime(std::chrono::duration<double>(accessDuration).count());
    }

    if (state.iterations() > 0)
    {
        state.counters["thread_startup_ns"] = totalThreadStartupNanoseconds / static_cast<double>(state.iterations());
        state.counters["thread_gate_ns"] = totalThreadGateNanoseconds / static_cast<double>(state.iterations());
    }
}

template <typename T, IntervalCase Case>
static void RegisterIntegerBenchmark(const char* name, void (*function)(benchmark::State&))
{
    auto* registration = benchmark::RegisterBenchmark(name, function);
    if constexpr (Case == IntervalCase::Runtime)
    {
        if constexpr (std::numeric_limits<T>::is_signed)
            registration->Args({kRuntimeSignedMinimum, kRuntimeSignedMaximum});
        else
            registration->Args({kRuntimeUnsignedMinimum, kRuntimeUnsignedMaximum});
        registration->ArgNames({"minimum", "maximum"});
    }
}

template <typename T, IntervalCase Case>
static void RegisterDirectBenchmark(const char* name)
{
    RegisterIntegerBenchmark<T, Case>(name, &BM_DefaultDirectInteger<T, Case>);
}

template <typename T, IntervalCase Case, T (*Wrapper)(T, T)>
static void RegisterWrapperBenchmark(const char* name)
{
    RegisterIntegerBenchmark<T, Case>(name, &BM_DefaultWrappedInteger<T, Case, Wrapper>);
}

template <typename T, IntervalCase Case>
static void RegisterCachedEngineBenchmark(const char* name)
{
    RegisterIntegerBenchmark<T, Case>(name, &BM_DefaultCachedEngineInteger<T, Case>);
}

template <typename T, IntervalCase Case>
static void RegisterExplicitEngineBenchmark(const char* name)
{
    RegisterIntegerBenchmark<T, Case>(name, &BM_DefaultExplicitEngineInteger<T, Case>);
}

static void RegisterIntegerBenchmarks()
{
    RegisterDirectBenchmark<int, IntervalCase::Dice>("BM_DefaultDirectIntDice");
    RegisterDirectBenchmark<int, IntervalCase::PowerOfTwo>("BM_DefaultDirectIntPowerOfTwo");
    RegisterDirectBenchmark<int, IntervalCase::Runtime>("BM_DefaultDirectIntRuntime");
    RegisterDirectBenchmark<int, IntervalCase::FullRange>("BM_DefaultDirectIntFullRange");
    RegisterDirectBenchmark<std::uint32_t, IntervalCase::PowerOfTwo>("BM_DefaultDirectUint32PowerOfTwo");
    RegisterDirectBenchmark<std::uint32_t, IntervalCase::Runtime>("BM_DefaultDirectUint32Runtime");
    RegisterDirectBenchmark<std::uint32_t, IntervalCase::FullRange>("BM_DefaultDirectUint32FullRange");
    RegisterDirectBenchmark<std::uint64_t, IntervalCase::PowerOfTwo>("BM_DefaultDirectUint64PowerOfTwo");
    RegisterDirectBenchmark<std::uint64_t, IntervalCase::Runtime>("BM_DefaultDirectUint64Runtime");
    RegisterDirectBenchmark<std::uint64_t, IntervalCase::FullRange>("BM_DefaultDirectUint64FullRange");

    RegisterWrapperBenchmark<int, IntervalCase::PowerOfTwo, DefaultIntWrapper>("BM_DefaultWrapperIntPowerOfTwo");
    RegisterWrapperBenchmark<int, IntervalCase::Runtime, DefaultIntWrapper>("BM_DefaultWrapperIntRuntime");
    RegisterWrapperBenchmark<int, IntervalCase::FullRange, DefaultIntWrapper>("BM_DefaultWrapperIntFullRange");
    RegisterWrapperBenchmark<std::uint32_t, IntervalCase::PowerOfTwo, DefaultUint32Wrapper>("BM_DefaultWrapperUint32PowerOfTwo");
    RegisterWrapperBenchmark<std::uint32_t, IntervalCase::Runtime, DefaultUint32Wrapper>("BM_DefaultWrapperUint32Runtime");
    RegisterWrapperBenchmark<std::uint32_t, IntervalCase::FullRange, DefaultUint32Wrapper>("BM_DefaultWrapperUint32FullRange");
    RegisterWrapperBenchmark<std::uint64_t, IntervalCase::PowerOfTwo, DefaultUint64Wrapper>("BM_DefaultWrapperUint64PowerOfTwo");
    RegisterWrapperBenchmark<std::uint64_t, IntervalCase::Runtime, DefaultUint64Wrapper>("BM_DefaultWrapperUint64Runtime");
    RegisterWrapperBenchmark<std::uint64_t, IntervalCase::FullRange, DefaultUint64Wrapper>("BM_DefaultWrapperUint64FullRange");

    RegisterCachedEngineBenchmark<int, IntervalCase::Dice>("BM_DefaultCachedEngineIntDice");
    RegisterCachedEngineBenchmark<int, IntervalCase::PowerOfTwo>("BM_DefaultCachedEngineIntPowerOfTwo");
    RegisterCachedEngineBenchmark<int, IntervalCase::Runtime>("BM_DefaultCachedEngineIntRuntime");
    RegisterCachedEngineBenchmark<int, IntervalCase::FullRange>("BM_DefaultCachedEngineIntFullRange");
    RegisterCachedEngineBenchmark<std::uint32_t, IntervalCase::PowerOfTwo>("BM_DefaultCachedEngineUint32PowerOfTwo");
    RegisterCachedEngineBenchmark<std::uint32_t, IntervalCase::Runtime>("BM_DefaultCachedEngineUint32Runtime");
    RegisterCachedEngineBenchmark<std::uint32_t, IntervalCase::FullRange>("BM_DefaultCachedEngineUint32FullRange");
    RegisterCachedEngineBenchmark<std::uint64_t, IntervalCase::PowerOfTwo>("BM_DefaultCachedEngineUint64PowerOfTwo");
    RegisterCachedEngineBenchmark<std::uint64_t, IntervalCase::Runtime>("BM_DefaultCachedEngineUint64Runtime");
    RegisterCachedEngineBenchmark<std::uint64_t, IntervalCase::FullRange>("BM_DefaultCachedEngineUint64FullRange");

    RegisterExplicitEngineBenchmark<int, IntervalCase::Dice>("BM_DefaultExplicitEngineIntDice");
    RegisterExplicitEngineBenchmark<int, IntervalCase::PowerOfTwo>("BM_DefaultExplicitEngineIntPowerOfTwo");
    RegisterExplicitEngineBenchmark<int, IntervalCase::Runtime>("BM_DefaultExplicitEngineIntRuntime");
    RegisterExplicitEngineBenchmark<int, IntervalCase::FullRange>("BM_DefaultExplicitEngineIntFullRange");
    RegisterExplicitEngineBenchmark<std::uint32_t, IntervalCase::PowerOfTwo>("BM_DefaultExplicitEngineUint32PowerOfTwo");
    RegisterExplicitEngineBenchmark<std::uint32_t, IntervalCase::Runtime>("BM_DefaultExplicitEngineUint32Runtime");
    RegisterExplicitEngineBenchmark<std::uint32_t, IntervalCase::FullRange>("BM_DefaultExplicitEngineUint32FullRange");
    RegisterExplicitEngineBenchmark<std::uint64_t, IntervalCase::PowerOfTwo>("BM_DefaultExplicitEngineUint64PowerOfTwo");
    RegisterExplicitEngineBenchmark<std::uint64_t, IntervalCase::Runtime>("BM_DefaultExplicitEngineUint64Runtime");
    RegisterExplicitEngineBenchmark<std::uint64_t, IntervalCase::FullRange>("BM_DefaultExplicitEngineUint64FullRange");

    benchmark::RegisterBenchmark("BM_DefaultWrapperDice", BM_DefaultWrappedDice);
    benchmark::RegisterBenchmark("BM_DefaultRandReal", BM_DefaultRandReal);
    benchmark::RegisterBenchmark("BM_DefaultRandNormal", BM_DefaultRandNormal);
    benchmark::RegisterBenchmark("BM_DefaultRandShuffle", BM_DefaultRandShuffle);
    benchmark::RegisterBenchmark("BM_DefaultRandSample", BM_DefaultRandSample);
}

static const bool kDefaultApiBenchmarksRegistered = []() {
#if defined(RANDX_BENCHMARK_CPP17)
    benchmark::AddCustomContext("cpp_standard", "17");
#else
    benchmark::AddCustomContext("cpp_standard", "23");
#endif
#if defined(RANDX_BENCHMARK_INITIALIZATION_OBSERVATION)
    benchmark::RegisterBenchmark("BM_DefaultColdStart", BM_DefaultColdStart)
        ->UseManualTime()
        ->Iterations(kColdStartObservationCount);
#else
    RegisterIntegerBenchmarks();
#endif
    return true;
}();
}
