	{
		if (!std::isfinite(a) || !std::isfinite(b) || a <= T{0} || b <= T{0})
			throw std::invalid_argument("RandBeta: invalid a or b");

		using WorkT = detail::BetaWorkType<T>;
		const WorkT wa = static_cast<WorkT>(a);
		const WorkT wb = static_cast<WorkT>(b);

		constexpr WorkT LargeShapeThreshold = WorkT{1000};
		constexpr WorkT SmallShapeThreshold = WorkT{1};
		if (wa >= LargeShapeThreshold || wb >= LargeShapeThreshold ||
		    wa < SmallShapeThreshold || wb < SmallShapeThreshold)
		{
			detail::DecomposedGammaLog<WorkT> sample_a, sample_b;
			detail::SampleGammaLogScale(engine, wa, sample_a);
			detail::SampleGammaLogScale(engine, wb, sample_b);
			const WorkT res = detail::ComputeBetaSampleFromLogScale(sample_a, sample_b);
			return static_cast<T>(res);
		}
		else
		{
			std::gamma_distribution<WorkT> distA(wa, WorkT{1});
			std::gamma_distribution<WorkT> distB(wb, WorkT{1});
			const WorkT x = distA(engine);
			const WorkT y = distB(engine);
			const WorkT res = detail::NormalizeBetaSample(x, y);
			return static_cast<T>(res);
		}
	}
