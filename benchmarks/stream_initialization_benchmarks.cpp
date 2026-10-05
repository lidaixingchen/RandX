#include <benchmark/benchmark.h>

#if defined(RANDX_BENCHMARK_CPP17)
#include <RandX_Cpp17.hpp>
#else
#include <RandX.hpp>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
constexpr std::uint64_t kBenchmarkSeed = 0x4D595DF4D0F33173ULL;
constexpr std::uint64_t kStreamHalfBits = std::numeric_limits<std::uint32_t>::digits;
constexpr std::uint64_t kStreamLowMask = (std::numeric_limits<std::uint32_t>::max)();
constexpr std::uint64_t kHalfBoundaryStreamId = std::uint64_t{1} << kStreamHalfBits;
constexpr std::uint64_t kHighestStreamIdBit = std::uint64_t{1} << (std::numeric_limits<std::uint64_t>::digits - 1);
constexpr std::uint64_t kMaximumStreamId = (std::numeric_limits<std::uint64_t>::max)();
constexpr std::uint64_t kSequentialStreamCount = 64;
constexpr std::array<std::uint64_t, 7> kMeasuredStreamIds{
    0,
    1,
    3,
    kStreamLowMask,
    kHalfBoundaryStreamId,
    kHighestStreamIdBit,
    kMaximumStreamId,
};

template <class Engine>
Engine MakeLegacyStream(std::uint64_t streamId, std::uint64_t seed)
{
    Engine engine{seed};
    const std::uint64_t longJumps = streamId >> kStreamHalfBits;
    const std::uint64_t shortJumps = streamId & kStreamLowMask;
    for (std::uint64_t index = 0; index < longJumps; ++index)
        engine.longJump();
    for (std::uint64_t index = 0; index < shortJumps; ++index)
        engine.jump();
    return engine;
}

template <class Engine>
void ObserveFirstStream(const char* engineName, std::uint64_t streamId, std::uint64_t seed, bool legacy)
{
    const auto start = std::chrono::steady_clock::now();
    Engine engine = legacy ? MakeLegacyStream<Engine>(streamId, seed)
                           : RandX::MakeStreamEngine<Engine>(streamId, seed);
    benchmark::DoNotOptimize(engine);
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "{\"engine\":\"" << engineName << "\",\"legacy\":" << (legacy ? "true" : "false")
              << ",\"stream_id\":\"" << streamId << "\",\"elapsed_ns\":" << elapsed
              << ",\"first_output\":\"" << engine() << "\"}\n";
}

std::uint64_t CountSetBits(std::uint64_t value) noexcept
{
    std::uint64_t count = 0;
    while (value != 0)
    {
        value &= value - 1;
        ++count;
    }
    return count;
}

std::uint64_t CountSignificantBits(std::uint64_t value) noexcept
{
    std::uint64_t count = 0;
    while (value != 0)
    {
        value >>= 1;
        ++count;
    }
    return count;
}

template <class Engine>
constexpr std::uint64_t StreamPowerMaterialBytes() noexcept
{
    using table_type = RandX::detail::StreamJumpPowerTable<Engine>;
    return sizeof(table_type::jump_powers) + sizeof(table_type::long_jump_powers);
}

template <class Engine>
static void BM_StreamLocate(benchmark::State& state)
{
    std::uint64_t streamId = kMeasuredStreamIds[static_cast<std::size_t>(state.range(0))];
    std::uint64_t seed = kBenchmarkSeed;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(streamId);
        benchmark::DoNotOptimize(seed);
        Engine engine = RandX::MakeStreamEngine<Engine>(streamId, seed);
        benchmark::DoNotOptimize(engine);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("stream_id=" + std::to_string(streamId));
    state.counters["stream_id_bits"] = static_cast<double>(CountSignificantBits(streamId));
    state.counters["stream_id_set_bits"] = static_cast<double>(CountSetBits(streamId));
    state.counters["table_bytes"] = static_cast<double>(StreamPowerMaterialBytes<Engine>());
    state.counters["seed_constructions"] = static_cast<double>(state.iterations());
}

