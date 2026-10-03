
#ifdef RANDX_EXPERIMENT_CPP17
#include <RandX_Cpp17.hpp>
#else
#include <RandX.hpp>
#endif
#include <array>
#include <iostream>
#include <list>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>

using X128 = RandX::Xoshiro128StarStar;
using X256 = RandX::Xoshiro256StarStar;
using Mt64 = std::mt19937_64;
struct Default {};
enum class Path { Container, Iterator, Reservoir };
constexpr std::uint64_t ExperimentSeed = 20261004;
constexpr std::int64_t SmallPopulation = 256;
constexpr std::int64_t LargePopulation = 65536;
constexpr std::int64_t EmptyPopulation = 0;
constexpr std::int64_t EmptyRequest = 0;
constexpr std::int64_t SingleRequest = 1;
constexpr std::int64_t PairRequest = 2;
constexpr std::int64_t SmallBitmapRequest = SmallPopulation / 4;
constexpr std::int64_t LargeBitmapRequest = LargePopulation / 64;
constexpr std::int64_t SmallDenseRequest = SmallPopulation / 2 + 1;
constexpr std::int64_t LargeDenseRequest = LargePopulation / 2 + 1;
using Parameters = std::array<std::int64_t, 2>;
constexpr std::array<Parameters, 8> ContainerCases{{
    {EmptyPopulation, SingleRequest}, {SmallPopulation, EmptyRequest},
    {SmallPopulation, SingleRequest}, {SmallPopulation, PairRequest},
    {SmallPopulation, SmallBitmapRequest}, {SmallPopulation, SmallDenseRequest},
    {LargePopulation, LargeBitmapRequest}, {LargePopulation, LargeDenseRequest}}};
constexpr std::array<Parameters, 5> IteratorCases{{
    {SmallPopulation, SingleRequest}, {SmallPopulation, PairRequest},
    {SmallPopulation, SmallBitmapRequest}, {SmallPopulation, SmallDenseRequest},
    {LargePopulation, LargeDenseRequest}}};
constexpr std::array<Parameters, 3> ReservoirCases{{
    {SmallPopulation, SingleRequest}, {SmallPopulation, SmallBitmapRequest},
    {LargePopulation, LargeBitmapRequest}}};

template<class Engine> static Engine MakeEngine() {
    if constexpr (std::is_same_v<Engine, Default>) {
        RandX::Reseed(ExperimentSeed);
        return {};
    } else return Engine{ExperimentSeed};
}
template<Path Kind>
using Storage = std::conditional_t<Kind == Path::Reservoir, std::list<int>, std::vector<int>>;
template<class Engine, Path Kind>
static auto Draw(Engine& engine, Storage<Kind>& values, std::size_t request) {
    if constexpr (Kind == Path::Container) {
        if constexpr (std::is_same_v<Engine, Default>) return RandX::RandSample(values, request);
        else return RandX::RandSample(engine, values, request);
    } else {
        const auto count = static_cast<std::ptrdiff_t>(request);
        if constexpr (std::is_same_v<Engine, Default>) return RandX::RandSample(values.cbegin(), values.cend(), count);
        else return RandX::RandSample(engine, values.cbegin(), values.cend(), count);
    }
}
template<class Engine> static void EmitState(Engine& engine) {
    if constexpr (std::is_same_v<Engine, Default>) EmitState(RandX::DefaultEngine());
    else if constexpr (std::is_same_v<Engine, Mt64>) std::cout << engine << '\n';
    else { for (auto value : engine.serialize()) std::cout << value << ' '; std::cout << '\n'; }
}
template<class Engine, Path Kind, std::size_t N>
static void Verify(const std::array<Parameters, N>& cases) {
    for (auto parameters : cases) {
        Storage<Kind> values(static_cast<std::size_t>(parameters[0]));
        std::iota(values.begin(), values.end(), 0);
        auto engine = MakeEngine<Engine>();
        auto result = Draw<Engine, Kind>(engine, values, static_cast<std::size_t>(parameters[1]));
        for (auto value : result) std::cout << value << ' ';
        std::cout << '\n'; EmitState(engine);
    }
}
template<class Engine> static void VerifyEngine() {
    Verify<Engine, Path::Container>(ContainerCases);
    Verify<Engine, Path::Iterator>(IteratorCases);
    Verify<Engine, Path::Reservoir>(ReservoirCases);
}
int main() {
    VerifyEngine<X128>(); VerifyEngine<X256>(); VerifyEngine<Mt64>(); VerifyEngine<Default>();
    for (auto size : {SmallPopulation, LargePopulation}) {
        std::vector<int> values(static_cast<std::size_t>(size));
        std::iota(values.begin(), values.end(), 0); RandX::Reseed(ExperimentSeed); RandX::RandShuffle(values);
        for (auto value : values) std::cout << value << ' ';
        std::cout << '\n'; EmitState(RandX::DefaultEngine());
    }
    return 0;
}
