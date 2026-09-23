#include <RandX.hpp>
#include <iostream>
#include <vector>
#include <numeric>
#include <array>

int main() {
    // 验证 C++23 编译期随机生成
    constexpr auto ce_val = RandX::RandIntCE<int, 42ULL>(1, 100);
    static_assert(ce_val >= 1 && ce_val <= 100);

    constexpr auto arr = RandX::ShuffledArray<int, 5, 42ULL>({1, 2, 3, 4, 5});
    static_assert(arr.size() == 5);

    // 验证运行期生成器与 ranges 接口
    RandX::Xoshiro256StarStar rng(12345ULL);
    uint64_t val = rng();
    if (val == 0ULL) {
        return 1;
    }

    int rand_val = RandX::RandInt(1, 100);
    if (rand_val < 1 || rand_val > 100) {
        return 2;
    }

    std::vector<int> data(10);
    std::iota(data.begin(), data.end(), 1);
    RandX::ranges::RandShuffle(data);

    std::cout << "consumer_cpp23 validation successful.\n";
    return 0;
}
