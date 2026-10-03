#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <vector>
#if defined(STUDY_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif
#include "effolkronium/random.hpp"
constexpr std::uint64_t Seed = 12345;
constexpr std::size_t Iterations = 5000000;
constexpr int Lower = 1;
constexpr int Six = 6;
constexpr int Eight = 8;
constexpr int MediumUpper = 1000000;
constexpr unsigned IntBits = std::numeric_limits<std::uint32_t>::digits;
volatile std::uint64_t Sink = 0;
struct SeedSource { std::uint64_t operator()() const { return Seed; } };
using X64Static = effolkronium::basic_random_static<RandX::Xoshiro256StarStar, SeedSource>;
using X64Tls = effolkronium::basic_random_thread_local<RandX::Xoshiro256StarStar, SeedSource>;
struct High32 {
    using result_type = std::uint32_t;
    RandX::Xoshiro256StarStar& source;
    result_type operator()() { return static_cast<result_type>(source() >> IntBits); }
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return (std::numeric_limits<result_type>::max)(); }
};
template<class F> [[gnu::noinline]] std::uint64_t Sum(F f) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < Iterations; ++i) sum += static_cast<std::uint64_t>(f());
    return sum;
}
template<class F> void Measure(const char* name, F f) {
    Sink = f();
    const auto begin = std::chrono::steady_clock::now();
    Sink = f();
    const auto end = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(end - begin).count();
    std::cout << name << ',' << std::fixed << std::setprecision(3) << ns / static_cast<double>(Iterations) << '\n';
}
std::uint64_t Lemire64(RandX::Xoshiro256StarStar& e, std::uint64_t range) {
    __uint128_t product = static_cast<__uint128_t>(e()) * range;
    std::uint64_t low = static_cast<std::uint64_t>(product);
    if (low < range) {
        const std::uint64_t threshold = (std::uint64_t{0} - range) % range;
        while (low < threshold) { product = static_cast<__uint128_t>(e()) * range; low = static_cast<std::uint64_t>(product); }
    }
    return static_cast<std::uint64_t>(product >> std::numeric_limits<std::uint64_t>::digits);
}
int main(int argc, char** argv) {
    const int runtimeUpper = argc > 1 ? std::atoi(argv[1]) : Six;
    RandX::Reseed(Seed);
    effolkronium::random_static::seed(static_cast<std::uint32_t>(Seed));
    effolkronium::random_thread_local::seed(static_cast<std::uint32_t>(Seed));
    X64Static::engine() = RandX::Xoshiro256StarStar{Seed};
    X64Tls::engine() = RandX::Xoshiro256StarStar{Seed};
    auto& cached = RandX::DefaultEngine();
    RandX::Xoshiro256StarStar x64{Seed};
    RandX::Xoshiro128StarStar x32{Seed};
    std::mt19937 mt32{static_cast<std::uint32_t>(Seed)};
    std::mt19937_64 mt64{Seed};
    High32 high{x64};
    std::uniform_int_distribution<int> dist6(Lower, Six);
    std::uniform_int_distribution<int> distRuntime(Lower, runtimeUpper);
    Measure("raw_x64_local", [&] { return Sum([&] { return x64(); }); });
    Measure("raw_x32_local", [&] { return Sum([&] { return x32(); }); });
    Measure("raw_mt32_local", [&] { return Sum([&] { return mt32(); }); });
    Measure("raw_mt64_local", [&] { return Sum([&] { return mt64(); }); });
    Measure("raw_x64_default", [&] { return Sum([] { return RandX::DefaultEngine()(); }); });
    Measure("raw_x64_cached", [&] { return Sum([&] { return cached(); }); });
    Measure("six_x64_default", [] { return Sum([] { return RandX::RandInt(Lower, Six); }); });
    Measure("six_x64_cached", [&] { return Sum([&] { return RandX::RandInt(cached, Lower, Six); }); });
    Measure("six_x64_local", [&] { return Sum([&] { return RandX::RandInt(x64, Lower, Six); }); });
    Measure("six_x64_std_local", [&] { return Sum([&] { return std::uniform_int_distribution<int>(Lower, Six)(x64); }); });
    Measure("six_x64_dist_reuse", [&] { return Sum([&] { return dist6(x64); }); });
    Measure("six_x64_lemire_local", [&] { return Sum([&] { return Lemire64(x64, Six) + Lower; }); });
    Measure("six_x64_high32", [&] { return Sum([&] { return std::uniform_int_distribution<int>(Lower, Six)(high); }); });
    Measure("six_x32_local", [&] { return Sum([&] { return RandX::RandInt(x32, Lower, Six); }); });
    Measure("six_mt32_std_local", [&] { return Sum([&] { return std::uniform_int_distribution<int>(Lower, Six)(mt32); }); });
    Measure("six_mt64_std_local", [&] { return Sum([&] { return std::uniform_int_distribution<int>(Lower, Six)(mt64); }); });
    Measure("six_mt32_randx_local", [&] { return Sum([&] { return RandX::RandInt(mt32, Lower, Six); }); });
    Measure("six_mt32_static", [] { return Sum([] { return effolkronium::random_static::get(Lower, Six); }); });
    Measure("six_mt32_tls", [] { return Sum([] { return effolkronium::random_thread_local::get(Lower, Six); }); });
    Measure("six_x64_other_static", [] { return Sum([] { return X64Static::get(Lower, Six); }); });
    Measure("six_x64_other_tls", [] { return Sum([] { return X64Tls::get(Lower, Six); }); });
    Measure("runtime_x64_default", [&] { return Sum([&] { return RandX::RandInt(Lower, runtimeUpper); }); });
    Measure("runtime_x64_local", [&] { return Sum([&] { return RandX::RandInt(x64, Lower, runtimeUpper); }); });
    Measure("runtime_x64_std_local", [&] { return Sum([&] { return std::uniform_int_distribution<int>(Lower, runtimeUpper)(x64); }); });
    Measure("runtime_x64_dist_reuse", [&] { return Sum([&] { return distRuntime(x64); }); });
    Measure("runtime_x64_lemire", [&] { return Sum([&] { return Lemire64(x64, static_cast<std::uint64_t>(runtimeUpper)) + Lower; }); });
    Measure("runtime_x64_high32", [&] { return Sum([&] { return std::uniform_int_distribution<int>(Lower, runtimeUpper)(high); }); });
    Measure("runtime_mt32_local", [&] { return Sum([&] { return RandX::RandInt(mt32, Lower, runtimeUpper); }); });
    Measure("runtime_mt32_static", [&] { return Sum([&] { return effolkronium::random_static::get(Lower, runtimeUpper); }); });
    Measure("eight_x64_default", [] { return Sum([] { return RandX::RandInt(Lower, Eight); }); });
    Measure("eight_x64_local", [&] { return Sum([&] { return RandX::RandInt(x64, Lower, Eight); }); });
    Measure("eight_mt32_local", [&] { return Sum([&] { return RandX::RandInt(mt32, Lower, Eight); }); });
    Measure("medium_x64_default", [] { return Sum([] { return RandX::RandInt(Lower, MediumUpper); }); });
    Measure("medium_x64_local", [&] { return Sum([&] { return RandX::RandInt(x64, Lower, MediumUpper); }); });
    Measure("medium_mt32_local", [&] { return Sum([&] { return RandX::RandInt(mt32, Lower, MediumUpper); }); });
    Measure("full32_x64_local", [&] { return Sum([&] { return RandX::RandInt<std::uint32_t>(x64, 0, (std::numeric_limits<std::uint32_t>::max)()); }); });
    Measure("full32_mt32_local", [&] { return Sum([&] { return RandX::RandInt<std::uint32_t>(mt32, 0, (std::numeric_limits<std::uint32_t>::max)()); }); });
    Measure("full64_x64_local", [&] { return Sum([&] { return RandX::RandInt<std::uint64_t>(x64, 0, (std::numeric_limits<std::uint64_t>::max)()); }); });
    Measure("full64_mt32_local", [&] { return Sum([&] { return RandX::RandInt<std::uint64_t>(mt32, 0, (std::numeric_limits<std::uint64_t>::max)()); }); });
}
