// 编译期随机（仅 C++23，RandX.hpp）
#include "RandX.hpp"
#include <array>
#include <iostream>

int main()
{
    // 编译期确定结果（固定种子）
    constexpr int dice = RandX::RandIntCE(1, 6);
    static_assert(dice >= 1 && dice <= 6);

    // 自定义种子
    constexpr auto v = RandX::RandIntCE<int, 42>(0, 100);

    // 编译期 Fisher-Yates 洗牌
    constexpr auto shuffled = [] {
        std::array<int, 10> a = {0,1,2,3,4,5,6,7,8,9};
        RandX::ShuffleCE(a.begin(), a.end());
        return a;
    }();

    for (const auto& x : shuffled)
        std::cout << x << ' ';
    std::cout << '\n';

    // ShuffledArray 便捷版
    constexpr auto arr = RandX::ShuffledArray(std::array{1,2,3,4,5});
    for (const auto& x : arr)
        std::cout << x << ' ';
}
