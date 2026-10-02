	{
		if (!std::isfinite(n) || n <= T{0})
			throw std::invalid_argument("RandStudentT: n must be positive");
		std::student_t_distribution<T> dist(n);
		return dist(DefaultEngine());
	}
