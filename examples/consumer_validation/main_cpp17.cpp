#include <RandX_Cpp17.hpp>
#include <iostream>
#include <vector>
#include <numeric>

int main() {
    // 验证 C++17 下生成器与便捷接口
    RandX::Xoshiro256StarStar rng(12345ULL);
    uint64_t val = rng();
    if (val == 0ULL) {
        return 1;
    }

    int rand_val = RandX::RandInt(1, 100);
    if (rand_val < 1 || rand_val > 100) {
        return 2;
    }

    double rand_real = RandX::RandReal(0.0, 1.0);
    if (rand_real < 0.0 || rand_real >= 1.0) {
        return 3;
    }

    std::vector<int> data(10);
    std::iota(data.begin(), data.end(), 1);
    RandX::RandShuffle(data);

    std::cout << "consumer_cpp17 validation successful.\n";
    return 0;
}
