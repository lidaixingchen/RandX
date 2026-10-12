#ifndef RANDX_TESTS_COMMON_AFFINE_STABILITY_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_AFFINE_STABILITY_CONTRACTS_HPP

#include <array>
#include <cmath>
#include <limits>
#include <random>

#include "doctest.h"
#include "fixtures.hpp"

namespace RandXTest::AffineStabilityContracts
{
constexpr double kOrdinaryLocation = 2.5;
constexpr double kOrdinaryScale = 1.25;
constexpr double kBinaryScaleFactor = 2.0;

template <class T>
class PoleQuantileEngine
{
public:
    using result_type = std::uint64_t;
    explicit PoleQuantileEngine(result_type value) : value_(value) {}
    static constexpr result_type min() noexcept { return 0; }
    static constexpr int EntropyBits = std::numeric_limits<T>::digits < std::numeric_limits<result_type>::digits
        ? std::numeric_limits<T>::digits : std::numeric_limits<result_type>::digits;
    static constexpr result_type max() noexcept
    {
        return (std::numeric_limits<result_type>::max)() >> (std::numeric_limits<result_type>::digits - EntropyBits);
    }
    result_type operator()() noexcept
    {
        ++calls;
        if (calls <= PrefixDrawCount)
            return value_;
        return fallback_() & max();
    }
    std::size_t calls{0};
private:
    static constexpr int WorkDigits = std::numeric_limits<T>::digits > std::numeric_limits<double>::digits
        ? std::numeric_limits<T>::digits : std::numeric_limits<double>::digits;
    static constexpr std::size_t PrefixDrawCount = (WorkDigits + EntropyBits - 1) / EntropyBits;
    result_type value_;
    RandX::Xoshiro256StarStar fallback_{ RandXTest::TestConstants::kDefaultEngineTestSeed };
};

template <class T>
T ComputeReferenceAffine(T location, T scale, T unitSample)
{
	const T binaryScaleFactor = static_cast<T>(kBinaryScaleFactor);
	const T scaledResult = std::fma(scale / binaryScaleFactor, unitSample, location / binaryScaleFactor);
	return scaledResult * binaryScaleFactor;
}

template <class T, class DistributionFactory, class ExplicitSampler, class DefaultSampler>
void CheckOrdinaryParameters(
	T location,
	T scale,
	DistributionFactory makeDistribution,
	ExplicitSampler sampleExplicit,
	DefaultSampler sampleDefault)
{
	using Engine = RandX::Xoshiro256StarStar;
	const std::uint64_t seed = RandXTest::TestConstants::kDefaultEngineTestSeed;
	Engine explicitActualEngine{ seed };
	Engine explicitExpectedEngine{ seed };
	auto explicitDistribution = makeDistribution(location, scale);
	const T explicitExpected = explicitDistribution(explicitExpectedEngine);
	const T explicitActual = sampleExplicit(explicitActualEngine, location, scale);
	CHECK(explicitActual == explicitExpected);
	CHECK(explicitActualEngine == explicitExpectedEngine);

	RandX::Reseed(seed);
	Engine defaultExpectedEngine = RandX::DefaultEngine();
	auto defaultDistribution = makeDistribution(location, scale);
	const T defaultExpected = defaultDistribution(defaultExpectedEngine);
	const T defaultActual = sampleDefault(location, scale);
	CHECK(defaultActual == defaultExpected);
	CHECK(RandX::DefaultEngine() == defaultExpectedEngine);
}

template <class T, class DistributionFactory, class ExplicitSampler, class DefaultSampler>
void CheckExtremeScale(
	DistributionFactory makeDistribution,
	ExplicitSampler sampleExplicit,
	DefaultSampler sampleDefault)
{
	using Engine = RandX::Xoshiro256StarStar;
	const std::uint64_t seed = RandXTest::TestConstants::kDefaultEngineTestSeed;
	const T scale = (std::numeric_limits<T>::max)();

	Engine referenceEngine{ seed };
	auto unitDistribution = makeDistribution(T{0}, T{1});
	const T unitSample = unitDistribution(referenceEngine);
	CHECK(std::isfinite(unitSample));
	CHECK(unitSample != T{0});

	const T cancelingLocation = std::signbit(unitSample) ? scale : -scale;
	const T sampleMagnitudeLimit = static_cast<T>(kBinaryScaleFactor);
	CHECK(std::abs(unitSample) < sampleMagnitudeLimit);
	const T finiteReference = ComputeReferenceAffine(cancelingLocation, scale, unitSample);
	CHECK(std::isfinite(finiteReference));

	Engine explicitActualEngine{ seed };
	Engine explicitExpectedEngine{ seed };
	auto explicitExpectedDistribution = makeDistribution(T{0}, T{1});
	const T explicitUnitSample = explicitExpectedDistribution(explicitExpectedEngine);
	const T explicitExpected = ComputeReferenceAffine(cancelingLocation, scale, explicitUnitSample);
	const T explicitActual = sampleExplicit(explicitActualEngine, cancelingLocation, scale);
	CHECK(explicitActual == explicitExpected);
	CHECK(std::isfinite(explicitActual));
	CHECK(explicitActualEngine == explicitExpectedEngine);

	RandX::Reseed(seed);
	Engine defaultExpectedEngine = RandX::DefaultEngine();
	auto defaultExpectedDistribution = makeDistribution(T{0}, T{1});
	const T defaultUnitSample = defaultExpectedDistribution(defaultExpectedEngine);
	const T defaultExpected = ComputeReferenceAffine(cancelingLocation, scale, defaultUnitSample);
	const T defaultActual = sampleDefault(cancelingLocation, scale);
	CHECK(defaultActual == defaultExpected);
	CHECK(std::isfinite(defaultActual));
	CHECK(RandX::DefaultEngine() == defaultExpectedEngine);

	const T outwardLocation = -cancelingLocation;
	const T overflowingReference = ComputeReferenceAffine(outwardLocation, scale, unitSample);
	CHECK(std::isinf(overflowingReference));

	Engine explicitOverflowEngine{ seed };
	Engine explicitOverflowExpectedEngine{ seed };
	auto explicitOverflowDistribution = makeDistribution(T{0}, T{1});
	const T explicitOverflowUnitSample = explicitOverflowDistribution(explicitOverflowExpectedEngine);
	const T explicitOverflowExpected = ComputeReferenceAffine(outwardLocation, scale, explicitOverflowUnitSample);
	const T explicitOverflowActual = sampleExplicit(explicitOverflowEngine, outwardLocation, scale);
	CHECK(explicitOverflowActual == explicitOverflowExpected);
	CHECK(std::isinf(explicitOverflowActual));
	CHECK(explicitOverflowEngine == explicitOverflowExpectedEngine);

	RandX::Reseed(seed);
	Engine defaultOverflowExpectedEngine = RandX::DefaultEngine();
	auto defaultOverflowDistribution = makeDistribution(T{0}, T{1});
	const T defaultOverflowUnitSample = defaultOverflowDistribution(defaultOverflowExpectedEngine);
	const T defaultOverflowExpected = ComputeReferenceAffine(outwardLocation, scale, defaultOverflowUnitSample);
	const T defaultOverflowActual = sampleDefault(outwardLocation, scale);
	CHECK(defaultOverflowActual == defaultOverflowExpected);
	CHECK(std::isinf(defaultOverflowActual));
	CHECK(RandX::DefaultEngine() == defaultOverflowExpectedEngine);
}
}

