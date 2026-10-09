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
		inline void ValidatePoissonMean(double mean)
		{
			if (!std::isfinite(mean) || mean < 0.0)
				throw std::invalid_argument("RandPoisson: mean must be non-negative");

			constexpr int PoissonDoublePrecisionDigits = std::numeric_limits<double>::digits;
			if constexpr (std::numeric_limits<T>::digits <= PoissonDoublePrecisionDigits)
			{
				if (mean > static_cast<double>((std::numeric_limits<T>::max)()))
					throw std::invalid_argument("RandPoisson: mean exceeds maximum value of return type");
			}
			else
			{
				const double maximumExclusiveMean = std::ldexp(1.0, std::numeric_limits<T>::digits);
				if (mean >= maximumExclusiveMean)
					throw std::invalid_argument("RandPoisson: mean exceeds maximum value of return type");
			}
		}

		template <class T>
		struct IsGeometricDistributionType : std::bool_constant<
			std::is_integral_v<T> &&
			std::is_same_v<T, std::remove_cv_t<T>> &&
			!std::is_same_v<T, bool> &&
			(std::numeric_limits<T>::digits <= std::numeric_limits<std::uint64_t>::digits)>
		{
		};
