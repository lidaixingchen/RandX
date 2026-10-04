	{
		detail::TriangularKernel<T> kernel(min, peak, max);
		return kernel.Generate(engine);
	}
