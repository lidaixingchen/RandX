// Generate known sequences for unit tests
#include "RandX.hpp"
#include <cstdio>
#include <cinttypes>

int main()
{
    // Xoshiro256StarStar with seed 12345
    {
        RandX::Xoshiro256StarStar rng{ 12345 };
        std::printf("Xoshiro256StarStar seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %" PRIu64 "\n", rng());
    }
    // Xoshiro256PlusPlus with seed 12345
    {
        RandX::Xoshiro256PlusPlus rng{ 12345 };
        std::printf("Xoshiro256PlusPlus seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %" PRIu64 "\n", rng());
    }
    // Xoshiro256Plus with seed 12345
    {
        RandX::Xoshiro256Plus rng{ 12345 };
        std::printf("Xoshiro256Plus seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %" PRIu64 "\n", rng());
    }
    // Xoroshiro128PlusPlus with seed 12345
    {
        RandX::Xoroshiro128PlusPlus rng{ 12345 };
        std::printf("Xoroshiro128PlusPlus seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %" PRIu64 "\n", rng());
    }
    // Xoshiro128StarStar with seed 12345
    {
        RandX::Xoshiro128StarStar rng{ 12345 };
        std::printf("Xoshiro128StarStar seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %u\n", rng());
    }
    // Xoroshiro64StarStar with seed 12345
    {
        RandX::Xoroshiro64StarStar rng{ 12345 };
        std::printf("Xoroshiro64StarStar seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %u\n", rng());
    }
    // SplitMix64 with seed 12345
    {
        RandX::SplitMix64 rng{ 12345 };
        std::printf("SplitMix64 seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %" PRIu64 "\n", rng());
    }
    // RandIntCE
    {
        constexpr int v = RandX::RandIntCE(0, 100);
        std::printf("RandIntCE(0,100) = %d\n", v);
    }
    return 0;
}
