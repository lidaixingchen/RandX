	{
		detail::RealIntervalKernel<T> kernel(min, max, "RandVector");
		std::vector<T> v;
		v.reserve(n);
		if (kernel.IsDegenerate())
		{
			v.assign(n, kernel.DegenerateValue());
			return v;
		}
		for (std::size_t i = 0; i < n; ++i)
			v.push_back(kernel.Generate(engine));
		return v;
	}
