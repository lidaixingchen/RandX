#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#if defined(RANDX_CONSUMER_STANDARD_17)
#include <RandX_Cpp17.hpp>
#else
#include <RandX.hpp>
#endif

int main()
{
    constexpr std::uint64_t OrdinarySeed = 20261005;
    constexpr int MinimumIntegerSample = 1;
    constexpr int MaximumIntegerSample = 6;
    constexpr std::size_t CustomStringLength = 8;
    constexpr std::size_t UuidLength = 36;
    constexpr std::string_view CustomCharacterSet = "abcdef";

    RandX::Xoshiro256StarStar ordinary{OrdinarySeed};
    const int integer_sample = RandX::RandInt(ordinary, MinimumIntegerSample, MaximumIntegerSample);
    const double real_sample = RandX::RandNormal(ordinary, 0.0, 1.0);
    const std::string custom_string = RandX::RandString(ordinary, CustomStringLength, CustomCharacterSet);
    const std::string uuid = RandX::RandUUID(ordinary);

    RandX::ChaCha20 secure_engine;
    const std::uint64_t secure_sample = secure_engine();
    const std::uint64_t secure_seed = RandX::SecureSeed();

    if (integer_sample < MinimumIntegerSample || integer_sample > MaximumIntegerSample || !std::isfinite(real_sample))
        return 1;
    if (custom_string.size() != CustomStringLength || uuid.size() != UuidLength)
        return 2;

    static_cast<void>(secure_sample);
    static_cast<void>(secure_seed);
    return 0;
}
