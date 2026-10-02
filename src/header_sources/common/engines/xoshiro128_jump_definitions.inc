	inline constexpr void Xoshiro128StarStar::jump() noexcept
	{
		constexpr std::uint32_t p[] = { 0x8764000bu, 0xf542d2d3u, 0x6fa035c3u, 0x77f2db5bu };
		jumpPoly(p);
	}


	inline constexpr void Xoshiro128StarStar::longJump() noexcept
	{
		constexpr std::uint32_t p[] = { 0xb523952eu, 0x0b6f099fu, 0xccf5a0efu, 0x1c580662u };
		jumpPoly(p);
	}
