#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#if __cplusplus >= 202002L
#include "RandX.hpp"
#else
#include "RandX_Cpp17.hpp"
#endif

#if defined(__SIZEOF_INT128__)
namespace
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

	using Engine = RandX::Xoshiro256StarStar;
	using SignedExtendedInteger = __int128;
	using UnsignedExtendedInteger = unsigned __int128;

	static_assert(std::is_integral_v<SignedExtendedInteger>);
	static_assert(std::is_integral_v<UnsignedExtendedInteger>);
	static_assert(std::numeric_limits<SignedExtendedInteger>::digits >
		std::numeric_limits<std::uint64_t>::digits);
	static_assert(std::numeric_limits<UnsignedExtendedInteger>::digits >
		std::numeric_limits<std::uint64_t>::digits);
	static_assert(!RandX::detail::IsStandardIntegerDistributionType<SignedExtendedInteger>::value);
	static_assert(!RandX::detail::IsStandardIntegerDistributionType<UnsignedExtendedInteger>::value);
	static_assert(!RandX::detail::IsGeometricDistributionType<SignedExtendedInteger>::value);
	static_assert(!RandX::detail::IsGeometricDistributionType<UnsignedExtendedInteger>::value);

	static_assert(!HasDefaultPoissonCall<SignedExtendedInteger>::value);
	static_assert(!HasEnginePoissonCall<SignedExtendedInteger, Engine>::value);
	static_assert(!HasDefaultBinomialCall<SignedExtendedInteger>::value);
	static_assert(!HasEngineBinomialCall<SignedExtendedInteger, Engine>::value);
	static_assert(!HasDefaultGeometricCall<SignedExtendedInteger>::value);
	static_assert(!HasEngineGeometricCall<SignedExtendedInteger, Engine>::value);
	static_assert(!HasDefaultPoissonCall<UnsignedExtendedInteger>::value);
	static_assert(!HasEnginePoissonCall<UnsignedExtendedInteger, Engine>::value);
	static_assert(!HasDefaultBinomialCall<UnsignedExtendedInteger>::value);
	static_assert(!HasEngineBinomialCall<UnsignedExtendedInteger, Engine>::value);
	static_assert(!HasDefaultGeometricCall<UnsignedExtendedInteger>::value);
	static_assert(!HasEngineGeometricCall<UnsignedExtendedInteger, Engine>::value);
}
#endif

int main()
{
	return 0;
}
