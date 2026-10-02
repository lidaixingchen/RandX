	{
		detail::RealIntervalKernel<T> kernel(min, max, "RandReal");
		if (kernel.IsDegenerate())
		{
			return kernel.DegenerateValue();
		}
		if (min == T{0} && max == T{1})
		{
			return RandCanonical<T>(engine);
		}
		return kernel.Generate(engine);
	}
