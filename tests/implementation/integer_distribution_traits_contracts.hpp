#ifndef RANDX_TESTS_IMPLEMENTATION_INTEGER_DISTRIBUTION_TRAITS_CONTRACTS_HPP
#define RANDX_TESTS_IMPLEMENTATION_INTEGER_DISTRIBUTION_TRAITS_CONTRACTS_HPP

TEST_SUITE("内部/分布类型策略")
{
	TEST_CASE("标准整数分布和几何分布 trait 遵守各自类型边界")
	{
		using RandX::detail::IsGeometricDistributionType;
		using RandX::detail::IsStandardIntegerDistributionType;

		static_assert(IsStandardIntegerDistributionType<short>::value);
		static_assert(IsStandardIntegerDistributionType<unsigned short>::value);
		static_assert(IsStandardIntegerDistributionType<int>::value);
		static_assert(IsStandardIntegerDistributionType<unsigned int>::value);
		static_assert(IsStandardIntegerDistributionType<long>::value);
		static_assert(IsStandardIntegerDistributionType<unsigned long>::value);
		static_assert(IsStandardIntegerDistributionType<long long>::value);
		static_assert(IsStandardIntegerDistributionType<unsigned long long>::value);
		static_assert(!IsStandardIntegerDistributionType<char>::value);
		static_assert(!IsStandardIntegerDistributionType<signed char>::value);
		static_assert(!IsStandardIntegerDistributionType<unsigned char>::value);
		static_assert(!IsStandardIntegerDistributionType<bool>::value);
		static_assert(!IsStandardIntegerDistributionType<const int>::value);
		static_assert(!IsStandardIntegerDistributionType<volatile long>::value);

		static_assert(IsGeometricDistributionType<char>::value ==
			(std::numeric_limits<char>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(IsGeometricDistributionType<signed char>::value ==
			(std::numeric_limits<signed char>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(IsGeometricDistributionType<unsigned char>::value ==
			(std::numeric_limits<unsigned char>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(IsGeometricDistributionType<wchar_t>::value ==
			(std::numeric_limits<wchar_t>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(IsGeometricDistributionType<char16_t>::value ==
			(std::numeric_limits<char16_t>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(IsGeometricDistributionType<char32_t>::value ==
			(std::numeric_limits<char32_t>::digits <= std::numeric_limits<std::uint64_t>::digits));
		static_assert(IsGeometricDistributionType<short>::value);
		static_assert(IsGeometricDistributionType<unsigned short>::value);
		static_assert(IsGeometricDistributionType<int>::value);
		static_assert(IsGeometricDistributionType<unsigned int>::value);
		static_assert(IsGeometricDistributionType<long>::value);
		static_assert(IsGeometricDistributionType<unsigned long>::value);
		static_assert(IsGeometricDistributionType<long long>::value);
		static_assert(IsGeometricDistributionType<unsigned long long>::value);
		static_assert(!IsGeometricDistributionType<bool>::value);
		static_assert(!IsGeometricDistributionType<const int>::value);
		static_assert(!IsGeometricDistributionType<volatile char>::value);
		enum class IntegerDistributionEnum { Value };
		static_assert(!IsStandardIntegerDistributionType<IntegerDistributionEnum>::value);
		static_assert(!IsStandardIntegerDistributionType<float>::value);
		static_assert(!IsGeometricDistributionType<IntegerDistributionEnum>::value);
		static_assert(!IsGeometricDistributionType<float>::value);
#if defined(__cpp_char8_t)
		static_assert(!IsStandardIntegerDistributionType<char8_t>::value);
		static_assert(IsGeometricDistributionType<char8_t>::value ==
			(std::numeric_limits<char8_t>::digits <= std::numeric_limits<std::uint64_t>::digits));
#endif

		CHECK(IsStandardIntegerDistributionType<short>::value);
		CHECK(IsGeometricDistributionType<std::uint64_t>::value);
	}
}

#endif
