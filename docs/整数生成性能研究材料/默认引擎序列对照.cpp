#if defined(RANDX_SEQUENCE_CPP17)
#include <RandX_Cpp17.hpp>
#else
#include <RandX.hpp>
#endif

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
constexpr std::uint64_t kSequenceSeed = 12345;
constexpr std::size_t kSamplesPerRange = 20000;
constexpr int kDiceMinimum = 1;
constexpr int kDiceMaximum = 6;
constexpr int kSingleValue = 42;
constexpr unsigned int kPowerOfTwoRangeWidth = 256;
constexpr std::int64_t kCrossZeroMinimum = -19;
constexpr std::int64_t kCrossZeroMaximum = 37;
constexpr std::size_t kContainerSize = 256;
constexpr std::size_t kSampleSize = 16;

void PrintState()
{
    for (const auto word : RandX::DefaultEngine().serialize())
        std::cout << word << ' ';
    std::cout << '\n';
}

template <typename Integer>
void PrintRange(const char* name, Integer minimum, Integer maximum)
{
    std::cout << name << '\n';
    RandX::Reseed(kSequenceSeed);
    for (std::size_t index = 0; index < kSamplesPerRange; ++index)
        std::cout << RandX::RandInt<Integer>(minimum, maximum) << '\n';
    PrintState();
}
}

int main()
{
    PrintRange("骰子", kDiceMinimum, kDiceMaximum);
    PrintRange("二的幂", 0U, kPowerOfTwoRangeWidth - 1);
    PrintRange("单值", kSingleValue, kSingleValue);
    PrintRange("跨零", kCrossZeroMinimum, kCrossZeroMaximum);
    PrintRange("完整有符号", (std::numeric_limits<std::int64_t>::lowest)(),
               (std::numeric_limits<std::int64_t>::max)());
    PrintRange("完整32位无符号", (std::numeric_limits<std::uint32_t>::lowest)(),
               (std::numeric_limits<std::uint32_t>::max)());
    PrintRange("完整64位无符号", (std::numeric_limits<std::uint64_t>::lowest)(),
               (std::numeric_limits<std::uint64_t>::max)());

    RandX::Reseed(kSequenceSeed);
    std::cout << std::hexfloat;
    for (std::size_t index = 0; index < kSamplesPerRange; ++index)
    {
        std::cout << RandX::DefaultEngine()() << ' '
                  << RandX::RandReal() << ' ' << RandX::RandNormal() << '\n';
    }
    PrintState();
    std::vector<int> values(kContainerSize);
    std::iota(values.begin(), values.end(), 0);
    RandX::RandShuffle(values);
    for (const auto value : values)
        std::cout << value << ' ';
    std::cout << '\n';
    for (const auto value : RandX::RandSample(values, kSampleSize))
        std::cout << value << ' ';
    std::cout << '\n';
    PrintState();
}
