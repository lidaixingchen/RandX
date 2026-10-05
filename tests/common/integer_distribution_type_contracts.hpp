#ifndef RANDX_TESTS_COMMON_INTEGER_DISTRIBUTION_TYPE_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_INTEGER_DISTRIBUTION_TYPE_CONTRACTS_HPP

#include <cstdint>
#include <limits>
#include <random>
#include <type_traits>
#include <utility>
#include "doctest.h"

namespace RandXTest
{
	namespace IntegerDistributionTypeContracts
	{
		template <class T, class = void>
		struct HasDefaultPoissonCall : std::false_type
		{
		};

		template <class T>
		struct HasDefaultPoissonCall<T, std::void_t<decltype(RandX::RandPoisson<T>())>> : std::true_type
		{
		};

		template <class T, class Engine, class = void>
		struct HasEnginePoissonCall : std::false_type
		{
		};

		template <class T, class Engine>
		struct HasEnginePoissonCall<T, Engine,
			std::void_t<decltype(RandX::RandPoisson<Engine, T>(std::declval<Engine&>()))>> : std::true_type
		{
		};

		template <class T, class = void>
		struct HasDefaultBinomialCall : std::false_type
		{
		};

		template <class T>
		struct HasDefaultBinomialCall<T, std::void_t<decltype(RandX::RandBinomial<T>())>> : std::true_type
		{
		};

		template <class T, class Engine, class = void>
		struct HasEngineBinomialCall : std::false_type
		{
		};

		template <class T, class Engine>
		struct HasEngineBinomialCall<T, Engine,
			std::void_t<decltype(RandX::RandBinomial<Engine, T>(std::declval<Engine&>()))>> : std::true_type
		{
		};

		template <class T, class = void>
		struct HasDefaultGeometricCall : std::false_type
		{
		};

		template <class T>
		struct HasDefaultGeometricCall<T, std::void_t<decltype(RandX::RandGeometric<T>())>> : std::true_type
		{
		};

		template <class T, class Engine, class = void>
		struct HasEngineGeometricCall : std::false_type
		{
		};

		template <class T, class Engine>
		struct HasEngineGeometricCall<T, Engine,
			std::void_t<decltype(RandX::RandGeometric<Engine, T>(std::declval<Engine&>()))>> : std::true_type
		{
		};

		template <class T>
		inline constexpr bool HasStandardIntegerDistributionCalls =
			HasDefaultPoissonCall<T>::value && HasEnginePoissonCall<T, RandX::Xoshiro256StarStar>::value &&
			HasDefaultBinomialCall<T>::value && HasEngineBinomialCall<T, RandX::Xoshiro256StarStar>::value;

		template <class T>
		inline constexpr bool HasGeometricDistributionCalls =
			HasDefaultGeometricCall<T>::value && HasEngineGeometricCall<T, RandX::Xoshiro256StarStar>::value;

		inline constexpr double PoissonMean = 4.5;
		inline constexpr unsigned int BinomialTrialCount = 12;
		inline constexpr double BinomialSuccessProbability = 0.375;
		inline constexpr double GeometricSuccessProbability = 0.5;

		template <class T>
		void CheckPoissonOutputAndState()
		{
			using Engine = RandX::Xoshiro256StarStar;
			const auto seed = RandXTest::TestConstants::kDefaultEngineTestSeed;
			Engine actualEngine{ seed };
			Engine expectedEngine{ seed };
			std::poisson_distribution<T> distribution{ PoissonMean };
			const T expected = distribution(expectedEngine);
			const T actual = RandX::RandPoisson<Engine, T>(actualEngine, PoissonMean);
			CHECK(actual == expected);
			CHECK(actualEngine == expectedEngine);

			RandX::Reseed(seed);
			Engine expectedDefaultEngine{ seed };
			const T expectedDefault = distribution(expectedDefaultEngine);
			const T actualDefault = RandX::RandPoisson<T>(PoissonMean);
			CHECK(actualDefault == expectedDefault);
			CHECK(RandX::DefaultEngine() == expectedDefaultEngine);
		}

