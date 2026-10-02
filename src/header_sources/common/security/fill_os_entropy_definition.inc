		template <class Reader>
		[[nodiscard]]
		inline bool FillOsEntropy(Reader& reader, void* buffer, std::size_t length) noexcept
		{
			if (length == 0) return true;

			auto* destination = static_cast<std::uint8_t*>(buffer);
			std::size_t filled = 0;
			while (filled < length)
			{
				const std::size_t requestSize = (std::min)(length - filled, reader.maxRequestSize());
				const EntropyReadResult result = reader.read(destination + filled, requestSize);
				if (result.status == EntropyReadStatus::Failure) return false;
				if (result.status == EntropyReadStatus::Interrupted) continue;
				filled += result.bytes;
			}
			return true;
		}
