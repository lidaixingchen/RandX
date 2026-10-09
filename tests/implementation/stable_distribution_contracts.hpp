#ifndef RANDX_TESTS_IMPLEMENTATION_STABLE_DISTRIBUTION_CONTRACTS_HPP
#define RANDX_TESTS_IMPLEMENTATION_STABLE_DISTRIBUTION_CONTRACTS_HPP

#include <cmath>
#include <limits>
#include "doctest.h"

TEST_SUITE("内部/共同/分布数值")
{
    TEST_CASE("极小自由度下相邻指数补偿保留比值方向")
    {
        const double degrees = std::numeric_limits<double>::denorm_min();
        const double adjacent = std::nextafter(1.0, 2.0);
        const RandX::detail::NormalizedGammaLogSample<double> largerCorrection{0.0, adjacent, degrees};
        const RandX::detail::NormalizedGammaLogSample<double> smallerCorrection{0.0, 1.0, degrees};
        const double decreasing = RandX::detail::ComputeNormalizedGammaLogRatio(largerCorrection, smallerCorrection);
        const double increasing = RandX::detail::ComputeNormalizedGammaLogRatio(smallerCorrection, largerCorrection);
        CHECK(decreasing < 0.0);
        CHECK(increasing > 0.0);
        CHECK(RandX::detail::ExpToPositiveSample<double>(decreasing) == 0.0);
        CHECK(std::isinf(RandX::detail::ExpToPositiveSample<double>(increasing)));
        CHECK(RandX::detail::ComputeNormalizedGammaLogRatio(smallerCorrection, smallerCorrection) == 0.0);
    }
}

#endif
