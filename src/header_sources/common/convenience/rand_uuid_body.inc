	{
		static constexpr char hex[] = "0123456789abcdef";
		std::string uuid(36, '-');
		const std::uint64_t u1 = detail::Generate64Bits(engine);
		const std::uint64_t u2 = detail::Generate64Bits(engine);

		for (int i = 0; i < 8; ++i)
			uuid[i] = hex[(u1 >> (i * 4)) & 0xFU];
		for (int i = 0; i < 4; ++i)
			uuid[9 + i] = hex[(u1 >> ((8 + i) * 4)) & 0xFU];
		uuid[14] = '4';
		for (int i = 1; i < 4; ++i)
			uuid[14 + i] = hex[(u1 >> ((12 + i) * 4)) & 0xFU];
		uuid[19] = hex[8 + ((u2 >> 0) & 0x3U)];
		for (int i = 1; i < 4; ++i)
			uuid[19 + i] = hex[(u2 >> (i * 4)) & 0xFU];
		for (int i = 0; i < 12; ++i)
			uuid[24 + i] = hex[(u2 >> ((4 + i) * 4)) & 0xFU];

		return uuid;
	}