		template <class T>
		void CheckBinomialOutputAndState()
		{
			using Engine = RandX::Xoshiro256StarStar;
			const auto seed = RandXTest::TestConstants::kDefaultEngineTestSeed;
			const T trialCount = static_cast<T>(BinomialTrialCount);
			Engine actualEngine{ seed };
			Engine expectedEngine{ seed };
			std::binomial_distribution<T> distribution{ trialCount, BinomialSuccessProbability };
			const T expected = distribution(expectedEngine);
			const T actual = RandX::RandBinomial<Engine, T>(actualEngine, trialCount, BinomialSuccessProbability);
			CHECK(actual == expected);
			CHECK(actualEngine == expectedEngine);

			RandX::Reseed(seed);
			Engine expectedDefaultEngine{ seed };
			const T expectedDefault = distribution(expectedDefaultEngine);
			const T actualDefault = RandX::RandBinomial<T>(trialCount, BinomialSuccessProbability);
			CHECK(actualDefault == expectedDefault);
			CHECK(RandX::DefaultEngine() == expectedDefaultEngine);
		}

		template <class T>
		void CheckGeometricOutputAndState()
		{
			using Engine = RandX::Xoshiro256StarStar;
			const auto seed = RandXTest::TestConstants::kDefaultEngineTestSeed;
			Engine actualEngine{ seed };
			Engine expectedEngine{ seed };
			const std::uint64_t expected = RandX::RandGeometric<Engine, std::uint64_t>(
				expectedEngine, GeometricSuccessProbability);
			if (expected <= static_cast<std::uint64_t>((std::numeric_limits<T>::max)()))
			{
				const T actual = RandX::RandGeometric<Engine, T>(actualEngine, GeometricSuccessProbability);
				CHECK(actual == static_cast<T>(expected));
			}
			else
			{
				CHECK_THROWS_AS((void)(RandX::RandGeometric<Engine, T>(actualEngine, GeometricSuccessProbability)),
					std::overflow_error);
			}
			CHECK(actualEngine == expectedEngine);

			RandX::Reseed(seed);
			Engine expectedDefaultEngine{ seed };
			const std::uint64_t expectedDefault = RandX::RandGeometric<Engine, std::uint64_t>(
				expectedDefaultEngine, GeometricSuccessProbability);
			if (expectedDefault <= static_cast<std::uint64_t>((std::numeric_limits<T>::max)()))
			{
				const T actualDefault = RandX::RandGeometric<T>(GeometricSuccessProbability);
				CHECK(actualDefault == static_cast<T>(expectedDefault));
			}
			else
			{
				CHECK_THROWS_AS((void)RandX::RandGeometric<T>(GeometricSuccessProbability), std::overflow_error);
			}
			CHECK(RandX::DefaultEngine() == expectedDefaultEngine);
		}
	}
}

