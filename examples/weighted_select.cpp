// 按权重随机选取
#include "RandX.hpp"
#include <iostream>
#include <vector>

int main()
{
    // 权重：A=1, B=2, C=7 → C 被选中概率约 70%
    std::vector<double> weights = { 1.0, 2.0, 7.0 };
    auto idx = RandX::RandWeighted(weights);
    std::cout << "选中索引: " << idx << '\n';
}
