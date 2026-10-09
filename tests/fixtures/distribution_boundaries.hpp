#ifndef RANDX_TESTS_FIXTURES_DISTRIBUTION_BOUNDARIES_HPP
#define RANDX_TESTS_FIXTURES_DISTRIBUTION_BOUNDARIES_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace RandXTest
{
	namespace DistributionBoundaryFixtures
	{
		class CallBudgetEngine
		{
		public:
			using result_type = std::uint64_t;
			static constexpr std::size_t DefaultCallBudget = 1024;

			static constexpr result_type min() noexcept { return 0; }
			static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }

			explicit CallBudgetEngine(std::size_t callBudget = DefaultCallBudget) noexcept
				: callBudget_(callBudget)
			{
			}

			result_type operator()()
			{
				if (callCount_ >= callBudget_)
					throw std::runtime_error("distribution engine call budget exceeded");
				++callCount_;
				return engine_();
			}

			std::size_t callCount() const noexcept { return callCount_; }
			std::size_t callBudget() const noexcept { return callBudget_; }

		private:
			std::size_t callBudget_;
			std::size_t callCount_{0};
			RandX::Xoshiro256StarStar engine_{RandX::DefaultSeed};
		};
	}
}

#endif
