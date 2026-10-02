	{
		detail::RealIntervalKernel<T> kernel(min, max, "RandFill");
		if (kernel.IsDegenerate())
		{
			const T val = kernel.DegenerateValue();
			for (; first != last; ++first)
				*first = val;
			return;
		}
		for (; first != last; ++first)
			*first = kernel.Generate(engine);
	}
