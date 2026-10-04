	{
		detail::TriangularKernel<T> kernel(min, peak, max);
		if (kernel.IsDegenerate())
		{
			return kernel.DegenerateValue();
		}
		return kernel.Generate(DefaultEngine());
	}
