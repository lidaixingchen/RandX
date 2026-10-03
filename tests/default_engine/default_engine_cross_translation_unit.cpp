#if defined(RANDX_DEFAULT_ENGINE_TEST_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif

#include "../fixtures/default_engine.hpp"

namespace RandXTest::DefaultEngineFixtures
{
const void* DefaultEngineAddressFromBridge()
{
    return &RandX::DefaultEngine();
}

DefaultEngineState DefaultEngineStateFromBridge()
{
    return RandX::DefaultEngine().serialize();
}

std::uint64_t DefaultEngineOutputFromBridge()
{
    return RandX::DefaultEngine()();
}

void DefaultEngineReseedFromBridge(std::uint64_t seed)
{
    RandX::Reseed(seed);
}

void DefaultEngineReseedRandomFromBridge()
{
    RandX::ReseedRandom();
}

void DefaultEngineResetFromBridge()
{
    RandX::ResetThreadLocalEngine();
}
}
