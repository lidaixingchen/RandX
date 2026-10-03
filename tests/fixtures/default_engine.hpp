#ifndef RANDX_TESTS_FIXTURES_DEFAULT_ENGINE_HPP
#define RANDX_TESTS_FIXTURES_DEFAULT_ENGINE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace RandXTest::DefaultEngineFixtures
{
constexpr std::uint64_t kRawOutputSeed = 12345;
constexpr std::size_t kDefaultEngineStateWords = 4;
constexpr std::size_t kRawOutputCount = 5;
constexpr std::size_t kRangeSampleCount = 32;
constexpr std::uint64_t kCrossTranslationUnitSeed = 0x4182A65D;
constexpr std::uint64_t kSecondReseedSeed = 0x9B7D31E4;
constexpr std::uint64_t kMainThreadSeed = 0x24D9C761;
constexpr std::uint64_t kWorkerThreadSeed = 0xD8437A15;
constexpr std::uint64_t kPostResetWorkerSeed = 0x572AC908;
constexpr int kSingleValue = 42;
constexpr int kDiceMinimum = 1;
constexpr int kDiceMaximum = 6;
constexpr unsigned int kPowerOfTwoRangeExponent = 8;
constexpr unsigned int kPowerOfTwoRangeSize = 1U << kPowerOfTwoRangeExponent;
constexpr std::int64_t kCrossZeroMinimum = -19;
constexpr std::int64_t kCrossZeroMaximum = 37;

using DefaultEngineState = std::array<std::uint64_t, kDefaultEngineStateWords>;

constexpr std::array<std::uint64_t, kRawOutputCount> kExpectedRawOutputs{
    13720838825685603483ULL,
    2398916695208396998ULL,
    17770384849984869256ULL,
    891717726879801395ULL,
    10241316046318454344ULL,
};

const void* DefaultEngineAddressFromBridge();
DefaultEngineState DefaultEngineStateFromBridge();
std::uint64_t DefaultEngineOutputFromBridge();
void DefaultEngineReseedFromBridge(std::uint64_t seed);
void DefaultEngineReseedRandomFromBridge();
void DefaultEngineResetFromBridge();
}

#endif
