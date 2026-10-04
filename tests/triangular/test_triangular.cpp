#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#if defined(RANDX_TRIANGULAR_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif
#include "../common/triangular_distribution_contracts.hpp"
#include "../common/triangular_statistical_contracts.hpp"
#include "../implementation/triangular_contracts.hpp"
