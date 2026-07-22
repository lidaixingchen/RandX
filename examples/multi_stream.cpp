// 多流并行：MakeStreamEngine 创建不重叠子序列
#include "RandX.hpp"
#include <iostream>

int main()
{
    // 创建 4 个不重叠的子序列引擎（每个间隔 2^128 步）
    auto rng0 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(0);
    auto rng1 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(1);
    auto rng2 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(2);
    auto rng3 = RandX::MakeStreamEngine<RandX::Xoshiro256StarStar>(3);

    // 各流独立生成，互不重叠
    std::cout << RandX::RandInt(rng0, 0, 99) << '\n';
    std::cout << RandX::RandInt(rng1, 0, 99) << '\n';
    std::cout << RandX::RandInt(rng2, 0, 99) << '\n';
    std::cout << RandX::RandInt(rng3, 0, 99) << '\n';
}
