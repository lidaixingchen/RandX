			{
				if constexpr (sizeof(result_type) == 8)
				{
					std::array<std::uint32_t, N * 2> raw;
					seq.generate(raw.data(), raw.data() + raw.size());
					for (std::size_t i = 0; i < N; ++i)
						s_[i] = (static_cast<result_type>(raw[2 * i]) << 32) | raw[2 * i + 1];
				}
				else
				{
					std::array<std::uint32_t, N> raw;
					seq.generate(raw.data(), raw.data() + raw.size());
					for (std::size_t i = 0; i < N; ++i)
						s_[i] = static_cast<result_type>(raw[i]);
				}
				if (IsAllZero(s_)) s_[0] = 1;
			}
