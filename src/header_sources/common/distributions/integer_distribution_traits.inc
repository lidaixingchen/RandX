		template <class T>
		struct IsStandardIntegerDistributionType : std::bool_constant<
			std::is_same_v<T, short> ||
			std::is_same_v<T, unsigned short> ||
			std::is_same_v<T, int> ||
			std::is_same_v<T, unsigned int> ||
			std::is_same_v<T, long> ||
			std::is_same_v<T, unsigned long> ||
			std::is_same_v<T, long long> ||
			std::is_same_v<T, unsigned long long>>
		{
		};

		template <class T>
		struct IsGeometricDistributionType : std::bool_constant<
			std::is_integral_v<T> &&
			std::is_same_v<T, std::remove_cv_t<T>> &&
			!std::is_same_v<T, bool> &&
			(std::numeric_limits<T>::digits <= std::numeric_limits<std::uint64_t>::digits)>
		{
		};
