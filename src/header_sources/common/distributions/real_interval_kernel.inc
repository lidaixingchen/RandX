		template <class T>
		class RealIntervalKernel
		{
		public:
			RealIntervalKernel(T min, T max, const char* funcName = "RandReal")
				: m_min(min), m_max(max), m_isDegenerate(min == max)
			{
				if (!std::isfinite(min) || !std::isfinite(max) || min > max)
				{
					throw std::invalid_argument(std::string(funcName) + ": invalid min or max");
				}
				if (m_isDegenerate)
				{
					return;
				}
				if (min < 0 && max > 0 && (max > std::numeric_limits<T>::max() + min))
				{
					throw std::invalid_argument(std::string(funcName) + ": range width overflow");
				}
				const T width = max - min;
				if (!std::isfinite(width))
				{
					throw std::invalid_argument(std::string(funcName) + ": range width overflow");
				}
				m_upperCorrected = std::nextafter(max, min);
				m_dist.param(typename std::uniform_real_distribution<T>::param_type(min, max));
			}

			[[nodiscard]]
			bool IsDegenerate() const noexcept { return m_isDegenerate; }

			[[nodiscard]]
			T DegenerateValue() const noexcept { return m_min; }

			template <class Engine>
			[[nodiscard]]
			T Generate(Engine& engine)
			{
				if (m_isDegenerate)
				{
					return m_min;
				}
				T val = m_dist(engine);
				if (!std::isfinite(val) || val < m_min || val > m_max)
				{
					throw std::runtime_error("RealIntervalKernel: numerical failure in distribution generation");
				}
				if (val >= m_max)
				{
					val = m_upperCorrected;
				}
				return val;
			}

		private:
			T m_min;
			T m_max;
			T m_upperCorrected{0};
			bool m_isDegenerate;
			std::uniform_real_distribution<T> m_dist;
		};
