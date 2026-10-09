	namespace detail
	{
		template <class T, class Diff, class It, class Sentinel, class GetEngine>
		inline T
		RandElementInput(It& first, Sentinel& last, GetEngine&& getEngine)
		{
			using Count = std::uint64_t;
			constexpr Count maxCount = (std::numeric_limits<Count>::max)();

			if (first == last)
				throw std::invalid_argument("RandElement: empty range");
			T selected = *first;
			++first;
			Count count = 1;
			while (first != last)
			{
				if (count == maxCount)
					throw std::length_error("RandElement: range exceeds count capacity");

				auto& engine = getEngine();
				Count index;
				if constexpr (std::numeric_limits<Diff>::digits <= std::numeric_limits<Count>::digits)
				{
					if (count <= static_cast<Count>((std::numeric_limits<Diff>::max)()))
					{
						const Diff upper = static_cast<Diff>(count);
						index = static_cast<Count>(RandInt<Diff>(engine, Diff{0}, upper));
					}
					else
					{
						index = RandInt<Count>(engine, Count{0}, count);
					}
				}
				else
				{
					const Diff upper = static_cast<Diff>(count);
					index = static_cast<Count>(RandInt<Diff>(engine, Diff{0}, upper));
				}
				if (index == 0)
					selected = *first;
				++first;
				++count;
			}
			if constexpr (std::is_constructible_v<T, T&&>)
				return selected;
			else
				return static_cast<const T&>(selected);
		}
	}
