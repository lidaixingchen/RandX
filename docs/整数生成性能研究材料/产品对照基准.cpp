#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <numeric>
#include <random>
#include <vector>
#if defined(BENCH_RANDX)
#include "RandX.hpp"
#elif defined(BENCH_RANDX_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "effolkronium/random.hpp"
#if defined(BENCH_STATIC)
using Random = effolkronium::random_static;
#else
using Random = effolkronium::random_thread_local;
#endif
#endif
constexpr std::uint64_t Seed = 12345;
constexpr std::size_t ScalarCount = 5000000;
constexpr std::size_t NormalCount = 500000;
constexpr std::size_t ShuffleCount = 10000;
constexpr std::size_t ShuffleSize = 1024;
constexpr std::size_t PopulationSize = 1000000;
constexpr std::size_t SampleSize = 100;
constexpr std::size_t SampleCount = 100;
constexpr std::size_t WarmupCount = 10000;
constexpr int DiceMin = 1;
constexpr int DiceMax = 6;
volatile std::uint64_t IntegerSink = 0;
volatile double RealSink = 0;
#if defined(BENCH_RANDX) || defined(BENCH_RANDX_CPP17)
void Reset() { RandX::Reseed(Seed); }
int Dice() { return RandX::RandInt(DiceMin, DiceMax); }
double Real() { return RandX::RandReal(0.0, 1.0); }
double Normal() { return RandX::RandNormal(0.0, 1.0); }
void Shuffle(std::vector<int>& values) { RandX::RandShuffle(values); }
std::vector<int> Sample(const std::vector<int>& values) { return RandX::RandSample(values, SampleSize); }
using Engine = RandX::Xoshiro256StarStar;
#else
void Reset() { Random::seed(static_cast<Random::engine_type::result_type>(Seed)); }
int Dice() { return Random::get(DiceMin, DiceMax); }
double Real() { return Random::get(0.0, 1.0); }
double Normal() { return Random::get<std::normal_distribution<double>>(0.0, 1.0); }
void Shuffle(std::vector<int>& values) { Random::shuffle(values); }
std::vector<int> Sample(const std::vector<int>& values) {
    std::vector<int> output;
    output.reserve(SampleSize);
    std::sample(values.begin(), values.end(), std::back_inserter(output), SampleSize, Random::engine());
    return output;
}
using Engine = Random::engine_type;
#endif
using Clock = std::chrono::steady_clock;
template<class Work> void Measure(const char* task, std::size_t count, Work work) {
    Reset();
    const auto start = Clock::now();
    work();
    const auto end = Clock::now();
    const double ns = std::chrono::duration<double, std::nano>(end - start).count();
    std::cout << task << ',' << std::fixed << std::setprecision(3) << ns / static_cast<double>(count) << '\n';
}
int main() {
    Reset();
    for (std::size_t i = 0; i < WarmupCount; ++i) IntegerSink = static_cast<std::uint64_t>(Dice());
    std::cout << "engine_bytes," << sizeof(Engine) << '\n';
    Measure("dice_ns", ScalarCount, [] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < ScalarCount; ++i) sum += static_cast<std::uint64_t>(Dice());
        IntegerSink = sum;
    });
    Measure("real_ns", ScalarCount, [] {
        double sum = 0;
        for (std::size_t i = 0; i < ScalarCount; ++i) sum += Real();
        RealSink = sum;
    });
    Measure("normal_ns", NormalCount, [] {
        double sum = 0;
        for (std::size_t i = 0; i < NormalCount; ++i) sum += Normal();
        RealSink = sum;
    });
    std::vector<int> values(ShuffleSize);
    std::iota(values.begin(), values.end(), 0);
    Measure("shuffle_ns", ShuffleCount, [&values] {
        for (std::size_t i = 0; i < ShuffleCount; ++i) {
            Shuffle(values);
            asm volatile("" : : "g"(values.data()) : "memory");
        }
        IntegerSink = static_cast<std::uint64_t>(values.front());
    });
    std::vector<int> population(PopulationSize);
    std::iota(population.begin(), population.end(), 0);
    const auto validation = Sample(population);
    auto sorted = validation;
    std::sort(sorted.begin(), sorted.end());
    if (sorted.size() != SampleSize || std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()
        || sorted.front() < 0 || static_cast<std::size_t>(sorted.back()) >= PopulationSize) return 1;
    Measure("sample_ns", SampleCount, [&population] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < SampleCount; ++i) {
            const auto output = Sample(population);
            sum += static_cast<std::uint64_t>(output.front()) + output.size();
        }
        IntegerSink = sum;
    });
}
