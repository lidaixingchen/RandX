		template <class T>
		using StableDistributionWorkType = std::conditional_t<std::is_same_v<T, float>, double, T>;

		template <class WorkT>
		struct NormalizedGammaLogSample
		{
			WorkT base_log{0};
			WorkT exponential{0};
			WorkT degrees{1};
		};

		template <class WorkT>
		struct DecomposedGammaLog;

		template <class WorkT, class Engine>
		inline void SampleGammaLogScale(Engine& engine, WorkT shape, DecomposedGammaLog<WorkT>& out);

		template <class WorkT, class Engine>
		inline NormalizedGammaLogSample<WorkT> SampleNormalizedGammaLog(Engine& engine, WorkT degrees)
		{
			const WorkT shape = degrees / WorkT{2};
			const WorkT log_shape = shape > WorkT{0} ? std::log(shape) : std::log(degrees) - std::log(WorkT{2});
			DecomposedGammaLog<WorkT> gamma_sample;
			if (shape > WorkT{0})
			{
				SampleGammaLogScale(engine, shape, gamma_sample);
			}
			else
			{
				// 最小次正规自由度的一半不可表示；提升后的 Gamma 形状舍入为 1，原形状保留在对数域。
				std::exponential_distribution<WorkT> exponential(WorkT{1});
				const WorkT correction = exponential(engine);
				SampleGammaLogScale(engine, WorkT{1}, gamma_sample);
				gamma_sample.exp_val = correction;
				gamma_sample.has_extra = true;
			}

			NormalizedGammaLogSample<WorkT> sample;
			sample.base_log = gamma_sample.base_log - log_shape;
			sample.exponential = gamma_sample.has_extra ? gamma_sample.exp_val : WorkT{0};
			sample.degrees = degrees;
			return sample;
		}

		template <class WorkT>
		inline WorkT ComputeNormalizedGammaLogRatio(
			const NormalizedGammaLogSample<WorkT>& numerator,
			const NormalizedGammaLogSample<WorkT>& denominator)
		{
			const WorkT base_diff = numerator.base_log - denominator.base_log;
			// 先在共同尺度上相减，再除以较小自由度，保留相近补偿项的差并避免无穷相减。
			WorkT correction;
			if (numerator.degrees <= denominator.degrees)
			{
				const WorkT ratio = numerator.degrees / denominator.degrees;
				correction = (ratio * denominator.exponential - numerator.exponential) / numerator.degrees;
			}
			else
			{
				const WorkT ratio = denominator.degrees / numerator.degrees;
				correction = (denominator.exponential - ratio * numerator.exponential) / denominator.degrees;
			}
			return base_diff + WorkT{2} * correction;
		}

		template <class WorkT>
		inline WorkT ComputeNormalizedGammaLogValue(const NormalizedGammaLogSample<WorkT>& sample)
		{
			return sample.base_log - WorkT{2} * (sample.exponential / sample.degrees);
		}

		template <class T, class WorkT>
		inline T ConvertSignedLogMagnitude(WorkT log_magnitude, WorkT sign_source)
		{
			const T maximum = (std::numeric_limits<T>::max)();
			const WorkT log_maximum = std::log(static_cast<WorkT>(maximum));
			if (log_magnitude > log_maximum)
				return std::copysign((std::numeric_limits<T>::infinity)(), static_cast<T>(sign_source));
			if (log_magnitude == -(std::numeric_limits<WorkT>::infinity)())
				return std::copysign(T{0}, static_cast<T>(sign_source));
			const WorkT magnitude = std::exp(log_magnitude);
			if (magnitude > static_cast<WorkT>(maximum))
				return std::copysign((std::numeric_limits<T>::infinity)(), static_cast<T>(sign_source));
			return std::copysign(static_cast<T>(magnitude), static_cast<T>(sign_source));
		}

		template <class T, class WorkT>
		inline T ExpToPositiveSample(WorkT log_sample)
		{
			const T maximum = (std::numeric_limits<T>::max)();
			const WorkT log_maximum = std::log(static_cast<WorkT>(maximum));
			if (log_sample > log_maximum)
				return (std::numeric_limits<T>::infinity)();

			const WorkT sample = std::exp(log_sample);
			if (sample > static_cast<WorkT>(maximum))
				return (std::numeric_limits<T>::infinity)();
			return static_cast<T>(sample);
		}

		template <class T>
		inline bool RequiresStableStudentT(T degrees)
		{
			using WorkT = StableDistributionWorkType<T>;
			// 大自由度下的指数增量会丢失有效位；小形状 Gamma 的幂变换则可能下溢。
			const WorkT precision_boundary = WorkT{1} / std::sqrt(static_cast<WorkT>(std::numeric_limits<T>::epsilon()));
			const T denorm_min = std::numeric_limits<T>::denorm_min();
			const T smallest_positive = denorm_min > T{0} ? denorm_min : std::numeric_limits<T>::min();
			const WorkT gamma_underflow_boundary = WorkT{2} /
				(-std::log(static_cast<WorkT>(smallest_positive)));
			const WorkT work_degrees = static_cast<WorkT>(degrees);
			return work_degrees <= gamma_underflow_boundary || work_degrees >= precision_boundary;
		}

		template <class T, class Engine>
		inline T SampleStableStudentT(Engine& engine, T degrees)
		{
			using WorkT = StableDistributionWorkType<T>;
			const WorkT work_degrees = static_cast<WorkT>(degrees);
			const NormalizedGammaLogSample<WorkT> gamma = SampleNormalizedGammaLog(engine, work_degrees);
			std::normal_distribution<WorkT> normal(WorkT{0}, WorkT{1});
			const WorkT normal_sample = normal(engine);
			if (normal_sample == WorkT{0})
				return T{0};
			const WorkT log_magnitude = std::log(std::abs(normal_sample)) -
				ComputeNormalizedGammaLogValue(gamma) / WorkT{2};
			return ConvertSignedLogMagnitude<T>(log_magnitude, normal_sample);
		}

		template <class T>
		inline bool RequiresStableFisherF(T m, T n)
		{
			using WorkT = StableDistributionWorkType<T>;
			const WorkT work_m = static_cast<WorkT>(m);
			const WorkT work_n = static_cast<WorkT>(n);
			const WorkT maximum = static_cast<WorkT>((std::numeric_limits<T>::max)());
			const WorkT large_degree_boundary = std::sqrt(maximum);
			const bool degree_scale_risk = work_m >= large_degree_boundary || work_n >= large_degree_boundary;
			const T denorm_min = std::numeric_limits<T>::denorm_min();
			const T smallest_positive = denorm_min > T{0} ? denorm_min : std::numeric_limits<T>::min();
			const WorkT gamma_underflow_boundary = WorkT{2} / (-std::log(static_cast<WorkT>(smallest_positive)));
			return degree_scale_risk || work_m <= gamma_underflow_boundary || work_n <= gamma_underflow_boundary;
		}

		template <class T, class Engine>
		inline T SampleStableFisherF(Engine& engine, T m, T n)
		{
			using WorkT = StableDistributionWorkType<T>;
			const NormalizedGammaLogSample<WorkT> numerator = SampleNormalizedGammaLog(engine, static_cast<WorkT>(m));
			const NormalizedGammaLogSample<WorkT> denominator = SampleNormalizedGammaLog(engine, static_cast<WorkT>(n));
			const WorkT log_ratio = ComputeNormalizedGammaLogRatio(numerator, denominator);
			return ExpToPositiveSample<T>(log_ratio);
		}
