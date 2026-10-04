#ifndef TESTS_IMPLEMENTATION_TRIANGULAR_CONTRACTS_HPP
#define TESTS_IMPLEMENTATION_TRIANGULAR_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>
#include "doctest.h"
#include "../common/fixtures.hpp"
#include "../fixtures/triangular_reference.hpp"

namespace RandXTest::TriangularContractFixtures
{
template <class T, class WorkT, std::size_t Size>
void CheckTriangularReferenceCases(
    const std::array<RandXTest::TriangularReference::QuantileCase<T, WorkT>, Size>& cases)
{
    using Sample = std::tuple<T, T, T, WorkT, T>;
    std::vector<Sample> samples;
    samples.reserve(Size);
    for (const auto& reference : cases)
    {
        INFO("参考案例 = " << reference.name << ", 有效位 = " << std::numeric_limits<T>::digits
            << ", min = " << reference.min_value
            << ", peak = " << reference.peak_value << ", max = " << reference.max_value
            << ", u = " << reference.uniform);
        const RandX::detail::TriangularKernel<T> kernel(
            reference.min_value, reference.peak_value, reference.max_value);
        const T actual = kernel.Quantile(reference.uniform);
        CHECK(std::isfinite(actual));
        CHECK(actual >= reference.min_value);
        CHECK(actual < reference.max_value);
        if (reference.uniform == WorkT{0})
        {
            CHECK(actual == reference.min_value);
        }

        const WorkT actualWork = static_cast<WorkT>(actual);
        const WorkT expectedWork = static_cast<WorkT>(reference.expected);
        const T magnitude = (std::max)(std::abs(actual), std::abs(reference.expected));
        const WorkT spacing = static_cast<WorkT>(magnitude == T{0}
            ? std::nextafter(T{0}, (std::numeric_limits<T>::infinity)())
            : magnitude - std::nextafter(magnitude, T{0}));
        const WorkT ulpError = std::abs(actualWork / spacing - expectedWork / spacing);
        if (!reference.check_width_scaled_error)
        {
            constexpr WorkT DiscreteMaximumUlps = WorkT{1};
            const WorkT width = static_cast<WorkT>(reference.max_value)
                - static_cast<WorkT>(reference.min_value);
            const WorkT maximumUlps = width < std::numeric_limits<T>::min()
                ? DiscreteMaximumUlps : static_cast<WorkT>(RandXTest::TriangularReference::kMaximumUlps);
            CHECK(ulpError <= maximumUlps);
            if (std::nextafter(reference.min_value, reference.max_value) == reference.max_value)
            {
                CHECK(actual == reference.expected);
            }
        }

        if (reference.check_width_scaled_error)
        {
            CHECK(reference.min_value < T{0});
            CHECK(T{0} < reference.max_value);
            const WorkT width = static_cast<WorkT>(reference.max_value)
                - static_cast<WorkT>(reference.min_value);
            const WorkT scaledError = std::abs((actualWork - expectedWork) / width);
            CHECK(scaledError <= static_cast<WorkT>(reference.width_scaled_budget));
        }

        samples.emplace_back(
            reference.min_value, reference.peak_value, reference.max_value, reference.uniform, actual);
    }

    std::sort(samples.begin(), samples.end());
    for (std::size_t index = 1; index < samples.size(); ++index)
    {
        const Sample& previous = samples[index - 1];
        const Sample& current = samples[index];
        if (std::get<0>(previous) == std::get<0>(current)
            && std::get<1>(previous) == std::get<1>(current)
            && std::get<2>(previous) == std::get<2>(current)
            && std::get<3>(previous) < std::get<3>(current))
        {
            CHECK(std::get<4>(previous) <= std::get<4>(current));
        }
    }
}
}

TEST_SUITE("内部/分布辅助计算")
{
    TEST_CASE("TriangularKernel 高精度逆 CDF 参考、误差预算与最终单调性")
    {
        using namespace RandXTest::TriangularReference;
        RandXTest::TriangularContractFixtures::CheckTriangularReferenceCases(kFloatCases);
        RandXTest::TriangularContractFixtures::CheckTriangularReferenceCases(kDoubleCases);
#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
        RandXTest::TriangularContractFixtures::CheckTriangularReferenceCases(kLongDouble53Cases);
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
        RandXTest::TriangularContractFixtures::CheckTriangularReferenceCases(kLongDouble64Cases);
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384
        RandXTest::TriangularContractFixtures::CheckTriangularReferenceCases(kLongDouble113Cases);
#else
        const RandX::detail::TriangularKernel<long double> kernel(-2.0L, 0.5L, 4.0L);
        CHECK(kernel.Quantile(0.0L) == -2.0L);
        CHECK(kernel.Quantile(2.5L / 6.0L) == 0.5L);
        CHECK(kernel.Quantile(0.25L) < kernel.Quantile(0.75L));
#endif
    }

    TEST_CASE("TriangularKernel 内部任意分位输入范围")
    {
        const RandX::detail::TriangularKernel<double> kernel(-1.0, 0.25, 2.0);
        constexpr double PeakQuantile = 1.25 / 3.0;
        CHECK(kernel.Quantile(0.0) == -1.0);
        CHECK(kernel.Quantile(PeakQuantile) == 0.25);
        CHECK(kernel.Quantile(std::nextafter(PeakQuantile, 0.0)) <= 0.25);
        CHECK(kernel.Quantile(std::nextafter(PeakQuantile, 1.0)) >= 0.25);
        CHECK(kernel.Quantile(std::nextafter(1.0, 0.0)) < 2.0);
        CHECK_THROWS_AS((void)kernel.Quantile(-std::numeric_limits<double>::epsilon()), std::invalid_argument);
        CHECK_THROWS_AS((void)kernel.Quantile(1.0), std::invalid_argument);
        CHECK_THROWS_AS((void)kernel.Quantile(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    }

}

#endif
