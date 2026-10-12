#ifndef RANDX_TESTS_IMPLEMENTATION_SCALED_ARITHMETIC_CONTRACTS_HPP
#define RANDX_TESTS_IMPLEMENTATION_SCALED_ARITHMETIC_CONTRACTS_HPP

#include <cmath>
#include <limits>
#include "doctest.h"

TEST_SUITE("内部/分布辅助计算")
{
	TEST_CASE("StableAffineTransform 区分可表示抵消与真实溢出")
	{
		const auto checkType = [](auto type)
		{
			using T = decltype(type);
			const T maximum = (std::numeric_limits<T>::max)();
			const T unitSample = T{1} + std::numeric_limits<T>::epsilon();
			const T finiteReference = RandXTest::AffineStabilityContracts::ComputeReferenceAffine(
				-maximum, maximum, unitSample);
			CHECK(std::isfinite(finiteReference));
			CHECK(RandX::detail::StableAffineTransform(-maximum, maximum, unitSample) == finiteReference);

			const T overflowingReference = RandXTest::AffineStabilityContracts::ComputeReferenceAffine(
				maximum, maximum, unitSample);
			CHECK(std::isinf(overflowingReference));
			CHECK(RandX::detail::StableAffineTransform(maximum, maximum, unitSample) == overflowingReference);
		};

		checkType(float{});
		checkType(double{});
		checkType(static_cast<long double>(0));
	}

}

#endif
