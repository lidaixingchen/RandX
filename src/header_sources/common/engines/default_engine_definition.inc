	[[nodiscard]]
	inline Xoshiro256StarStar& DefaultEngine()
	{
		thread_local Xoshiro256StarStar engine = detail::CreateDefaultEngine();
		return engine;
	}
