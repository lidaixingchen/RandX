		{
			if (!std::isfinite(x) || !std::isfinite(y) || x < T{0} || y < T{0})
				throw std::domain_error("RandBeta: Gamma sample is not finite or negative");
			if (x == T{0} && y == T{0})
				throw std::domain_error("RandBeta: both Gamma samples are zero");
			if (x == T{0}) return T{0};
			if (y == T{0}) return T{1};
			const T sum = x + y;
			if (std::isfinite(sum))
			{
				return x / sum;
			}
			if (x >= y && x > T{0})
			{
				return T{1} / (T{1} + (y / x));
			}
			else if (x < y && y > T{0})
			{
				const T r = x / y;
				return r / (T{1} + r);
			}
			throw std::domain_error("RandBeta: unable to normalize Gamma samples");
		}
