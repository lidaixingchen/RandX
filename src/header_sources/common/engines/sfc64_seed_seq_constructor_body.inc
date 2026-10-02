		{
			std::array<std::uint32_t, 8> seeds;
			seq.generate(seeds.data(), seeds.data() + seeds.size());
			s_[0] = (static_cast<std::uint64_t>(seeds[0]) << 32) | seeds[1];
			s_[1] = (static_cast<std::uint64_t>(seeds[2]) << 32) | seeds[3];
			s_[2] = (static_cast<std::uint64_t>(seeds[4]) << 32) | seeds[5];
			s_[3] = 1;
			// 全零状态会导致输出可预测，强制修正
			if ((s_[0] | s_[1] | s_[2]) == 0) s_[0] = 0x9E3779B97F4A7C15ULL;
			// 与种子构造函数一致：12 轮预热
			for (int i = 0; i < 12; ++i) { operator()(); }
		}
