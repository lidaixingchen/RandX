	inline constexpr void Xoroshiro128StarStar::jump() noexcept
	{
		constexpr std::uint64_t p[] = { 0xdf900294d8f554a5, 0x170865df4b3201fc };
		jumpPoly(p);
	}


	inline constexpr void Xoroshiro128StarStar::longJump() noexcept
	{
		constexpr std::uint64_t p[] = { 0xd2a98b26625eee7b, 0xdddf9b1090aa7ac1 };
		jumpPoly(p);
	}
