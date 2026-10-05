#include "RandX.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    using Engine = RandX::Xoshiro256StarStar;

    constexpr std::uint64_t kSeed = 0x4D595DF4D0F33173ULL;
    constexpr std::uint64_t kStreamHalfBits = std::numeric_limits<std::uint32_t>::digits;
    constexpr std::uint64_t kLowStreamMask = (std::numeric_limits<std::uint32_t>::max)();
    constexpr std::uint64_t kStreamCount = 4;

    std::uint64_t highStreamIndex = 0;
    std::uint64_t lowStreamIndex = kLowStreamMask - 1;
    Engine highSegmentStart = RandX::MakeStreamEngine<Engine>(highStreamIndex << kStreamHalfBits, kSeed);
    Engine current = RandX::MakeStreamEngine<Engine>(
        (highStreamIndex << kStreamHalfBits) | lowStreamIndex,
        kSeed);

    for (std::uint64_t index = 0; index < kStreamCount; ++index)
    {
        Engine stream = current;
        std::cout << highStreamIndex << ':' << lowStreamIndex << ' ' << stream() << '\n';

        if (index + 1 == kStreamCount)
            break;

        if (lowStreamIndex == kLowStreamMask)
        {
            ++highStreamIndex;
            highSegmentStart.longJump();
            current = highSegmentStart;
            lowStreamIndex = 0;
        }
        else
        {
            current.jump();
            ++lowStreamIndex;
        }
    }
}