template <class Engine>
static void BM_StreamLocateLegacySmallId(benchmark::State& state)
{
    std::uint64_t streamId = kMeasuredStreamIds[static_cast<std::size_t>(state.range(0))];
    std::uint64_t seed = kBenchmarkSeed;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(streamId);
        benchmark::DoNotOptimize(seed);
        Engine engine = MakeLegacyStream<Engine>(streamId, seed);
        benchmark::DoNotOptimize(engine);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("stream_id=" + std::to_string(streamId));
    state.counters["stream_id_bits"] = static_cast<double>(CountSignificantBits(streamId));
    state.counters["stream_id_set_bits"] = static_cast<double>(CountSetBits(streamId));
    state.counters["legacy_jump_calls"] = static_cast<double>(
        (streamId >> kStreamHalfBits) + (streamId & kStreamLowMask));
    state.counters["seed_constructions"] = static_cast<double>(state.iterations());
}

template <class Engine>
static void BM_SequentialStreamsByJump(benchmark::State& state)
{
    std::uint64_t seed = kBenchmarkSeed;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(seed);
        Engine current{seed};
        for (std::uint64_t index = 0; index < kSequentialStreamCount; ++index)
        {
            Engine stream = current;
            typename Engine::result_type first = stream();
            benchmark::DoNotOptimize(first);
            if (index + 1 < kSequentialStreamCount)
                current.jump();
        }
    }
    state.SetItemsProcessed(state.iterations() * kSequentialStreamCount);
    state.counters["streams_per_seed"] = static_cast<double>(kSequentialStreamCount);
    state.counters["table_bytes"] = static_cast<double>(StreamPowerMaterialBytes<Engine>());
}

template <class Engine>
static void BM_StreamSeedConstruction(benchmark::State& state)
{
    std::uint64_t seed = kBenchmarkSeed;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(seed);
        Engine engine = RandX::MakeStreamEngine<Engine>(0, seed);
        benchmark::DoNotOptimize(engine);
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["stream_id_bits"] = 0;
    state.counters["stream_id_set_bits"] = 0;
    state.counters["table_bytes"] = static_cast<double>(StreamPowerMaterialBytes<Engine>());
    state.counters["runtime_table_initialization"] = 0;
}

template <class Engine>
void RegisterEngineBenchmarks(const char* engineName)
{
    const std::string prefix = std::string("BM_StreamInitialization/") + engineName;
    benchmark::internal::Benchmark* locate = benchmark::RegisterBenchmark(
        (prefix + "/locate").c_str(),
        &BM_StreamLocate<Engine>);
    benchmark::internal::Benchmark* legacy = benchmark::RegisterBenchmark(
        (prefix + "/legacy_small_id").c_str(),
        &BM_StreamLocateLegacySmallId<Engine>);
    for (std::size_t index = 0; index < kMeasuredStreamIds.size(); ++index)
        locate->Arg(static_cast<std::int64_t>(index));
    legacy->Arg(0)->Arg(1)->Arg(2)->Arg(4);
    benchmark::RegisterBenchmark(
        (prefix + "/sequential_by_jump").c_str(),
        &BM_SequentialStreamsByJump<Engine>);
    benchmark::RegisterBenchmark(
        (prefix + "/seed_construction").c_str(),
        &BM_StreamSeedConstruction<Engine>);
}

[[maybe_unused]] const bool kStreamBenchmarksRegistered = [] {
    RegisterEngineBenchmarks<RandX::Xoshiro256StarStar>("xoshiro256ss");
    RegisterEngineBenchmarks<RandX::Xoroshiro128StarStar>("xoroshiro128ss");
    RegisterEngineBenchmarks<RandX::Xoshiro128StarStar>("xoshiro128ss");
    return true;
}();
}

int main(int argc, char** argv)
{
    if (argc == 5 && (std::string(argv[1]) == "--randx-first-stream" ||
                      std::string(argv[1]) == "--randx-first-stream-legacy"))
    {
        try
        {
            const bool legacy = std::string(argv[1]) == "--randx-first-stream-legacy";
            const std::string engineName = argv[2];
            const std::uint64_t streamId = std::stoull(argv[3]);
            const std::uint64_t seed = std::stoull(argv[4]);
            if (engineName == "xoshiro256ss")
                ObserveFirstStream<RandX::Xoshiro256StarStar>(argv[2], streamId, seed, legacy);
            else if (engineName == "xoroshiro128ss")
                ObserveFirstStream<RandX::Xoroshiro128StarStar>(argv[2], streamId, seed, legacy);
            else if (engineName == "xoshiro128ss")
                ObserveFirstStream<RandX::Xoshiro128StarStar>(argv[2], streamId, seed, legacy);
            else
                throw std::invalid_argument("未知引擎");
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return 2;
        }
    }
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
