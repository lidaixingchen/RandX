	inline constexpr void Xoshiro256StarStar::jump() noexcept
	{
		constexpr std::uint64_t p[] = {
			0x180ec6d33cfd0aba, 0xd5a61266f0c9392c,
			0xa9582618e03fc9aa, 0x39abdc4529b1661c };
		jumpPoly(p);
	}


	inline constexpr void Xoshiro256StarStar::longJump() noexcept
	{
		constexpr std::uint64_t p[] = {
			0x76e15d3efefdcbbf, 0xc5004e441c522fb3,
			0x77710069854ee241, 0x39109bb02acbe635 };
		jumpPoly(p);
	}
