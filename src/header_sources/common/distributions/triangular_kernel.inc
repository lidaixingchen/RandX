	template <class T>
	class TriangularKernel
	{
	public:
		using WorkType = std::conditional_t<std::is_same_v<T, float>, double, T>;

		TriangularKernel(T min, T peak, T max)
			: m_min(min), m_peak(peak), m_max(max), m_isDegenerate(min == peak && peak == max)
		{
			if (!std::isfinite(min) || !std::isfinite(peak) || !std::isfinite(max)
				|| min > peak || peak > max)
			{
				throw std::invalid_argument("RandTriangular: invalid min, peak, or max");
			}
			if (m_isDegenerate)
			{
				return;
			}

			if (min < T{0} && max > T{0}
				&& max > (std::numeric_limits<T>::max)() + min)
			{
				throw std::invalid_argument("RandTriangular: range width overflow");
			}
			const T returnWidth = max - min;
			if (!std::isfinite(returnWidth))
			{
				throw std::invalid_argument("RandTriangular: range width overflow");
			}

			m_minWork = static_cast<WorkType>(min);
			m_peakWork = static_cast<WorkType>(peak);
			m_maxWork = static_cast<WorkType>(max);
			m_width = m_maxWork - m_minWork;
			m_leftWidth = m_peakWork - m_minWork;
			m_rightWidth = m_maxWork - m_peakWork;
			m_leftRatio = m_leftWidth / m_width;
			m_rightRatio = m_rightWidth / m_width;
			m_leftAnchorSwitch = m_leftRatio * Quarter;
			m_rightAnchorSwitch = WorkType{1} - m_rightRatio * Quarter;
			m_leftAnchorSwitchValue = static_cast<T>(m_minWork + m_leftWidth * Half);
			m_rightAnchorSwitchValue = static_cast<T>(m_maxWork - m_rightWidth * Half);
			m_upperCorrected = std::nextafter(max, min);
		}

		[[nodiscard]]
		bool IsDegenerate() const noexcept
		{
			return m_isDegenerate;
		}

		[[nodiscard]]
		T DegenerateValue() const noexcept
		{
			return m_min;
		}

		[[nodiscard]]
		T Quantile(WorkType uniform) const
		{
			if (!std::isfinite(uniform) || uniform < WorkType{0} || uniform >= WorkType{1})
			{
				throw std::invalid_argument("RandTriangular: uniform input is outside [0, 1)");
			}
			if (m_isDegenerate)
			{
				return m_min;
			}
			if (uniform == WorkType{0})
			{
				return m_min;
			}
			if (uniform == m_leftRatio)
			{
				return m_peak;
			}

			WorkType sample{};
			if (uniform < m_leftRatio)
			{
				if (uniform < m_leftAnchorSwitch)
				{
					const WorkType distanceFromMin =
						m_leftWidth * std::sqrt(uniform / m_leftRatio);
					sample = m_minWork + distanceFromMin;
				}
				else
				{
					const WorkType modeGap = m_leftRatio - uniform;
					const WorkType modeDenominator =
						WorkType{1} + std::sqrt(uniform / m_leftRatio);
					const WorkType distanceFromPeak = (m_width * modeGap) / modeDenominator;
					sample = m_peakWork - distanceFromPeak;
				}
			}
			else
			{
				if (uniform < m_rightAnchorSwitch)
				{
					const WorkType modeGap = uniform - m_leftRatio;
					const WorkType modeDenominator =
						WorkType{1} + std::sqrt((WorkType{1} - uniform) / m_rightRatio);
					const WorkType distanceFromPeak = (m_width * modeGap) / modeDenominator;
					sample = m_peakWork + distanceFromPeak;
				}
				else
				{
					const WorkType distanceFromMax =
						m_rightWidth * std::sqrt((WorkType{1} - uniform) / m_rightRatio);
					sample = m_maxWork - distanceFromMax;
				}
			}

			if (!std::isfinite(sample) || sample < m_minWork || sample > m_maxWork)
			{
				throw std::runtime_error("TriangularKernel: numerical failure in quantile conversion");
			}
			T result = static_cast<T>(sample);
			if (!std::isfinite(result))
			{
				throw std::runtime_error("TriangularKernel: non-finite result");
			}
			if (result < m_min)
			{
				result = m_min;
			}
			if (uniform < m_leftRatio)
			{
				if (uniform < m_leftAnchorSwitch && result > m_leftAnchorSwitchValue)
				{
					result = m_leftAnchorSwitchValue;
				}
				else if (uniform >= m_leftAnchorSwitch && result < m_leftAnchorSwitchValue)
				{
					result = m_leftAnchorSwitchValue;
				}
			}
			else if (uniform < m_rightAnchorSwitch && result > m_rightAnchorSwitchValue)
			{
				result = m_rightAnchorSwitchValue;
			}
			else if (uniform >= m_rightAnchorSwitch && result < m_rightAnchorSwitchValue)
			{
				result = m_rightAnchorSwitchValue;
			}
			if (result >= m_max)
			{
				result = m_upperCorrected;
			}
			return result;
		}

		template <class Engine>
		[[nodiscard]]
		T Generate(Engine& engine) const
		{
			if (m_isDegenerate)
			{
				return m_min;
			}
			return Quantile(static_cast<WorkType>(RandCanonical<T>(engine)));
		}

	private:
		static constexpr WorkType Half = WorkType{1} / WorkType{2};
		static constexpr WorkType Quarter = WorkType{1} / WorkType{4};

		T m_min;
		T m_peak;
		T m_max;
		T m_upperCorrected{0};
		bool m_isDegenerate;
		WorkType m_minWork{0};
		WorkType m_peakWork{0};
		WorkType m_maxWork{0};
		WorkType m_width{0};
		WorkType m_leftWidth{0};
		WorkType m_rightWidth{0};
		WorkType m_leftRatio{0};
		WorkType m_rightRatio{0};
		WorkType m_leftAnchorSwitch{0};
		WorkType m_rightAnchorSwitch{1};
		T m_leftAnchorSwitchValue{0};
		T m_rightAnchorSwitchValue{0};
	};
