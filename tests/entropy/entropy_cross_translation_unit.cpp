#if defined(RANDX_ENTROPY_TEST_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif

#include <cstddef>
#include <cstdint>

namespace RandXTest::EntropyFixtures
{
void EntropyCrossTranslationUnitSecureRandomBytes(void* buffer, std::size_t length)
{
    RandX::SecureRandomBytes(buffer, length);
}

std::uint64_t EntropyCrossTranslationUnitSecureSeed()
{
    return RandX::SecureSeed();
}
}
