	{
		if (std::empty(c))
			throw std::invalid_argument("RandElement: empty container");
		return c[RandInt<std::size_t>(static_cast<std::size_t>(std::size(c) - 1))];
	}
