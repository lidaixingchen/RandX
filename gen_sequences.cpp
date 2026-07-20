// Generate known sequences for unit tests
#include "Random.hpp"
#include <cstdio>

int main()
{
    // Xoshiro256StarStar with seed 12345
    {
        xoshiro::Xoshiro256StarStar rng{ 12345 };
        std::printf("Xoshiro256StarStar seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %llu\n", rng());
    }
    // Xoshiro256PlusPlus with seed 12345
    {
        xoshiro::Xoshiro256PlusPlus rng{ 12345 };
        std::printf("Xoshiro256PlusPlus seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %llu\n", rng());
    }
    // Xoshiro256Plus with seed 12345
    {
        xoshiro::Xoshiro256Plus rng{ 12345 };
        std::printf("Xoshiro256Plus seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %llu\n", rng());
    }
    // Xoroshiro128PlusPlus with seed 12345
    {
        xoshiro::Xoroshiro128PlusPlus rng{ 12345 };
        std::printf("Xoroshiro128PlusPlus seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %llu\n", rng());
    }
    // Xoshiro128StarStar with seed 12345
    {
        xoshiro::Xoshiro128StarStar rng{ 12345 };
        std::printf("Xoshiro128StarStar seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %u\n", rng());
    }
    // Xoroshiro64StarStar with seed 12345
    {
        xoshiro::Xoroshiro64StarStar rng{ 12345 };
        std::printf("Xoroshiro64StarStar seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %u\n", rng());
    }
    // SplitMix64 with seed 12345
    {
        xoshiro::SplitMix64 rng{ 12345 };
        std::printf("SplitMix64 seed=12345:\n");
        for (int i = 0; i < 5; ++i)
            std::printf("  %llu\n", rng());
    }
    // RandIntCE
    {
        constexpr int v = xoshiro::RandIntCE(0, 100);
        std::printf("RandIntCE(0,100) = %d\n", v);
    }
    return 0;
}
