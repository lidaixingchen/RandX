		template <class WeightContainer>
		struct PreparedWeights
		{
			using Size = typename WeightContainer::size_type;
			std::vector<Size> active_indices;
			std::vector<double> normalized_weights;
		};

		template <class WeightContainer>
		[[nodiscard]]
		inline PreparedWeights<WeightContainer> PrepareWeights(const WeightContainer& weights)
		{
			if (weights.empty())
			{
				throw std::invalid_argument("RandWeighted: weights container cannot be empty");
			}

			using RawWeightType = typename WeightContainer::value_type;
			using WeightType = std::remove_cv_t<RawWeightType>;

			static_assert(std::is_arithmetic_v<WeightType> && !std::is_same_v<WeightType, bool>,
				"RandWeighted: weight element type must be a non-bool arithmetic type");

			using WorkType = std::conditional_t<
				(std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits ||
				 std::numeric_limits<long double>::max_exponent > std::numeric_limits<double>::max_exponent),
				long double, double>;

			WorkType maxWeight = 0;
			for (const auto& w : weights)
			{
				if constexpr (std::is_floating_point_v<WeightType>)
				{
					if (!std::isfinite(w) || w < 0)
					{
						throw std::invalid_argument("RandWeighted: weights must be finite and non-negative");
					}
				}
				else
				{
					if (w < 0)
					{
						throw std::invalid_argument("RandWeighted: weights must be non-negative");
					}
				}

				const auto val = static_cast<WorkType>(w);
				if (val > maxWeight)
				{
					maxWeight = val;
				}
			}

			if (maxWeight <= 0)
			{
				throw std::invalid_argument("RandWeighted: at least one weight must be strictly positive");
			}

			PreparedWeights<WeightContainer> result;
			typename WeightContainer::size_type currentIndex = 0;

			for (const auto& w : weights)
			{
				if (w > 0)
				{
					const WorkType scaled = static_cast<WorkType>(w) / maxWeight;
					const double doubleWeight = static_cast<double>(scaled);
					if (doubleWeight <= 0.0)
					{
						throw std::range_error("RandWeighted: positive weight underflowed to zero after normalization");
					}
					result.active_indices.push_back(currentIndex);
					result.normalized_weights.push_back(doubleWeight);
				}
				++currentIndex;
			}

			return result;
		}
