			constexpr void discard(unsigned long long z) noexcept
			{
				for (unsigned long long i = 0; i < z; ++i)
					static_cast<Derived*>(this)->operator()();
			}
