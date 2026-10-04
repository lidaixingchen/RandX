#include "RandX.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace
{
constexpr std::size_t kSimulationCount = 100'000;
constexpr double kFastestServiceMinutes = 4.0;
constexpr double kTypicalServiceMinutes = 7.0;
constexpr double kSlowestServiceMinutes = 13.0;
constexpr double kTheoreticalMeanMinutes =
    (kFastestServiceMinutes + kTypicalServiceMinutes + kSlowestServiceMinutes) / 3.0;
constexpr std::uint64_t kSimulationSeed = 0x8C3C010CB4754C91ULL;
}

int main()
{
    RandX::Xoshiro256StarStar engine{kSimulationSeed};
    double totalServiceMinutes = 0.0;

    for (std::size_t simulation = 0; simulation < kSimulationCount; ++simulation)
    {
        totalServiceMinutes += RandX::RandTriangular(
            engine,
            kFastestServiceMinutes,
            kTypicalServiceMinutes,
            kSlowestServiceMinutes);
    }

    const double estimatedMeanMinutes = totalServiceMinutes / static_cast<double>(kSimulationCount);
    std::cout << std::fixed << std::setprecision(3)
              << "模拟次数：" << kSimulationCount << '\n'
              << "每单服务时间估计均值：" << estimatedMeanMinutes << " 分钟\n"
              << "理论均值：" << kTheoreticalMeanMinutes << " 分钟\n";
    return 0;
}
