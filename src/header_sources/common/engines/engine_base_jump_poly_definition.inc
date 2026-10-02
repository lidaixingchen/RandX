			template <std::size_t K>
			constexpr void jumpPoly(const ResultType (&poly)[K]) noexcept
			{
				std::array<ResultType, N> acc{};
				for (std::size_t i = 0; i < K; ++i)
					for (int b = 0; b < Bits; ++b)
					{
						if (poly[i] & (ResultType{ 1 } << b))
							for (std::size_t j = 0; j < N; ++j)
								acc[j] ^= s_[j];
						static_cast<Derived*>(this)->operator()();
					}
				s_ = acc;
			}