TEST_SUITE("公共/基础/分布")
{
    TEST_CASE("Cauchy 大单位分位保留有限的尺度抵消结果")
    {
        constexpr long double OverflowProductRatio = 1.5L;
        const auto verifyType = [=](auto type_tag)
        {
            using T = decltype(type_tag);
            using PoleQuantileEngine = RandXTest::AffineStabilityContracts::PoleQuantileEngine<T>;
            const T maximum = (std::numeric_limits<T>::max)();
            using Word = typename PoleQuantileEngine::result_type;
            const std::array<Word, 3> candidates{ Word{0}, Word{1}, PoleQuantileEngine::max() / Word{2} };
            T largest_sample{0};
            Word selected_word{0};
            for (const Word candidate : candidates)
            {
                PoleQuantileEngine candidate_engine{ candidate };
                std::cauchy_distribution<T> candidate_distribution(T{0}, T{1});
                const T sample = candidate_distribution(candidate_engine);
                if (std::isfinite(sample) && std::abs(sample) > std::abs(largest_sample))
                {
                    largest_sample = sample;
                    selected_word = candidate;
                }
            }
            REQUIRE(std::abs(largest_sample) > T{1});
            PoleQuantileEngine reference_engine{ selected_word };
            std::cauchy_distribution<T> unit_distribution(T{0}, T{1});
            const T unit_sample = unit_distribution(reference_engine);
            REQUIRE(std::isfinite(unit_sample));
            const T scale = (maximum / std::abs(unit_sample)) * static_cast<T>(OverflowProductRatio);
            const T location = std::signbit(unit_sample) ? maximum : -maximum;
#if defined(_MSC_VER)
            CHECK(scale < maximum * std::numeric_limits<T>::epsilon());
#endif
            const T expected = RandXTest::AffineStabilityContracts::ComputeReferenceAffine(
                location, scale, unit_sample);
            REQUIRE(std::isfinite(expected));
            PoleQuantileEngine actual_engine{ selected_word };
            const T actual = RandX::RandCauchy(actual_engine, location, scale);
            CHECK(actual == expected);
            CHECK(std::isfinite(actual));
            CHECK(actual_engine.calls == reference_engine.calls);
        };
        verifyType(float{});
        verifyType(double{});
        verifyType(static_cast<long double>(0));
    }

	TEST_CASE("Normal、Cauchy 与 ExtremeValue 常规参数保持标准库序列")
	{
		const auto normalFactory = [](auto location, auto scale)
		{
			return std::normal_distribution<decltype(location)>(location, scale);
		};
		const auto cauchyFactory = [](auto location, auto scale)
		{
			return std::cauchy_distribution<decltype(location)>(location, scale);
		};
		const auto extremeValueFactory = [](auto location, auto scale)
		{
			return std::extreme_value_distribution<decltype(location)>(location, scale);
		};
		const auto normalExplicit = [](auto& engine, auto location, auto scale)
		{
			return RandX::RandNormal(engine, location, scale);
		};
		const auto normalDefault = [](auto location, auto scale)
		{
			return RandX::RandNormal(location, scale);
		};
		const auto cauchyExplicit = [](auto& engine, auto location, auto scale)
		{
			return RandX::RandCauchy(engine, location, scale);
		};
		const auto cauchyDefault = [](auto location, auto scale)
		{
			return RandX::RandCauchy(location, scale);
		};
		const auto extremeValueExplicit = [](auto& engine, auto location, auto scale)
		{
			return RandX::RandExtremeValue(engine, location, scale);
		};
		const auto extremeValueDefault = [](auto location, auto scale)
		{
			return RandX::RandExtremeValue(location, scale);
		};
		const auto checkType = [&](auto type)
		{
			using T = decltype(type);
			const T location = static_cast<T>(RandXTest::AffineStabilityContracts::kOrdinaryLocation);
			const T scale = static_cast<T>(RandXTest::AffineStabilityContracts::kOrdinaryScale);
			RandXTest::AffineStabilityContracts::CheckOrdinaryParameters<T>(
				location, scale, normalFactory, normalExplicit, normalDefault);
			RandXTest::AffineStabilityContracts::CheckOrdinaryParameters<T>(
				location, scale, cauchyFactory, cauchyExplicit, cauchyDefault);
			RandXTest::AffineStabilityContracts::CheckOrdinaryParameters<T>(
				location, scale, extremeValueFactory, extremeValueExplicit, extremeValueDefault);
		};

		checkType(float{});
		checkType(double{});
		checkType(static_cast<long double>(0));
	}

	TEST_CASE("Normal、Cauchy 与 ExtremeValue 极端尺度保留可表示仿射结果")
	{
		const auto normalFactory = [](auto location, auto scale)
		{
			return std::normal_distribution<decltype(location)>(location, scale);
		};
		const auto cauchyFactory = [](auto location, auto scale)
		{
			return std::cauchy_distribution<decltype(location)>(location, scale);
		};
		const auto extremeValueFactory = [](auto location, auto scale)
		{
			return std::extreme_value_distribution<decltype(location)>(location, scale);
		};
		const auto normalExplicit = [](auto& engine, auto location, auto scale)
		{
			return RandX::RandNormal(engine, location, scale);
		};
		const auto normalDefault = [](auto location, auto scale)
		{
			return RandX::RandNormal(location, scale);
		};
		const auto cauchyExplicit = [](auto& engine, auto location, auto scale)
		{
			return RandX::RandCauchy(engine, location, scale);
		};
		const auto cauchyDefault = [](auto location, auto scale)
		{
			return RandX::RandCauchy(location, scale);
		};
		const auto extremeValueExplicit = [](auto& engine, auto location, auto scale)
		{
			return RandX::RandExtremeValue(engine, location, scale);
		};
		const auto extremeValueDefault = [](auto location, auto scale)
		{
			return RandX::RandExtremeValue(location, scale);
		};
		const auto checkType = [&](auto type)
		{
			using T = decltype(type);
			RandXTest::AffineStabilityContracts::CheckExtremeScale<T>(
				normalFactory, normalExplicit, normalDefault);
			RandXTest::AffineStabilityContracts::CheckExtremeScale<T>(
				cauchyFactory, cauchyExplicit, cauchyDefault);
			RandXTest::AffineStabilityContracts::CheckExtremeScale<T>(
				extremeValueFactory, extremeValueExplicit, extremeValueDefault);
		};

		checkType(float{});
		checkType(double{});
		checkType(static_cast<long double>(0));
	}
}

#endif
