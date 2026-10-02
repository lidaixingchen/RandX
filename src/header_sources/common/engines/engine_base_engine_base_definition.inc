			explicit constexpr EngineBase(const state_type& state) noexcept
				: s_(state)
			{
				if constexpr (!EngineStatePolicy<Derived>::AllowsZeroState)
				{
					if (IsAllZero(s_))
					{
						s_[0] = static_cast<ResultType>(1);
					}
					assert(!IsAllZero(s_) && "absorbing all-zero state");
				}
			}
