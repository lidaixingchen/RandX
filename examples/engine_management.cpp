// 手动管理引擎：种子、序列化、跳跃、跳过
#include "RandX.hpp"
#include <iostream>

int main()
{
    RandX::Xoshiro256StarStar rng{ RandX::RandomSeed() };

    // 指定引擎的便捷函数
    std::cout << RandX::RandInt(rng, 0, 99) << '\n';

    // 配合标准库 distribution
    std::normal_distribution<double> norm(0.0, 1.0);
    std::cout << norm(rng) << '\n';

    // 序列化 / 反序列化
    auto state = rng.serialize();
    rng.deserialize(state);

    // 跳跃（并行子序列）
    rng.jump();      // 前进 2^128 步
    rng.longJump();  // 前进 2^192 步

    // 跳过
    rng.discard(1000);
}
