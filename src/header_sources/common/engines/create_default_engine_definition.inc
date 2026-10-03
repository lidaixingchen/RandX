	namespace detail
	{
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		inline Xoshiro256StarStar CreateDefaultEngine()
		{
			return Xoshiro256StarStar{ RandomSeed() };
		}
	}
