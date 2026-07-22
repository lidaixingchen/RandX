// ChaCha20 CSPRNG：密码学安全随机数
#include "RandX.hpp"
#include <cstdint>
#include <iostream>

int main()
{
    // 默认构造：OS 熵自动播种（密码学安全）
    RandX::ChaCha20 rng;
    std::cout << rng() << '\n';

    // 直接取 OS 密码学熵字节
    std::uint8_t key[32];
    RandX::SecureRandomBytes(key, sizeof(key));

    // 密码学安全种子
    std::uint64_t seed = RandX::SecureSeed();

    // 检测当前平台是否走 OS 密码学 API
    if (!RandX::IsOsCryptoEntropyAvailable())
        std::cerr << "警告：当前运行在 std::random_device 兜底路径\n";

    // 与便捷 API 配合
    std::cout << RandX::RandInt(rng, 1, 1000) << '\n';

    // 手动 reseed
    rng.reseed();

    // 显式 key + nonce + counter（仅测试/KAT 复现）
    const std::uint8_t k[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                                 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    const std::uint8_t n[12] = {0,0,0,9,0,0,0,0x4a,0,0,0,0};
    RandX::ChaCha20 kat_rng(k, 32, n, 12, 1);
    std::cout << kat_rng() << '\n';
}