TEST_SUITE("公共/基础/分布")
{
	TEST_CASE("Poisson 和 Binomial 使用八种无 cv 标准整数结果类型")
	{
		using namespace RandXTest::IntegerDistributionTypeContracts;
		using Engine = RandX::Xoshiro256StarStar;

		static_assert(HasStandardIntegerDistributionCalls<short>);
		static_assert(HasStandardIntegerDistributionCalls<unsigned short>);
		static_assert(HasStandardIntegerDistributionCalls<int>);
		static_assert(HasStandardIntegerDistributionCalls<unsigned int>);
		static_assert(HasStandardIntegerDistributionCalls<long>);
		static_assert(HasStandardIntegerDistributionCalls<unsigned long>);
		static_assert(HasStandardIntegerDistributionCalls<long long>);
		static_assert(HasStandardIntegerDistributionCalls<unsigned long long>);

		static_assert(!HasStandardIntegerDistributionCalls<char>);
		static_assert(!HasStandardIntegerDistributionCalls<signed char>);
		static_assert(!HasStandardIntegerDistributionCalls<unsigned char>);
		static_assert(!HasStandardIntegerDistributionCalls<wchar_t>);
		static_assert(!HasStandardIntegerDistributionCalls<char16_t>);
		static_assert(!HasStandardIntegerDistributionCalls<char32_t>);
		static_assert(!HasStandardIntegerDistributionCalls<bool>);
		static_assert(!HasStandardIntegerDistributionCalls<const int>);
		static_assert(!HasStandardIntegerDistributionCalls<volatile unsigned long>);
		enum class IntegerDistributionEnum { Value };
		static_assert(!HasStandardIntegerDistributionCalls<IntegerDistributionEnum>);
		static_assert(!HasStandardIntegerDistributionCalls<float>);
		static_assert(!HasStandardIntegerDistributionCalls<double>);
#if defined(__cpp_char8_t)
		static_assert(!HasStandardIntegerDistributionCalls<char8_t>);
#endif

		CheckPoissonOutputAndState<short>();
		CheckPoissonOutputAndState<unsigned short>();
		CheckPoissonOutputAndState<int>();
		CheckPoissonOutputAndState<unsigned int>();
		CheckPoissonOutputAndState<long>();
		CheckPoissonOutputAndState<unsigned long>();
		CheckPoissonOutputAndState<long long>();
		CheckPoissonOutputAndState<unsigned long long>();
		CheckBinomialOutputAndState<short>();
		CheckBinomialOutputAndState<unsigned short>();
		CheckBinomialOutputAndState<int>();
		CheckBinomialOutputAndState<unsigned int>();
		CheckBinomialOutputAndState<long>();
		CheckBinomialOutputAndState<unsigned long>();
		CheckBinomialOutputAndState<long long>();
		CheckBinomialOutputAndState<unsigned long long>();
	}

	TEST_CASE("Geometric 使用 64 位内无 cv 整数并保留字符类型")
	{
		using namespace RandXTest::IntegerDistributionTypeContracts;

		static_assert(HasGeometricDistributionCalls<char> ==
			(std::numeric_limits<char>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(HasGeometricDistributionCalls<signed char> ==
			(std::numeric_limits<signed char>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(HasGeometricDistributionCalls<unsigned char> ==
			(std::numeric_limits<unsigned char>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(HasGeometricDistributionCalls<wchar_t> ==
			(std::numeric_limits<wchar_t>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(HasGeometricDistributionCalls<char16_t> ==
			(std::numeric_limits<char16_t>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(HasGeometricDistributionCalls<char32_t> ==
			(std::numeric_limits<char32_t>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(HasGeometricDistributionCalls<short>);
		static_assert(HasGeometricDistributionCalls<unsigned short>);
		static_assert(HasGeometricDistributionCalls<int>);
		static_assert(HasGeometricDistributionCalls<unsigned int>);
		static_assert(HasGeometricDistributionCalls<long>);
		static_assert(HasGeometricDistributionCalls<unsigned long>);
		static_assert(HasGeometricDistributionCalls<long long>);
		static_assert(HasGeometricDistributionCalls<unsigned long long>);
		static_assert(!HasGeometricDistributionCalls<bool>);
		static_assert(!HasGeometricDistributionCalls<const int>);
		static_assert(!HasGeometricDistributionCalls<volatile char>);
		enum class IntegerDistributionEnum { Value };
		static_assert(!HasGeometricDistributionCalls<IntegerDistributionEnum>);
		static_assert(!HasGeometricDistributionCalls<float>);
		static_assert(!HasGeometricDistributionCalls<double>);
#if defined(__cpp_char8_t)
		static_assert(HasGeometricDistributionCalls<char8_t>);
#endif

		if constexpr (HasGeometricDistributionCalls<char>) CheckGeometricOutputAndState<char>();
		if constexpr (HasGeometricDistributionCalls<signed char>) CheckGeometricOutputAndState<signed char>();
		if constexpr (HasGeometricDistributionCalls<unsigned char>) CheckGeometricOutputAndState<unsigned char>();
		if constexpr (HasGeometricDistributionCalls<wchar_t>) CheckGeometricOutputAndState<wchar_t>();
		if constexpr (HasGeometricDistributionCalls<char16_t>) CheckGeometricOutputAndState<char16_t>();
		if constexpr (HasGeometricDistributionCalls<char32_t>) CheckGeometricOutputAndState<char32_t>();
		CheckGeometricOutputAndState<short>();
		CheckGeometricOutputAndState<unsigned short>();
		CheckGeometricOutputAndState<int>();
		CheckGeometricOutputAndState<unsigned int>();
		CheckGeometricOutputAndState<long>();
		CheckGeometricOutputAndState<unsigned long>();
		CheckGeometricOutputAndState<long long>();
		CheckGeometricOutputAndState<unsigned long long>();
#if defined(__cpp_char8_t)
		CheckGeometricOutputAndState<char8_t>();
#endif
	}
}

#endif
