#include <benchmark/benchmark.h>

#if defined(RANDX_BENCHMARK_CPP17)
#include <RandX_Cpp17.hpp>
#else
#include <RandX.hpp>
#endif

#include "benchmarks/common/default_api_benchmarks.hpp"

BENCHMARK_MAIN();
