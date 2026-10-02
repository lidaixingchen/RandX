		template <class CharT, class Traits>
		class StreamFormatGuard
		{
		public:
			using StreamType = std::basic_ios<CharT, Traits>;

			explicit StreamFormatGuard(StreamType& stream)
				: m_stream(stream),
				  m_flags(stream.flags()),
				  m_fill(stream.fill()),
				  m_width(stream.width())
			{
			}

			~StreamFormatGuard()
			{
				m_stream.flags(m_flags);
				m_stream.fill(m_fill);
				m_stream.width(m_width);
			}

			StreamFormatGuard(const StreamFormatGuard&) = delete;
			StreamFormatGuard& operator=(const StreamFormatGuard&) = delete;

		private:
			StreamType& m_stream;
			typename StreamType::fmtflags m_flags;
			typename StreamType::char_type m_fill;
			std::streamsize m_width;
		};
