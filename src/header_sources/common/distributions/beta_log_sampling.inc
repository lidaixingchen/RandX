		template <class WorkT>
		struct DecomposedGammaLog
		{
			WorkT base_log{0};
			WorkT exp_val{0};
			WorkT shape{1};
			bool has_extra{false};
		};

		template <class WorkT, class Engine>
		inline void SampleGammaLogScale(Engine& engine, WorkT shape, DecomposedGammaLog<WorkT>& out)
		{
			WorkT effective_shape = shape;
			out.shape = shape;
			if (shape < WorkT{1})
			{
				effective_shape = shape + WorkT{1};
				std::exponential_distribution<WorkT> exp_dist(WorkT{1});
				out.exp_val = exp_dist(engine);
				out.has_extra = true;
			}
			else
			{
				out.exp_val = WorkT{0};
				out.has_extra = false;
			}

			const WorkT d = effective_shape - (WorkT{1} / WorkT{3});
			const WorkT c = (WorkT{1} / WorkT{3}) / std::sqrt(d);

			constexpr std::size_t MaxAttempts = 1000;
			std::normal_distribution<WorkT> norm_dist(WorkT{0}, WorkT{1});

			for (std::size_t attempt = 0; attempt < MaxAttempts; ++attempt)
			{
				const WorkT z = norm_dist(engine);
				const WorkT cz = c * z;
				if (cz <= WorkT{-1})
				{
					continue;
				}

				const WorkT log_v = WorkT{3} * std::log1p(cz);
				const WorkT u = RandCanonical<WorkT>(engine);
				if (u <= WorkT{0})
				{
					continue;
				}

				const WorkT z2 = z * z;
				const WorkT z4 = z2 * z2;
				if (u < WorkT{1} - WorkT{0.0331} * z4)
				{
					out.base_log = std::log(d) + log_v;
					return;
				}

				WorkT diff = WorkT{0};
				const WorkT abs_cz = std::abs(cz);
				if (abs_cz < WorkT{1e-2})
				{
					const WorkT x = cz;
					const WorkT x2 = x * x;
					const WorkT x4 = x2 * x2;
					const WorkT poly = -WorkT{0.75} + x * (WorkT{0.6} + x * (-WorkT{0.5} + x * (WorkT{3.0/7.0} - WorkT{0.375} * x)));
					diff = d * x4 * poly;
				}
				else
				{
					const WorkT x = cz;
					const WorkT x2 = x * x;
					const WorkT x3 = x2 * x;
					const WorkT log1p_minus_x = std::log1p(x) - x;
					diff = d * (WorkT{1.5} * x2 - x3 + WorkT{3} * log1p_minus_x);
				}

				if (std::log(u) < diff)
				{
					out.base_log = std::log(d) + log_v;
					return;
				}
			}

			throw std::runtime_error("RandBeta: Gamma rejection sampling failed to converge");
		}

		template <class WorkT>
		inline WorkT ComputeBetaSampleFromLogScale(const DecomposedGammaLog<WorkT>& a_sample, const DecomposedGammaLog<WorkT>& b_sample)
		{
			const WorkT base_diff = b_sample.base_log - a_sample.base_log;
			WorkT delta = WorkT{0};

			if (a_sample.has_extra && b_sample.has_extra)
			{
				const WorkT sa = a_sample.shape;
				const WorkT sb = b_sample.shape;
				if (sa == sb)
				{
					const WorkT diff = a_sample.exp_val - b_sample.exp_val;
					delta = diff / sa;
				}
				else if (sa < sb)
				{
					const WorkT r = sa / sb;
					const WorkT diff = a_sample.exp_val - r * b_sample.exp_val;
					delta = diff / sa;
				}
				else
				{
					const WorkT r = sb / sa;
					const WorkT diff = r * a_sample.exp_val - b_sample.exp_val;
					delta = diff / sb;
				}
			}
			else if (a_sample.has_extra && !b_sample.has_extra)
			{
				delta = a_sample.exp_val / a_sample.shape;
			}
			else if (!a_sample.has_extra && b_sample.has_extra)
			{
				delta = -b_sample.exp_val / b_sample.shape;
			}

			const WorkT L = base_diff + delta;

			if (std::isnan(L))
			{
				throw std::runtime_error("RandBeta: NaN encountered during Beta calculation");
			}

			if (std::abs(L) < WorkT{0.5})
			{
				const WorkT em1 = std::expm1(L);
				return WorkT{0.5} - em1 / (WorkT{2} * (WorkT{2} + em1));
			}

			if (L >= WorkT{0.5})
			{
				const WorkT exp_neg_L = std::exp(-L);
				return exp_neg_L / (WorkT{1} + exp_neg_L);
			}

			const WorkT exp_L = std::exp(L);
			return WorkT{1} / (WorkT{1} + exp_L);
		}

		template <class WorkT>
		inline WorkT ComputeBetaSampleFromLogScale(WorkT d_a, WorkT e_a, WorkT d_b, WorkT e_b)
		{
			DecomposedGammaLog<WorkT> sample_a, sample_b;
			sample_a.base_log = std::log(d_a) + e_a;
			sample_b.base_log = std::log(d_b) + e_b;
			return ComputeBetaSampleFromLogScale(sample_a, sample_b);
		}
