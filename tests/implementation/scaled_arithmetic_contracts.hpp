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

    TEST_CASE("Gamma 与 Weibull 对数合并保留可表示值并接受真实零值和无穷")
    {
        const auto verifyType = [](auto type_tag)
        {
            using T = decltype(type_tag);
            using WorkT = RandX::detail::StableDistributionWorkType<T>;

            const T denorm_min = std::numeric_limits<T>::denorm_min();
            const T smallest_positive = denorm_min > T{0} ? denorm_min : std::numeric_limits<T>::min();
            const T maximum = (std::numeric_limits<T>::max)();
            const WorkT log_smallest = std::log(static_cast<WorkT>(smallest_positive));
            const WorkT log_maximum = std::log(static_cast<WorkT>(maximum));

            RandX::detail::DecomposedGammaLog<WorkT> gamma_sample;
            gamma_sample.base_log = log_smallest - WorkT{6};
            gamma_sample.exp_val = WorkT{0.25};
            gamma_sample.shape = WorkT{0.125};
            gamma_sample.has_extra = true;

            const T rescued_gamma = RandX::detail::ScaleGammaSampleFromLog<T>(gamma_sample, maximum);
            CHECK(std::isfinite(rescued_gamma));
            CHECK(rescued_gamma > T{0});

            gamma_sample.base_log = log_smallest - WorkT{1};
            gamma_sample.has_extra = false;
            CHECK(RandX::detail::ScaleGammaSampleFromLog<T>(gamma_sample, T{1}) == T{0});

            gamma_sample.base_log = log_maximum + WorkT{1};
            CHECK(std::isinf(RandX::detail::ScaleGammaSampleFromLog<T>(gamma_sample, T{1})));

            const WorkT weibull_shape = WorkT{0.125};
            const WorkT rescued_log = log_smallest - WorkT{8};
            const WorkT rescued_exponential = std::exp(rescued_log * weibull_shape);
            const WorkT scaled_weibull_log = RandX::detail::WeibullSampleLogValue(
                rescued_exponential, weibull_shape, static_cast<WorkT>(maximum));
            const T rescued_weibull = RandX::detail::ExpToPositiveSample<T>(scaled_weibull_log);
            CHECK(std::isfinite(rescued_weibull));
            CHECK(rescued_weibull > T{0});

            const WorkT underflow_exponential = std::exp(rescued_log * weibull_shape);
            const WorkT underflow_log = RandX::detail::WeibullSampleLogValue(
                underflow_exponential, weibull_shape, WorkT{1});
            CHECK(RandX::detail::ExpToPositiveSample<T>(underflow_log) == T{0});

            const WorkT overflow_log = RandX::detail::WeibullSampleLogValue(
                WorkT{2}, weibull_shape, static_cast<WorkT>(maximum));
            CHECK(std::isinf(RandX::detail::ExpToPositiveSample<T>(overflow_log)));
        };

        verifyType(float{});
        verifyType(double{});
        verifyType(static_cast<long double>(0));
    }

}

#endif
