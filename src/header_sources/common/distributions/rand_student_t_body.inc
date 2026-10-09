	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandStudentT: n must be positive");
		if (detail::RequiresStableStudentT(n))
			return detail::SampleStableStudentT(DefaultEngine(), n);
		std::student_t_distribution<T> dist(n);
		return dist(DefaultEngine());
	}
