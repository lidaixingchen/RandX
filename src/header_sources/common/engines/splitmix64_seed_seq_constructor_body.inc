		{
			std::array<std::uint32_t, 2> seeds;
			seq.generate(seeds.data(), seeds.data() + seeds.size());
			m_state = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
		}
